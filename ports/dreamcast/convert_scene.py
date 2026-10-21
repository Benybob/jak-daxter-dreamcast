"""Convert a static OpenGOAL background GLB to a bounded, color-only DCM1 mesh.

Textures, animation, skins, collision, materials and game logic are not ported.
Node transforms, triangle indices, COLOR_0 and baseColorFactor are supported.
"""
import argparse
import json
import math
import pathlib
import struct

MAX_TRIANGLES = 8192
COMPONENTS = {5120: ("b", 127), 5121: ("B", 255), 5122: ("h", 32767),
              5123: ("H", 65535), 5125: ("I", 4294967295), 5126: ("f", 1)}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def read_glb(path):
    data = path.read_bytes()
    if len(data) < 12 or struct.unpack_from("<4sII", data) != (b"glTF", 2, len(data)):
        raise ValueError("expected a complete glTF 2 binary (.glb)")
    chunks, offset = {}, 12
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError("short GLB chunk header")
        size, kind = struct.unpack_from("<II", data, offset)
        offset += 8
        if size % 4 or offset + size > len(data) or kind in chunks:
            raise ValueError("invalid or duplicate GLB chunk")
        chunks[kind] = data[offset:offset + size]
        offset += size
    doc = json.loads(chunks[0x4e4f534a])
    binary = chunks.get(0x004e4942, b"")
    buffers = doc.get("buffers", [])
    if len(buffers) != 1 or "uri" in buffers[0] or buffers[0]["byteLength"] > len(binary):
        raise ValueError("only a single embedded GLB buffer is supported")
    return doc, binary[:buffers[0]["byteLength"]]


def accessor(doc, binary, index):
    info = doc["accessors"][index]
    if "sparse" in info:
        raise ValueError("sparse accessors are unsupported")
    view = doc["bufferViews"][info["bufferView"]]
    if view.get("buffer", 0) != 0:
        raise ValueError("external buffer reference")
    code, maximum = COMPONENTS[info["componentType"]]
    width = WIDTHS[info["type"]]
    layout = struct.Struct("<" + code * width)
    stride = view.get("byteStride", layout.size) or layout.size
    start = view.get("byteOffset", 0) + info.get("byteOffset", 0)
    end = view.get("byteOffset", 0) + view["byteLength"]
    count = info["count"]
    if (count < 0 or stride < layout.size or start < view.get("byteOffset", 0) or
            end > len(binary) or start + max(0, count-1)*stride + (layout.size if count else 0) > end):
        raise ValueError("accessor outside its buffer view")
    result = []
    for i in range(count):
        values = layout.unpack_from(binary, start + i*stride)
        if info.get("normalized") and code != "f":
            values = tuple(max(-1, value/maximum) for value in values)
        if not all(math.isfinite(v) for v in values):
            raise ValueError("non-finite accessor value")
        result.append(values)
    return result


def multiply(a, b):
    return [sum(a[k*4+r]*b[c*4+k] for k in range(4)) for c in range(4) for r in range(4)]


def transform(matrix, point):
    return tuple(sum(matrix[k*4+r]*point[k] for k in range(3)) + matrix[12+r]
                 for r in range(3))


def node_matrix(node):
    if "matrix" in node:
        matrix = node["matrix"]
        if len(matrix) != 16 or not all(math.isfinite(v) for v in matrix):
            raise ValueError("invalid node matrix")
        if any(abs(matrix[i]) > 1e-6 for i in (3, 7, 11)) or abs(matrix[15]-1) > 1e-6:
            raise ValueError("projective node matrix unsupported")
        return matrix
    x, y, z, w = node.get("rotation", [0, 0, 0, 1])
    norm = math.sqrt(x*x+y*y+z*z+w*w)
    if not math.isfinite(norm) or norm < 1e-12:
        raise ValueError("invalid quaternion")
    x, y, z, w = (v/norm for v in (x, y, z, w))
    matrix = [1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w), 0,
              2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w), 0,
              2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y), 0, 0, 0, 0, 1]
    scale = node.get("scale", [1, 1, 1])
    for c in range(3):
        for r in range(3):
            matrix[c*4+r] *= scale[c]
    matrix[12:15] = node.get("translation", [0, 0, 0])
    if not all(math.isfinite(v) for v in matrix):
        raise ValueError("non-finite node transform")
    return matrix


def convert(doc, binary, mesh_filter=None, limit=MAX_TRIANGLES, truncate=False, mesh_index=None):
    if mesh_index is not None and not 0 <= mesh_index < len(doc.get("meshes", [])):
        raise ValueError("mesh index outside mesh array")
    output, total, textured = [], 0, 0
    nodes = doc.get("nodes", [])
    roots = doc["scenes"][doc.get("scene", 0)]["nodes"]
    needed = set()

    def select(index, ancestors):
        if index in ancestors:
            raise ValueError("cycle in node graph")
        node = nodes[index]
        mesh_id = node.get("mesh")
        selected = mesh_id is not None and (mesh_index is None or mesh_id == mesh_index)
        if selected and mesh_filter:
            selected = mesh_filter in doc["meshes"][mesh_id].get("name", "")
        for child in node.get("children", []):
            selected = select(child, ancestors | {index}) or selected
        if selected:
            needed.add(index)
        return selected

    if mesh_index is not None or mesh_filter:
        for root in roots:
            select(root, set())

    def walk(index, parent, ancestors):
        nonlocal total, textured
        if (mesh_index is not None or mesh_filter) and index not in needed:
            return
        if index in ancestors:
            raise ValueError("cycle in node graph")
        node = nodes[index]
        world = multiply(parent, node_matrix(node))
        if "skin" in node:
            raise ValueError("skinned geometry is unsupported; use background exports")
        if "mesh" in node:
            mesh = doc["meshes"][node["mesh"]]
            if (mesh_index is None or node["mesh"] == mesh_index) and (not mesh_filter or mesh_filter in mesh.get("name", "")):
                for primitive in mesh["primitives"]:
                    if primitive.get("mode", 4) != 4 or "targets" in primitive:
                        raise ValueError("only static triangle primitives are supported")
                    positions = accessor(doc, binary, primitive["attributes"]["POSITION"])
                    if any(len(p) != 3 for p in positions):
                        raise ValueError("POSITION must be VEC3")
                    colors = None
                    if "COLOR_0" in primitive["attributes"]:
                        colors = accessor(doc, binary, primitive["attributes"]["COLOR_0"])
                        if len(colors) != len(positions) or any(len(c) not in (3, 4) for c in colors):
                            raise ValueError("invalid COLOR_0")
                    indices = (accessor(doc, binary, primitive["indices"]) if "indices" in primitive
                               else [(i,) for i in range(len(positions))])
                    if len(indices) % 3 or any(len(v) != 1 or not isinstance(v[0], int) or
                                              v[0] < 0 or v[0] >= len(positions) for v in indices):
                        raise ValueError("invalid triangle indices")
                    material = doc.get("materials", [])[primitive["material"]] if "material" in primitive else {}
                    pbr = material.get("pbrMetallicRoughness", {})
                    factor = pbr.get("baseColorFactor", [1, 1, 1, 1])
                    if len(factor) != 4 or not all(math.isfinite(c) for c in factor):
                        raise ValueError("invalid baseColorFactor")
                    textured += int("baseColorTexture" in pbr)
                    for start in range(0, len(indices), 3):
                        total += 1
                        if total > limit:
                            if truncate:
                                continue
                            raise ValueError(f"selection exceeds {limit} triangles; crop/select a mesh or explicitly use --truncate")
                        for entry in indices[start:start+3]:
                            i = entry[0]
                            point = transform(world, positions[i])
                            if not all(math.isfinite(v) for v in point):
                                raise ValueError("non-finite transformed position")
                            color = colors[i] if colors else (1, 1, 1)
                            rgb = [round(max(0, min(1, color[k]*factor[k]))*255) for k in range(3)]
                            output.append((point, 0xff000000 | rgb[0]<<16 | rgb[1]<<8 | rgb[2]))
        for child in node.get("children", []):
            walk(child, world, ancestors | {index})

    for root in roots:
        walk(root, IDENTITY, set())
    if not output:
        raise ValueError("no selected triangles")
    lower = [min(point[k] for point, _ in output) for k in range(3)]
    upper = [max(point[k] for point, _ in output) for k in range(3)]
    center = [(a+b)/2 for a, b in zip(lower, upper)]
    radius = max(math.sqrt(sum((p[k]-center[k])**2 for k in range(3))) for p, _ in output)
    if not math.isfinite(radius) or radius < 1e-12:
        raise ValueError("degenerate scene bounds")
    data = bytearray(struct.pack("<4sI", b"DCM1", len(output)))
    for point, color in output:
        data += struct.pack("<fffI", *((point[k]-center[k])/radius for k in range(3)), color)
    return data, {"source_triangles": total, "output_triangles": len(output)//3,
                  "truncated": total > limit, "bytes": len(data),
                  "textured_primitives_without_textures": textured,
                  "original_center": center, "original_radius": radius}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path, nargs="?")
    parser.add_argument("--mesh", help="mesh name substring")
    parser.add_argument("--mesh-index", type=int, help="exact zero-based mesh index, including unnamed OpenGOAL meshes")
    parser.add_argument("--list-meshes", action="store_true")
    parser.add_argument("--max-triangles", type=int, default=MAX_TRIANGLES)
    parser.add_argument("--truncate", action="store_true", help="explicitly keep only the first N triangles")
    args = parser.parse_args()
    try:
        doc, binary = read_glb(args.input)
        if args.list_meshes:
            print(json.dumps([m.get("name", str(i)) for i, m in enumerate(doc["meshes"])], indent=2))
            return
        if not args.output or not 1 <= args.max_triangles <= MAX_TRIANGLES:
            parser.error("output required; --max-triangles must be 1..8192")
        data, report = convert(doc, binary, args.mesh, args.max_triangles, args.truncate, args.mesh_index)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
        print(json.dumps(report, indent=2))
    except (ValueError, KeyError, IndexError, TypeError, OSError, struct.error) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
