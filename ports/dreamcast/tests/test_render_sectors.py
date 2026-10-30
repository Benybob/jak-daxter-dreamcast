"""Synthetic DRM2 tests. Written for Codex to execute. Not run by the author.

These tests build GLB buffers in memory. They do not read game files, do not
claim a native fit, and do not require network access. Pillow is used only to
make tiny PNG data URIs.
"""

from __future__ import annotations

import base64
import hashlib
import json
import struct
import unittest
import zlib
import sys
from contextlib import contextmanager
from io import BytesIO
from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import prepare_render_sectors as rs


def _png_uri(color, size=8):
    image = Image.new("RGBA", (size, size), color)
    buffer = BytesIO()
    image.save(buffer, format="PNG")
    encoded = base64.b64encode(buffer.getvalue()).decode("ascii")
    return "data:image/png;base64," + encoded


def _glb(doc, binary):
    raw = bytes(binary)
    doc = dict(doc)
    doc["buffers"] = [{"byteLength": len(raw)}]
    json_bytes = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    json_bytes += b" " * ((4 - len(json_bytes) % 4) % 4)
    binary_pad = raw + b"\x00" * ((4 - len(raw) % 4) % 4)
    total = 12 + 8 + len(json_bytes) + 8 + len(binary_pad)
    out = bytearray()
    out += struct.pack("<4sII", b"glTF", 2, total)
    out += struct.pack("<II", len(json_bytes), 0x4E4F534A)
    out += json_bytes
    out += struct.pack("<II", len(binary_pad), 0x004E4942)
    out += binary_pad
    return bytes(out)


class GlbBuilder:
    def __init__(self):
        self.buffer = bytearray()
        self.buffer_views = []
        self.accessors = []
        self.meshes = []
        self.nodes = []
        self.materials = []
        self.textures = []
        self.images = []
        self.samplers = []
        self.scenes = [{"nodes": []}]
        self.scene = 0
        self.extra = {}

    def _align(self):
        while len(self.buffer) % 4:
            self.buffer.append(0)

    def add_view(self, data):
        self._align()
        start = len(self.buffer)
        self.buffer += data
        self.buffer_views.append({"buffer": 0, "byteOffset": start, "byteLength": len(data)})
        return len(self.buffer_views) - 1

    def add_accessor(self, view, component, kind, count, **extra):
        info = {
            "bufferView": view,
            "componentType": component,
            "type": kind,
            "count": count,
        }
        info.update(extra)
        self.accessors.append(info)
        return len(self.accessors) - 1

    def add_vec3(self, triples):
        raw = b"".join(struct.pack("<3f", *triple) for triple in triples)
        return self.add_accessor(self.add_view(raw), 5126, "VEC3", len(triples))

    def add_vec2(self, pairs):
        raw = b"".join(struct.pack("<2f", *pair) for pair in pairs)
        return self.add_accessor(self.add_view(raw), 5126, "VEC2", len(pairs))

    def add_vec4(self, tuples):
        raw = b"".join(struct.pack("<4f", *item) for item in tuples)
        return self.add_accessor(self.add_view(raw), 5126, "VEC4", len(tuples))

    def add_indices(self, indices):
        raw = b"".join(struct.pack("<H", index) for index in indices)
        return self.add_accessor(self.add_view(raw), 5123, "SCALAR", len(indices))

    def add_mesh(self, primitives, **extra):
        mesh = {"primitives": primitives}
        mesh.update(extra)
        self.meshes.append(mesh)
        return len(self.meshes) - 1

    def add_node(self, mesh=None, children=None, **transform):
        node = {}
        if mesh is not None:
            node["mesh"] = mesh
        if children is not None:
            node["children"] = list(children)
        node.update(transform)
        self.nodes.append(node)
        return len(self.nodes) - 1

    def add_solid_material(self, color, mode="OPAQUE", cutoff=None, sampler=None, tex_coord=0, extensions=None):
        image_index = len(self.images)
        self.images.append({"uri": _png_uri(color)})
        texture = {"source": image_index}
        if sampler is not None:
            texture["sampler"] = sampler
        if extensions is not None:
            texture["extensions"] = extensions
        texture_index = len(self.textures)
        self.textures.append(texture)
        info = {"index": texture_index}
        if tex_coord:
            info["texCoord"] = tex_coord
        pbr = {"baseColorTexture": info}
        material = {"pbrMetallicRoughness": pbr, "alphaMode": mode}
        if cutoff is not None:
            material["alphaCutoff"] = cutoff
        self.materials.append(material)
        return len(self.materials) - 1

    def to_bytes(self):
        doc = {
            "asset": {"version": "2.0"},
            "scene": self.scene,
            "scenes": self.scenes,
            "nodes": self.nodes,
            "meshes": self.meshes,
            "accessors": self.accessors,
            "bufferViews": self.buffer_views,
            "buffers": [{"byteLength": len(self.buffer)}],
        }
        if self.materials:
            doc["materials"] = self.materials
        if self.textures:
            doc["textures"] = self.textures
        if self.images:
            doc["images"] = self.images
        if self.samplers:
            doc["samplers"] = self.samplers
        doc.update(self.extra)
        return _glb(doc, self.buffer)


def _parse(blob):
    return rs.parse_glb_bytes(blob)


def _package(blob, **kwargs):
    doc, binary = _parse(blob)
    return rs.build_package(doc, binary, source_sha256=hashlib.sha256(blob).digest(), **kwargs)


def _one(positions, uvs=None, colors=None, material=None, node=None, normals=None):
    builder = GlbBuilder()
    attributes = {"POSITION": builder.add_vec3(positions)}
    if uvs is not None:
        attributes["TEXCOORD_0"] = builder.add_vec2(uvs)
    if colors is not None:
        width = len(colors[0])
        if width == 4:
            attributes["COLOR_0"] = builder.add_vec4(colors)
        else:
            raw = b"".join(struct.pack("<3f", *color) for color in colors)
            attributes["COLOR_0"] = builder.add_accessor(builder.add_view(raw), 5126, "VEC3", len(colors))
    if normals is not None:
        attributes["NORMAL"] = builder.add_vec3(normals)
    primitive = {
        "attributes": attributes,
        "indices": builder.add_indices(list(range(len(positions)))),
        "mode": 4,
    }
    if material is not None:
        primitive["material"] = material
    mesh = builder.add_mesh([primitive])
    node_id = builder.add_node(mesh=mesh, **(node or {}))
    builder.scenes[0]["nodes"] = [node_id]
    return builder


def _interior():
    positions = [(1.0, 0.0, 1.0), (2.0, 0.0, 1.0), (1.0, 3.0, 2.0)]
    return _one(positions)


def _face_positions(package):
    faces = []
    for blob in list(package.sectors.values()) + [package.global_sector]:
        for record in rs.parse_sector(blob)["records"]:
            faces.append(record)
    return faces


def _ids(package):
    local, glob = set(), set()
    for blob in package.sectors.values():
        for record in rs.parse_sector(blob)["records"]:
            local.add(record["source_id"])
    for record in rs.parse_sector(package.global_sector)["records"]:
        glob.add(record["source_id"])
    return local, glob


def _solo(material, images=None, textures=None, samplers=None):
    builder = GlbBuilder()
    for image in images or []:
        builder.images.append({"uri": _png_uri(image)})
    for sampler in samplers or []:
        builder.samplers.append(sampler)
    for texture in textures or []:
        builder.textures.append(texture)
    builder.materials.append(material)
    positions = [(1.0, 0.0, 1.0), (2.0, 0.0, 1.0), (1.0, 1.0, 1.0)]
    primitive = {
        "attributes": {"POSITION": builder.add_vec3(positions)},
        "indices": builder.add_indices([0, 1, 2]),
        "mode": 4,
        "material": 0,
    }
    builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
    return builder


@contextmanager
def _patched(**values):
    saved = {name: getattr(rs, name) for name in values}
    try:
        for name, value in values.items():
            setattr(rs, name, value)
        yield
    finally:
        for name, value in saved.items():
            setattr(rs, name, value)


class RenderSectorTests(unittest.TestCase):
    def test_layout_offsets(self):
        self.assertEqual(rs.HEADER_SIZE, 256)
        self.assertEqual(rs.FACE_STRUCT.size, 116)
        self.assertEqual(rs.VERTEX_BYTES, 36)
        self.assertEqual(rs.MATERIAL_STRUCT.size, 64)
        self.assertEqual(rs.HEADER_OFFSETS["magic"], 0)
        self.assertEqual(rs.HEADER_OFFSETS["version"], 4)
        self.assertEqual(rs.HEADER_OFFSETS["cell_size"], 48)
        self.assertEqual(rs.HEADER_OFFSETS["per_sector_cap"], 56)
        self.assertEqual(rs.HEADER_OFFSETS["window_unique_cap"], 64)
        self.assertEqual(rs.HEADER_OFFSETS["source_cap"], 72)
        self.assertEqual(rs.HEADER_OFFSETS["package_byte_cap"], 96)
        self.assertEqual(rs.HEADER_OFFSETS["source_sha256"], 164)
        self.assertEqual(rs.HEADER_OFFSETS["material_stride"], 208)
        self.assertEqual(rs.HEADER_OFFSETS["normal_encoding"], 212)
        self.assertEqual(rs.HEADER_OFFSETS["reserved_11"] + 4, 256)
        self.assertEqual(rs.SECTOR_HEADER_STRUCT.size, 32)
        self.assertEqual(rs.TEXTURE_STRUCT.size, 16)
        self.assertEqual(rs.ENTRY_STRUCT.size, 32)
        self.assertEqual(rs.PER_SECTOR_CAP * rs.FACE_BYTES, 475136)
        self.assertLess(rs.PER_SECTOR_CAP * rs.FACE_BYTES, 512 * 1024)
        self.assertLess(rs.PER_SECTOR_CAP * rs.FACE_BYTES + rs.COMPRESS_SLACK, 513 * 1024)

    def test_magic_and_version_are_not_collision(self):
        package = _package(_interior().to_bytes())
        self.assertEqual(package.manifest[:4], b"DRM2")
        self.assertEqual(package.global_sector[:4], b"DRS2")
        self.assertEqual(struct.unpack_from("<I", package.manifest, 4)[0], 2)
        for magic in (b"DCS1", b"DCSS", b"DCR1", b"DCT1", b"DCM1", b"DRM1", b"DRS1"):
            self.assertNotEqual(package.manifest[:4], magic)
            self.assertNotEqual(package.global_sector[:4], magic)
        header = rs.parse_manifest(package.manifest)
        self.assertEqual(header["version"], 2)
        self.assertEqual(header["face_stride"], 116)
        self.assertEqual(header["vertex_stride"], 36)
        self.assertEqual(header["material_stride"], 64)
        self.assertEqual(header["normal_encoding"], rs.NORMAL_WORLD_UNIT)
        self.assertEqual(package.summary["format"], "DRM2")
        self.assertEqual(package.summary["face_bytes"], 116)
        self.assertEqual(package.summary["material_bytes"], 64)
        self.assertFalse(package.summary["public_abi"])
        self.assertFalse(package.summary["host_budget_is_native_fit"])

    def test_identity_triangle_bytes_and_color(self):
        positions = [(1.0, 2.0, 3.0), (4.0, 5.0, 6.0), (7.0, 8.0, 9.0)]
        package = _package(_one(positions).to_bytes())
        header = rs.parse_manifest(package.manifest)
        self.assertEqual(header["source_count"], 1)
        self.assertEqual(header["global_count"], 0)
        self.assertEqual(header["texture_count"], 1)
        self.assertEqual(header["textures"][0]["width"], 8)
        self.assertEqual(header["textures"][0]["height"], 8)
        self.assertEqual(header["textures"][0]["format"], rs.TEXTURE_ARGB1555)
        pixels = header["textures"][0]["pixels"]
        self.assertEqual(set(struct.unpack("<64H", pixels)), {0xFFFF})
        face = rs.parse_sector(next(iter(package.sectors.values())))["records"][0]
        # These three positions are colinear, so the flat normal is zero.
        expected = []
        for point in positions:
            expected.append((
                rs.f32(point[0]), rs.f32(point[1]), rs.f32(point[2]),
                0.0, 0.0, 0.0, 0.0, 0.0, 0xFFFFFFFF,
            ))
        self.assertEqual(face["vertices"], tuple(expected))
        self.assertEqual(len(face["raw"]), 116)
        self.assertEqual(face["material_id"], 0)
        self.assertEqual(face["source_id"], 0)
        material = header["materials"][0]
        self.assertEqual(material["base_flags"], 0)
        self.assertEqual(material["base_texture"], 0)
        self.assertEqual(material["alpha_mode"], 0)
        self.assertEqual(material["alpha_cutoff"], 0.5)
        self.assertEqual(material["strength_texture"], rs.ABSENT_TEXTURE)
        self.assertEqual(material["spec_color_texture"], rs.ABSENT_TEXTURE)
        self.assertEqual(material["metallic_factor"], 1.0)
        self.assertEqual(material["roughness_factor"], 1.0)
        self.assertEqual(material["specular_factor"], 1.0)
        self.assertEqual(material["specular_color_factor"], (1.0, 1.0, 1.0))

    def test_translation_rotation_scale_and_parent(self):
        positions = [(1.0, 2.0, 3.0), (1.0, 2.0, 4.0), (1.0, 3.0, 3.0)]
        package = _package(_one(positions, node={"rotation": [0, 1, 0, 0]}).to_bytes())
        face = _face_positions(package)[0]
        self.assertEqual(face["vertices"][0][:3], (-1.0, 2.0, -3.0))

        scaled = _one(
            [(1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (1.0, 0.0, 1.0)],
            node={"translation": [10, -3, 0.5], "scale": [2, 3, 4]},
        )
        package = _package(scaled.to_bytes())
        face = _face_positions(package)[0]
        self.assertEqual(face["vertices"][0][:3], (12.0, -3.0, 0.5))

        parent = GlbBuilder()
        positions = [(1.0, 0.0, 1.0), (2.0, 0.0, 1.0), (1.0, 1.0, 1.0)]
        mesh = parent.add_mesh([{
            "attributes": {"POSITION": parent.add_vec3(positions)},
            "indices": parent.add_indices([0, 1, 2]),
            "mode": 4,
        }])
        child = parent.add_node(mesh=mesh, translation=[5, 0, 0])
        root = parent.add_node(children=[child], translation=[100, 0, 0])
        parent.scenes[0]["nodes"] = [root]
        package = _package(parent.to_bytes())
        face = _face_positions(package)[0]
        self.assertEqual(face["vertices"][0][:3], (106.0, 0.0, 1.0))

    def test_matrix_translation_and_ambiguous_rejected(self):
        matrix = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 9, 0, 0, 1]
        builder = _one([(1, 0, 1), (2, 0, 1), (1, 1, 1)], node={"matrix": matrix})
        package = _package(builder.to_bytes())
        self.assertEqual(_face_positions(package)[0]["vertices"][0][0], 10.0)
        bad = _one([(1, 0, 1), (2, 0, 1), (1, 1, 1)], node={"matrix": matrix, "translation": [1, 0, 0]})
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(bad.to_bytes())
        self.assertIn("ambiguous matrix and TRS", str(caught.exception))

    def test_multiple_instances_and_dag(self):
        builder = GlbBuilder()
        positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
        mesh = builder.add_mesh([{
            "attributes": {"POSITION": builder.add_vec3(positions)},
            "indices": builder.add_indices([0, 1, 2]),
            "mode": 4,
        }])
        first = builder.add_node(mesh=mesh, translation=[0, 0, 0])
        second = builder.add_node(mesh=mesh, translation=[0, 4, 0])
        builder.scenes[0]["nodes"] = [first, second]
        package = _package(builder.to_bytes())
        self.assertEqual(package.source_count, 2)
        self.assertEqual(package.position_decode_count, 1)
        self.assertEqual(package.position_transform_count, 2)
        faces = sorted(_face_positions(package), key=lambda item: item["source_id"])
        self.assertEqual([item["source_id"] for item in faces], [0, 1])
        self.assertEqual(faces[0]["vertices"][0][1], 0.0)
        self.assertEqual(faces[1]["vertices"][0][1], 4.0)
        self.assertNotEqual(faces[0]["raw"], faces[1]["raw"])

        dag = GlbBuilder()
        mesh = dag.add_mesh([{
            "attributes": {"POSITION": dag.add_vec3(positions)},
            "indices": dag.add_indices([0, 1, 2]),
            "mode": 4,
        }])
        shared = dag.add_node(mesh=mesh, translation=[1, 0, 1])
        left = dag.add_node(children=[shared], translation=[0, 0, 0])
        right = dag.add_node(children=[shared], translation=[0, 5, 0])
        dag.scenes[0]["nodes"] = [left, right]
        package = _package(dag.to_bytes())
        self.assertEqual(package.source_count, 2)
        ys = sorted(face["vertices"][0][1] for face in _face_positions(package))
        self.assertEqual(ys, [0.0, 5.0])

    def test_child_ids_follow_parent_mesh(self):
        builder = GlbBuilder()

        def mesh_at(x):
            positions = [(x, 0, 1), (x + 0.2, 0, 1), (x, 0.2, 1.2)]
            return builder.add_mesh([{
                "attributes": {"POSITION": builder.add_vec3(positions)},
                "indices": builder.add_indices([0, 1, 2]),
                "mode": 4,
            }])

        child = builder.add_node(mesh=mesh_at(1.5))
        root = builder.add_node(mesh=mesh_at(1.0), children=[child])
        builder.scenes[0]["nodes"] = [root]
        package = _package(builder.to_bytes())
        faces = sorted(_face_positions(package), key=lambda item: item["source_id"])
        self.assertEqual(faces[0]["vertices"][0][0], 1.0)
        self.assertEqual(faces[1]["vertices"][0][0], 1.5)

    def test_inactive_scene_and_unreferenced_mesh_ignored(self):
        builder = _interior()
        other = builder.add_vec3([(50, 0, 50), (51, 0, 50), (50, 1, 50)])
        builder.add_mesh([{"attributes": {"POSITION": other}, "mode": 4}])
        builder.scenes.append({"nodes": []})
        builder.scene = 0
        package = _package(builder.to_bytes())
        self.assertEqual(package.source_count, 1)

        inactive = GlbBuilder()
        keep = inactive.add_mesh([{
            "attributes": {"POSITION": inactive.add_vec3([(1, 0, 1), (2, 0, 1), (1, 1, 1)])},
            "mode": 4,
        }])
        drop = inactive.add_mesh([{
            "attributes": {"POSITION": inactive.add_vec3([(30, 0, 30), (31, 0, 30), (30, 1, 30)])},
            "mode": 4,
        }])
        keep_node = inactive.add_node(mesh=keep)
        inactive.add_node(mesh=drop)
        inactive.scenes = [{"nodes": [keep_node]}, {"nodes": [1]}]
        inactive.scene = 0
        package = _package(inactive.to_bytes())
        self.assertEqual(package.source_count, 1)
        self.assertEqual(_face_positions(package)[0]["vertices"][0][0], 1.0)

    def test_shared_position_accessor_decoded_once(self):
        builder = GlbBuilder()
        positions = builder.add_vec3([(1, 0, 1), (2, 0, 1), (1, 1, 1)])
        indices = builder.add_indices([0, 1, 2])
        primitives = [{"attributes": {"POSITION": positions}, "indices": indices, "mode": 4} for _ in range(20)]
        mesh = builder.add_mesh(primitives)
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=mesh)]
        package = _package(builder.to_bytes())
        self.assertEqual(package.source_count, 20)
        self.assertEqual(package.position_decode_count, 1)
        self.assertEqual(package.position_transform_count, 1)
        self.assertEqual(package.accessor_decode_count, 2)

    def test_supplied_normal_kept_and_uv_not_flipped(self):
        positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
        normals = [(0, 1, 0), (0, 1, 0), (0, 1, 0)]
        uvs = [(0.25, 0.75), (0.5, 0.25), (1.5, -2.0)]
        package = _package(_one(positions, uvs=uvs, normals=normals).to_bytes())
        vertices = _face_positions(package)[0]["vertices"]
        self.assertEqual([(vertex[3], vertex[4]) for vertex in vertices], [(rs.f32(u), rs.f32(v)) for u, v in uvs])
        self.assertNotEqual(vertices[0][4], rs.f32(1.0 - 0.75))
        self.assertEqual([(vertex[5], vertex[6], vertex[7]) for vertex in vertices], [(0.0, 1.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)])

    def test_color_factor_clamp_and_rgb_alpha(self):
        builder = GlbBuilder()
        positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
        colors = [(2.0, -1.0, 0.5, 1.0), (1, 0, 0, 1), (0, 0, 0, 0.5)]
        attributes = {
            "POSITION": builder.add_vec3(positions),
            "COLOR_0": builder.add_vec4(colors),
        }
        builder.materials.append({
            "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]},
        })
        primitive = {"attributes": attributes, "indices": builder.add_indices([0, 1, 2]), "mode": 4, "material": 0}
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        package = _package(builder.to_bytes())
        face = _face_positions(package)[0]
        self.assertEqual(face["vertices"][0][8], 0xFFFF0080)
        self.assertEqual(face["vertices"][1][8], 0xFFFF0000)
        self.assertEqual(face["vertices"][2][8], 0x80000000)

        rgb = _one(positions, colors=[(1, 0, 0), (0, 1, 0), (0, 0, 1)])
        package = _package(rgb.to_bytes())
        face = _face_positions(package)[0]
        self.assertEqual(face["vertices"][0][8] >> 24, 255)

        half = GlbBuilder()
        half.materials.append({"pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.5, 0.5, 0.5]}})
        primitive = {
            "attributes": {"POSITION": half.add_vec3(positions), "COLOR_0": half.add_vec4([(1, 1, 1, 1)] * 3)},
            "indices": half.add_indices([0, 1, 2]),
            "material": 0,
        }
        half.scenes[0]["nodes"] = [half.add_node(mesh=half.add_mesh([primitive]))]
        package = _package(half.to_bytes())
        # 0.5 * 255 = 127.5, Python round half to even -> 128.
        self.assertEqual(_face_positions(package)[0]["vertices"][0][8], 0x80808080)

        boost = GlbBuilder()
        boost.materials.append({"pbrMetallicRoughness": {"baseColorFactor": [2, 2, 2, 2]}})
        primitive = {
            "attributes": {
                "POSITION": boost.add_vec3(positions),
                "COLOR_0": boost.add_vec4([(0.5, 0.5, 0.5, 0.5)] * 3),
            },
            "indices": boost.add_indices([0, 1, 2]),
            "material": 0,
        }
        boost.scenes[0]["nodes"] = [boost.add_node(mesh=boost.add_mesh([primitive]))]
        package = _package(boost.to_bytes())
        # 0.5 * 2 clamps at 1. The factor is the PS2 blend compensation.
        self.assertEqual(_face_positions(package)[0]["vertices"][0][8], 0xFFFFFFFF)

    def test_wrap_flags_mask_and_texture_dedup(self):
        builder = GlbBuilder()
        builder.samplers.append({"wrapS": 33071, "wrapT": 33648})
        material = builder.add_solid_material((255, 0, 0, 128), mode="MASK", cutoff=0.25, sampler=0)
        builder.textures.append({"source": 0})
        second = {
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}},
            "alphaMode": "MASK",
            "alphaCutoff": 0.25,
        }
        builder.materials.append(second)
        positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
        attributes = {"POSITION": builder.add_vec3(positions), "TEXCOORD_0": builder.add_vec2([(0, 0), (1, 0), (0, 1)])}
        indices = builder.add_indices([0, 1, 2])
        primitives = []
        for material_index in (material, 1):
            primitives.append({"attributes": attributes, "indices": indices, "material": material_index, "mode": 4})
        builder.materials[1] = {
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}},
            "alphaMode": "MASK",
            "alphaCutoff": 0.25,
        }
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh(primitives))]
        package = _package(builder.to_bytes())
        header = rs.parse_manifest(package.manifest)
        self.assertEqual(header["texture_count"], 1)
        self.assertEqual(header["material_count"], 2)
        self.assertEqual(header["materials"][0]["base_flags"], rs.FLAG_MASK | rs.FLAG_CLAMP_S | rs.FLAG_MIRROR_T)
        self.assertEqual(header["materials"][1]["base_flags"], rs.FLAG_MASK)
        self.assertEqual(header["materials"][0]["base_texture"], 0)
        self.assertEqual(header["materials"][1]["base_texture"], 0)
        self.assertEqual(header["materials"][0]["alpha_mode"], 1)

    def test_opaque_and_mask_pixels(self):
        opaque = GlbBuilder()
        opaque.add_solid_material((10, 20, 30, 0), mode="OPAQUE")
        positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
        primitive = {
            "attributes": {"POSITION": opaque.add_vec3(positions)},
            "indices": opaque.add_indices([0, 1, 2]),
            "material": 0,
        }
        opaque.scenes[0]["nodes"] = [opaque.add_node(mesh=opaque.add_mesh([primitive]))]
        header = rs.parse_manifest(_package(opaque.to_bytes()).manifest)
        pixels = struct.unpack("<64H", header["textures"][0]["pixels"])
        self.assertTrue(all(value & 0x8000 for value in pixels))

        masked = GlbBuilder()
        masked.add_solid_material((255, 255, 255, 0), mode="MASK", cutoff=0.5)
        primitive = {
            "attributes": {"POSITION": masked.add_vec3(positions)},
            "indices": masked.add_indices([0, 1, 2]),
            "material": 0,
        }
        masked.scenes[0]["nodes"] = [masked.add_node(mesh=masked.add_mesh([primitive]))]
        header = rs.parse_manifest(_package(masked.to_bytes()).manifest)
        pixels = struct.unpack("<64H", header["textures"][0]["pixels"])
        self.assertEqual(set(pixels), {0x7FFF})

    def test_cutoff_changes_texture_identity(self):
        builder = GlbBuilder()
        builder.images.append({"uri": _png_uri((255, 255, 255, 255))})
        builder.textures.append({"source": 0})
        builder.materials.append({"alphaMode": "MASK", "alphaCutoff": 0.2, "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}})
        builder.materials.append({"alphaMode": "MASK", "alphaCutoff": 0.8, "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}})
        positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
        attributes = {"POSITION": builder.add_vec3(positions)}
        indices = builder.add_indices([0, 1, 2])
        primitives = [
            {"attributes": attributes, "indices": indices, "material": 0},
            {"attributes": attributes, "indices": indices, "material": 1},
        ]
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh(primitives))]
        header = rs.parse_manifest(_package(builder.to_bytes()).manifest)
        self.assertEqual(header["texture_count"], 2)
        self.assertEqual(header["material_count"], 2)

    def test_degenerate_and_winding_kept(self):
        positions = [(8.0, 1.0, 8.0), (8.0, 1.0, 8.0), (8.0, 1.0, 8.0)]
        clockwise = [(8.0, 0.0, 8.0), (9.0, 0.0, 8.0), (8.0, 0.0, 9.0)]
        builder = GlbBuilder()
        attributes = {"POSITION": builder.add_vec3(positions + clockwise)}
        primitive = {"attributes": attributes, "indices": builder.add_indices([0, 1, 2, 3, 4, 5])}
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        package = _package(builder.to_bytes())
        self.assertEqual(package.source_count, 2)
        faces = sorted(_face_positions(package), key=lambda item: item["source_id"])
        self.assertEqual(faces[0]["vertices"][0][:3], (8.0, 1.0, 8.0))
        self.assertEqual(faces[0]["vertices"][1][:3], faces[0]["vertices"][0][:3])
        self.assertEqual(faces[0]["vertices"][0][5:8], (0.0, 0.0, 0.0))
        self.assertEqual([vertex[:3] for vertex in faces[1]["vertices"]], clockwise)
        self.assertEqual(faces[1]["vertices"][0][5:8], (0.0, -1.0, 0.0))
        self.assertEqual(package.flat_normal_faces, 2)
        self.assertEqual(package.authored_normal_faces, 0)

    def test_negative_cell_and_origin_corner(self):
        positions = [(-8.0, 1.0, 1.0), (-7.0, 1.0, 1.2), (-8.0, 2.0, 1.4)]
        package = _package(_one(positions).to_bytes())
        self.assertEqual(set(package.sectors), {(-1, 0)})
        header = rs.parse_manifest(package.manifest)
        self.assertEqual(header["ix_min"], -1)
        self.assertEqual(header["iz_min"], 0)
        self.assertIn("sectors/-1_0.drs", package.summary["native_files"])

        corner = _one([(0.0, 5.0, 0.0), (0.0, 5.0, 0.0), (0.0, 5.0, 0.0)])
        package = _package(corner.to_bytes())
        self.assertEqual(set(package.sectors), {(-1, -1), (-1, 0), (0, -1), (0, 0)})
        self.assertEqual(package.summary["replica_count"], 4)
        self.assertEqual(package.summary["global_count"], 0)
        window = rs.assemble_window(package, (0.0, 0.0, 0.0, 0.0))
        self.assertEqual(len(window), 1)
        self.assertEqual(window[0]["source_id"], 0)

    def test_boundary_replica_dedup_keeps_whole_face(self):
        positions = [(15.0, 0.0, 8.0), (17.0, 0.0, 8.0), (16.0, 1.0, 8.0)]
        package = _package(_one(positions).to_bytes())
        self.assertEqual(set(package.sectors), {(0, 0), (1, 0)})
        self.assertEqual(package.summary["replica_count"], 2)
        raws = []
        for blob in package.sectors.values():
            records = rs.parse_sector(blob)["records"]
            self.assertEqual(len(records), 1)
            raws.append(records[0]["raw"])
            self.assertEqual(records[0]["vertices"][0][0], 15.0)
            self.assertEqual(records[0]["vertices"][1][0], 17.0)
        self.assertEqual(raws[0], raws[1])
        self.assertEqual(len(raws[0]), 116)
        window = rs.assemble_window(package, (0.0, 32.0, 0.0, 16.0))
        self.assertEqual(len(window), 1)
        self.assertEqual(window[0]["raw"], raws[0])

    def test_span_rules_local_and_global(self):
        local = _one([(1.0, 0.0, 1.0), (65.0, 0.0, 1.0), (1.0, 0.0, 2.0)])
        package = _package(local.to_bytes())
        self.assertEqual(package.summary["global_count"], 0)
        self.assertGreaterEqual(package.summary["replica_count"], 2)
        self.assertEqual(_ids(package)[0], {0})

        huge = _one([(0.0, 0.0, 0.0), (1897.0, 0.0, 0.0), (0.0, 0.0, 10.0)])
        package = _package(huge.to_bytes())
        self.assertEqual(package.summary["global_count"], 1)
        self.assertEqual(package.summary["local_sector_count"], 0)
        self.assertEqual(package.summary["replica_count"], 0)
        face = rs.parse_sector(package.global_sector)["records"][0]
        self.assertEqual(face["vertices"][1][0], 1897.0)
        self.assertEqual(len(face["vertices"]), 3)

        tall = _one([(1.0, 0.0, 1.0), (1.0, 100.0, 1.0), (2.0, 0.0, 2.0)])
        package = _package(tall.to_bytes())
        self.assertEqual(package.summary["global_count"], 1)
        self.assertEqual(set(package.sectors), set())
        self.assertEqual(rs.parse_sector(package.global_sector)["records"][0]["vertices"][1][1], 100.0)

        wide = _one([(0.0, 0.0, 0.0), (60.0, 0.0, 0.0), (0.0, 0.0, 60.0)])
        package = _package(wide.to_bytes(), cell_size=4)
        self.assertEqual(package.summary["global_count"], 1)
        self.assertEqual(package.summary["local_sector_count"], 0)

    def test_all_source_ids_union_and_disjoint(self):
        builder = GlbBuilder()
        positions = [
            (1, 0, 1), (2, 0, 1), (1, 1, 1),
            (15, 0, 8), (17, 0, 8), (16, 1, 8),
            (0, 0, 0), (1897, 0, 0), (0, 0, 5),
        ]
        primitive = {
            "attributes": {"POSITION": builder.add_vec3(positions)},
            "indices": builder.add_indices(list(range(9))),
        }
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        package = _package(builder.to_bytes())
        local, glob = _ids(package)
        self.assertTrue(local.isdisjoint(glob))
        self.assertEqual(local | glob, {0, 1, 2})
        self.assertIn(2, glob)
        self.assertNotIn(2, local)

    def test_window_filter_and_order(self):
        builder = GlbBuilder()
        positions = [
            (1, 0, 1), (2, 0, 1), (1, 1, 1.2),
            (5000, 0, 1), (5200, 0, 1), (5000, 0, 2),
            (-10, 0, 1), (80, 0, 1), (-10, 0, 2),
        ]
        primitive = {
            "attributes": {"POSITION": builder.add_vec3(positions)},
            "indices": builder.add_indices(list(range(9))),
        }
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        package = _package(builder.to_bytes())
        window = rs.assemble_window(package, (0.0, 16.0, 0.0, 16.0))
        self.assertEqual([face["source_id"] for face in window], [0, 2])
        far = rs.assemble_window(package, (4000.0, 5300.0, 0.0, 16.0))
        self.assertEqual([face["source_id"] for face in far], [1])
        with self.assertRaises(rs.RenderSectorError):
            rs.assemble_window(package, (1.0, 0.0, 0.0, 1.0))
        with self.assertRaises(rs.RenderSectorError):
            rs.assemble_window(package, (0.0, float("nan"), 0.0, 1.0))

    def test_window_cap_8192_and_8193(self):
        builder = GlbBuilder()
        positions = []
        for base in (1.0, 17.0):
            for _ in range(4096):
                positions.extend([(base, 0.0, 1.0), (base + 0.01, 0.0, 1.0), (base, 0.01, 1.2)])
        positions.extend([(50000.0, 0.0, 1.0), (51000.0, 0.0, 1.0), (50000.0, 1.0, 2.0)])
        positions.extend([(1000.0, 0.0, 1.0), (2000.0, 0.0, 1.0), (1000.0, 0.0, 2.0)])
        primitive = {
            "attributes": {"POSITION": builder.add_vec3(positions)},
            "indices": builder.add_indices(list(range(len(positions)))),
        }
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        package = _package(builder.to_bytes())
        self.assertEqual(package.source_count, 8194)
        fitted = rs.assemble_window(package, (0.0, 32.0, 0.0, 16.0))
        self.assertEqual(len(fitted), 8192)
        self.assertEqual(fitted[0]["source_id"], 0)
        self.assertEqual(fitted[-1]["source_id"], 8191)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.assemble_window(package, (0.0, 2500.0, 0.0, 16.0))
        self.assertIn("unique faces exceed window cap", str(caught.exception))

    def test_replica_payload_mismatch(self):
        positions = [(15.0, 0.0, 8.0), (17.0, 0.0, 8.0), (16.0, 1.0, 8.0)]
        package = _package(_one(positions).to_bytes())
        (ix, iz), blob = sorted(package.sectors.items())[0]
        parsed = rs.parse_sector(blob)
        mutated = bytearray(parsed["records"][0]["raw"])
        mutated[8] ^= 0x01
        replacement = rs.encode_sector([bytes(mutated)], ix, iz, is_global=False)
        sectors = dict(package.sectors)
        sectors[(ix, iz)] = replacement
        resigned = rs.relink_package(package, sectors=sectors)
        rs.parse_manifest(resigned.manifest)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.assemble_window(resigned, (0.0, 32.0, 0.0, 16.0))
        self.assertIn("replica mismatch", str(caught.exception))

    def test_sector_unsorted_rejected(self):
        builder = GlbBuilder()
        positions = [
            (1, 0, 1), (1.2, 0, 1), (1, 0.2, 1.2),
            (1.4, 0, 1), (1.6, 0, 1), (1.4, 0.2, 1.2),
        ]
        primitive = {
            "attributes": {"POSITION": builder.add_vec3(positions)},
            "indices": builder.add_indices(list(range(6))),
        }
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        package = _package(builder.to_bytes())
        blob = next(iter(package.sectors.values()))
        parsed = rs.parse_sector(blob)
        self.assertEqual([item["source_id"] for item in parsed["records"]], [0, 1])
        swapped = parsed["records"][1]["raw"] + parsed["records"][0]["raw"]
        compressed = zlib.compress(swapped, 9)
        header = rs.SECTOR_HEADER_STRUCT.pack(
            rs.MAGIC_SECTOR, rs.FORMAT_VERSION, 0, parsed["ix"], parsed["iz"], 2, len(swapped), len(compressed)
        )
        body = header + compressed
        bad = body + struct.pack("<II", rs.crc32(swapped), rs.crc32(compressed))
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_sector(bad)
        self.assertIn("sector order", str(caught.exception))

    def test_truncated_crc_and_exact_eof(self):
        package = _package(_interior().to_bytes())
        manifest = package.manifest
        sector = next(iter(package.sectors.values()))
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_manifest(manifest[:20])
        self.assertIn("truncated manifest", str(caught.exception))
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_manifest(manifest[:-1])
        self.assertIn("truncated manifest", str(caught.exception))
        flipped = bytearray(manifest)
        flipped[HEADER_SAFE] ^= 0xFF
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_manifest(bytes(flipped))
        self.assertIn("manifest crc mismatch", str(caught.exception))
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_manifest(manifest + b"\x00")
        self.assertIn("malformed manifest", str(caught.exception))
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_sector(sector[:-1])
        self.assertIn("truncated sector", str(caught.exception))
        bad_sector = bytearray(sector)
        bad_sector[40] ^= 0xFF
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_sector(bytes(bad_sector))
        self.assertIn("sector crc mismatch", str(caught.exception))
        with self.assertRaises(rs.RenderSectorError):
            rs.parse_sector(sector + b"\x00")

        broken_flags = bytearray(sector)
        broken_flags[8:12] = struct.pack("<I", 3)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_sector(bytes(broken_flags))
        self.assertIn("malformed sector", str(caught.exception))
        key = next(iter(package.sectors))
        flagged = bytearray(sector)
        flagged[8:12] = struct.pack("<I", rs.SECTOR_FLAG_GLOBAL)
        package.sectors[key] = bytes(flagged)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.assemble_window(package, (0.0, 16.0, 0.0, 16.0))
        self.assertIn("sector crc mismatch", str(caught.exception))

    def test_resigned_limits_still_rejected(self):
        package = _package(_interior().to_bytes())
        blob = bytearray(package.manifest)

        def resign(buffer):
            body = bytes(buffer[:-4])
            return body + struct.pack("<I", rs.crc32(body))

        blob[rs.HEADER_OFFSETS["per_sector_cap"]:rs.HEADER_OFFSETS["per_sector_cap"] + 4] = struct.pack("<I", 100)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_manifest(resign(blob))
        self.assertIn("manifest contract mismatch", str(caught.exception))

        over = bytearray(package.manifest)
        over[rs.HEADER_OFFSETS["package_bytes"]:rs.HEADER_OFFSETS["package_bytes"] + 4] = struct.pack("<I", rs.MAX_PACKAGE_BYTES + 1)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_manifest(resign(over))
        self.assertIn("package budget exceeded", str(caught.exception))

        span = bytearray(package.manifest)
        span[rs.HEADER_OFFSETS["max_axis_span"]:rs.HEADER_OFFSETS["max_axis_span"] + 4] = struct.pack("<f", 32.0)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_manifest(resign(span))
        self.assertIn("manifest contract mismatch", str(caught.exception))

        for name, value in (
            ("face_stride", 80),
            ("vertex_stride", 24),
            ("material_stride", 16),
            ("normal_encoding", 0),
        ):
            edited = bytearray(package.manifest)
            edited[rs.HEADER_OFFSETS[name]:rs.HEADER_OFFSETS[name] + 4] = struct.pack("<I", value)
            with self.assertRaises(rs.RenderSectorError) as caught:
                rs.parse_manifest(resign(edited))
            self.assertIn("manifest contract mismatch", str(caught.exception))

        reserved = bytearray(package.manifest)
        reserved[rs.HEADER_OFFSETS["reserved_2"]:rs.HEADER_OFFSETS["reserved_2"] + 4] = struct.pack("<I", 1)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_manifest(resign(reserved))
        self.assertIn("malformed manifest", str(caught.exception))

    def test_real_sector_and_global_caps(self):
        def batch(count, x, y0, y1):
            builder = GlbBuilder()
            positions = []
            for _ in range(count):
                positions.extend([(x, y0, 1.0), (x + 0.01, y0, 1.0), (x, y1, 1.2)])
            primitive = {
                "attributes": {"POSITION": builder.add_vec3(positions)},
                "indices": builder.add_indices(list(range(len(positions)))),
            }
            builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
            return builder.to_bytes()

        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(batch(4097, 1.0, 0.0, 0.2))
        self.assertIn("sector face cap exceeded", str(caught.exception))
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(batch(4097, 1.0, 0.0, 100.0))
        self.assertIn("global cap exceeded", str(caught.exception))

    def test_material_and_texture_cap_boundaries(self):
        def build(material_target):
            builder = GlbBuilder()
            wraps = [(10497, 10497), (33071, 10497), (33648, 10497),
                     (10497, 33071), (33071, 33071), (33648, 33071),
                     (10497, 33648), (33071, 33648), (33648, 33648)]
            for index, (wrap_s, wrap_t) in enumerate(wraps):
                builder.samplers.append({"wrapS": wrap_s, "wrapT": wrap_t})
            positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
            attributes = {"POSITION": builder.add_vec3(positions)}
            indices = builder.add_indices([0, 1, 2])
            primitives = []
            remaining = material_target
            image_index = 0
            while remaining:
                builder.images.append({"uri": _png_uri((image_index % 256, 1, 2, 255))})
                take = min(9, remaining)
                for sampler_index in range(take):
                    texture_index = len(builder.textures)
                    builder.textures.append({"source": image_index, "sampler": sampler_index})
                    material_index = len(builder.materials)
                    builder.materials.append({
                        "pbrMetallicRoughness": {"baseColorTexture": {"index": texture_index}},
                        "alphaMode": "OPAQUE",
                    })
                    primitives.append({"attributes": attributes, "indices": indices, "material": material_index})
                remaining -= take
                image_index += 1
            builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh(primitives))]
            return builder.to_bytes()

        package = _package(build(256))
        self.assertEqual(package.summary["material_count"], 256)
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(build(257))
        self.assertIn("material cap exceeded", str(caught.exception))

        def textured(count):
            builder = GlbBuilder()
            positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
            attributes = {"POSITION": builder.add_vec3(positions)}
            indices = builder.add_indices([0, 1, 2])
            primitives = []
            for index in range(count):
                builder.images.append({"uri": _png_uri((index % 256, index % 255, 4, 255))})
                builder.textures.append({"source": index})
                builder.materials.append({
                    "pbrMetallicRoughness": {"baseColorTexture": {"index": index}},
                })
                primitives.append({"attributes": attributes, "indices": indices, "material": index})
            builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh(primitives))]
            return builder.to_bytes()

        package = _package(textured(256))
        self.assertEqual(package.summary["texture_count"], 256)
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(textured(257))
        self.assertIn("texture cap exceeded", str(caught.exception))

    def test_patched_caps_fail_without_truncation(self):
        blob = _one([(15, 0, 8), (17, 0, 8), (16, 1, 8)]).to_bytes()
        with _patched(MAX_TOTAL_REPLICAS=1):
            with self.assertRaises(rs.RenderSectorError) as caught:
                _package(blob)
            self.assertIn("replica cap exceeded", str(caught.exception))
        positions = []
        builder = GlbBuilder()
        for x in (1.0, 20.0):
            positions.extend([(x, 0, 1), (x + 0.2, 0, 1), (x, 0.2, 1.2)])
        primitive = {
            "attributes": {"POSITION": builder.add_vec3(positions)},
            "indices": builder.add_indices(list(range(6))),
        }
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        two_cells = builder.to_bytes()
        with _patched(MAX_SECTORS=1):
            with self.assertRaises(rs.RenderSectorError) as caught:
                _package(two_cells)
            self.assertIn("sector cap exceeded", str(caught.exception))
        three = GlbBuilder()
        positions = []
        for index in range(3):
            x = 1.0 + index
            positions.extend([(x, 0, 1), (x + 0.1, 0, 1), (x, 0.1, 1.1)])
        primitive = {
            "attributes": {"POSITION": three.add_vec3(positions)},
            "indices": three.add_indices(list(range(9))),
        }
        three.scenes[0]["nodes"] = [three.add_node(mesh=three.add_mesh([primitive]))]
        with _patched(MAX_SOURCE=2):
            with self.assertRaises(rs.RenderSectorError) as caught:
                _package(three.to_bytes())
            self.assertIn("source cap exceeded", str(caught.exception))
        with _patched(MAX_TEXTURE_BYTES=16):
            with self.assertRaises(rs.RenderSectorError) as caught:
                _package(_interior().to_bytes())
            self.assertIn("texture budget exceeded", str(caught.exception))
        with _patched(MAX_PACKAGE_BYTES=64):
            with self.assertRaises(rs.RenderSectorError) as caught:
                _package(_interior().to_bytes())
            self.assertIn("package budget exceeded", str(caught.exception))

    def test_blend_material_keeps_four_bit_alpha(self):
        builder = GlbBuilder()
        builder.images = [{"uri": _png_uri((255, 128, 0, 136))}]
        builder.textures = [{"source": 0}]
        builder.materials.append({"alphaMode": "BLEND", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}})
        positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
        primitive = {"attributes": {"POSITION": builder.add_vec3(positions),
                                    "TEXCOORD_0": builder.add_vec2([(0, 0), (1, 0), (0, 1)])},
                     "indices": builder.add_indices([0, 1, 2]), "material": 0}
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        header = rs.parse_manifest(_package(builder.to_bytes()).manifest)
        material = header["materials"][0]
        self.assertEqual(material["base_flags"], rs.FLAG_BLEND)
        self.assertEqual(material["alpha_mode"], 2)
        texture = header["textures"][material["base_texture"]]
        self.assertEqual(texture["format"], rs.TEXTURE_ARGB4444)
        # alpha 136 -> 8, red 255 -> 15, green 128 -> 8, blue 0
        self.assertEqual(set(struct.unpack("<64H", texture["pixels"])), {0x8F80})

    def test_rejected_draws_and_graphs(self):
        positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
        cases = []
        odd = GlbBuilder()
        odd.materials.append({"alphaMode": "FOO"})
        primitive = {"attributes": {"POSITION": odd.add_vec3(positions)}, "indices": odd.add_indices([0, 1, 2]), "material": 0}
        odd.scenes[0]["nodes"] = [odd.add_node(mesh=odd.add_mesh([primitive]))]
        cases.append((odd.to_bytes(), "unsupported alphaMode"))

        points = _one(positions)
        points.meshes[0]["primitives"][0]["mode"] = 0
        cases.append((points.to_bytes(), "unsupported primitive mode"))

        morph = _one(positions)
        morph.meshes[0]["primitives"][0]["targets"] = []
        cases.append((morph.to_bytes(), "morph"))

        skin = _one(positions, node={"skin": 0})
        cases.append((skin.to_bytes(), "skinned"))

        cycle = GlbBuilder()
        mesh = cycle.add_mesh([{"attributes": {"POSITION": cycle.add_vec3(positions)}, "mode": 4}])
        cycle.add_node(mesh=mesh, children=[1])
        cycle.add_node(children=[0])
        cycle.scenes[0]["nodes"] = [0]
        cases.append((cycle.to_bytes(), "cycle"))

        tex = GlbBuilder()
        tex.add_solid_material((255, 255, 255, 255), tex_coord=1)
        primitive = {"attributes": {"POSITION": tex.add_vec3(positions)}, "indices": tex.add_indices([0, 1, 2]), "material": 0}
        tex.scenes[0]["nodes"] = [tex.add_node(mesh=tex.add_mesh([primitive]))]
        cases.append((tex.to_bytes(), "unsupported texture"))

        ext = _one(positions)
        ext.meshes[0]["primitives"][0]["extensions"] = {"KHR_draco_mesh_compression": {}}
        cases.append((ext.to_bytes(), "unsupported extensions"))

        required = _one(positions)
        required.extra["extensionsRequired"] = ["KHR_unknown_required_test"]
        cases.append((required.to_bytes(), "unsupported extensions"))

        joints = _one(positions)
        joints.meshes[0]["primitives"][0]["attributes"]["JOINTS_0"] = 0
        cases.append((joints.to_bytes(), "skinned"))

        projective = _one(positions, node={"matrix": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2]})
        cases.append((projective.to_bytes(), "projective"))

        quat = _one(positions, node={"rotation": [0, 0, 0, 0]})
        cases.append((quat.to_bytes(), "quaternion"))

        for blob, text in cases:
            with self.subTest(text=text):
                with self.assertRaises(rs.RenderSectorError) as caught:
                    _package(blob)
                self.assertIn(text, str(caught.exception))

    def test_bad_indices_uv_world_nonfinite_and_sparse(self):
        positions = [(1, 0, 1), (2, 0, 1), (1, 1, 1)]
        builder = GlbBuilder()
        primitive = {
            "attributes": {"POSITION": builder.add_vec3(positions)},
            "indices": builder.add_indices([0, 1, 9]),
        }
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(builder.to_bytes())
        self.assertIn("invalid triangle indices", str(caught.exception))

        short = GlbBuilder()
        primitive = {"attributes": {"POSITION": short.add_vec3(positions[:2])}, "mode": 4}
        short.scenes[0]["nodes"] = [short.add_node(mesh=short.add_mesh([primitive]))]
        with self.assertRaises(rs.RenderSectorError):
            _package(short.to_bytes())

        uv = _one(positions, uvs=[(0, 0), (70000, 0), (0, 1)])
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(uv.to_bytes())
        self.assertIn("invalid position/UV", str(caught.exception))

        far = _one([(2_000_000.0, 0, 1), (2_000_001.0, 0, 1), (2_000_000.0, 1, 1)])
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(far.to_bytes())
        self.assertIn("coordinate exceeds world bound", str(caught.exception))

        nan = GlbBuilder()
        raw = b"".join(struct.pack("<3f", *point) for point in ((1, 0, 1), (float("nan"), 0, 1), (1, 1, 1)))
        primitive = {"attributes": {"POSITION": nan.add_accessor(nan.add_view(raw), 5126, "VEC3", 3)}, "mode": 4}
        nan.scenes[0]["nodes"] = [nan.add_node(mesh=nan.add_mesh([primitive]))]
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(nan.to_bytes())
        self.assertIn("non-finite", str(caught.exception))

        sparse = _one(positions)
        sparse.accessors[0]["sparse"] = {"count": 0}
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(sparse.to_bytes())
        self.assertIn("sparse", str(caught.exception))

        with self.assertRaises(rs.RenderSectorError):
            rs.parse_glb_bytes(b"glTF")
        truncated = _interior().to_bytes()
        with self.assertRaises(rs.RenderSectorError):
            rs.parse_glb_bytes(truncated[:-8])

    def test_cell_and_texture_size_limits(self):
        blob = _interior().to_bytes()
        for cell in (4, 16, 64, 16.5):
            package = _package(blob, cell_size=cell)
            self.assertEqual(package.cell_size, rs.f32(cell))
        for cell in (3.9, 64.1, True, float("nan")):
            with self.subTest(cell=cell):
                with self.assertRaises(rs.RenderSectorError) as caught:
                    _package(blob, cell_size=cell)
                self.assertIn("cell size", str(caught.exception))
        for size in (8, 16, 32, 64):
            self.assertEqual(_package(blob, texture_size=size).summary["texture_size"], size)
        for size in (7, 24, 128, True, 512):
            with self.subTest(size=size):
                with self.assertRaises(rs.RenderSectorError) as caught:
                    _package(blob, texture_size=size)
                self.assertIn("texture size", str(caught.exception))

    def test_reproducible_bytes(self):
        blob = _one(
            [(15, 0, 8), (17, 1, 8), (16, 0, 9)],
            uvs=[(0.1, 0.2), (0.3, 0.4), (0.5, 0.6)],
            colors=[(0.2, 0.4, 0.6, 1), (1, 1, 1, 1), (0, 0, 0, 1)],
        ).to_bytes()
        sha = hashlib.sha256(blob).digest()
        doc, binary = _parse(blob)
        first = rs.build_package(doc, binary, source_sha256=sha)
        second = rs.build_package(doc, binary, source_sha256=sha)
        self.assertEqual(first.manifest, second.manifest)
        self.assertEqual(first.global_sector, second.global_sector)
        self.assertEqual(first.sectors, second.sectors)
        self.assertEqual(first.report_bytes, second.report_bytes)
        self.assertNotIn(b"timestamp", first.report_bytes.lower())

    def test_prepare_sha_and_atomic_output(self):
        blob = _interior().to_bytes()
        with TemporaryDirectory() as temporary:
            parent = Path(temporary)
            marker = "PATHMARKER_9f3a"
            marked = parent / marker
            marked.mkdir()
            glb_path = marked / "scene.glb"
            glb_path.write_bytes(blob)
            dest = parent / "package"
            summary = rs.prepare(glb_path, dest)
            self.assertEqual(bytes.fromhex(summary["source_sha256"]), hashlib.sha256(blob).digest())
            self.assertNotEqual(hashlib.sha256(blob).digest(), hashlib.sha256(_parse(blob)[1]).digest())
            manifest = (dest / "manifest.drm").read_bytes()
            report = (dest / "report.json").read_bytes()
            self.assertNotIn(marker.encode("ascii"), manifest)
            self.assertNotIn(marker.encode("ascii"), report)
            self.assertNotIn(marker.encode("ascii"), (dest / "global.drs").read_bytes())
            self.assertEqual(json.loads(report.decode("utf-8")), summary)
            self.assertTrue(summary["experimental"])
            self.assertFalse(summary["host_budget_is_native_fit"])
            native = 0
            for relative in summary["native_files"]:
                file_path = dest.joinpath(*Path(relative).parts)
                self.assertTrue(file_path.is_file())
                native += file_path.stat().st_size
            self.assertEqual(native, summary["package_bytes"])
            found = sorted(path.relative_to(dest).as_posix() for path in dest.rglob("*") if path.is_file())
            self.assertEqual(found, sorted(summary["native_files"] + summary["host_files"]))
            rs.parse_manifest(manifest)
            self.assertFalse((dest / "manifest.drm.partial").exists())
            loaded = json.loads(report.decode("utf-8"))
            self.assertNotIn("output_dir", loaded)
            self.assertNotIn(str(glb_path), report.decode("utf-8"))

            sentinel_dir = parent / "kept"
            sentinel_dir.mkdir()
            (sentinel_dir / "keep.txt").write_text("keep", encoding="utf-8")
            with self.assertRaises(rs.RenderSectorError) as caught:
                rs.prepare(glb_path, sentinel_dir)
            self.assertIn("already exists", str(caught.exception))
            self.assertEqual((sentinel_dir / "keep.txt").read_text(encoding="utf-8"), "keep")
            self.assertFalse((sentinel_dir / "manifest.drm").exists())

            sentinel_file = parent / "file.out"
            sentinel_file.write_text("file", encoding="utf-8")
            with self.assertRaises(rs.RenderSectorError):
                rs.prepare(glb_path, sentinel_file)
            self.assertEqual(sentinel_file.read_text(encoding="utf-8"), "file")

            aborted = parent / "aborted"
            with self.assertRaises(rs.RenderSectorError) as caught:
                rs.prepare(glb_path, aborted, fail_after=0)
            self.assertIn("write aborted before manifest commit", str(caught.exception))
            self.assertFalse(aborted.exists())
            self.assertFalse((parent / ".aborted.drm-tmp").exists())

            temp = parent / ".reserved.drm-tmp"
            temp.mkdir()
            (temp / "sentinel.txt").write_text("mine", encoding="utf-8")
            with self.assertRaises(rs.RenderSectorError) as caught:
                rs.prepare(glb_path, parent / "reserved")
            self.assertIn("temp output already exists", str(caught.exception))
            self.assertEqual((temp / "sentinel.txt").read_text(encoding="utf-8"), "mine")
            self.assertFalse((parent / "reserved").exists())

            outside = parent.parent
            with self.assertRaises(rs.RenderSectorError):
                rs.assert_contained(parent, parent)
            with self.assertRaises(rs.RenderSectorError):
                rs.assert_contained(outside, parent)
            self.assertTrue(rs._is_within(dest, parent))

            code = rs.main([str(glb_path), str(parent / "from-cli")])
            self.assertEqual(code, 0)
            self.assertTrue((parent / "from-cli" / "manifest.drm").is_file())
            missing = rs.main([str(parent / "missing.glb"), str(parent / "nope")])
            self.assertEqual(missing, 1)
            self.assertFalse((parent / "nope").exists())

    def test_cap_failure_writes_nothing(self):
        builder = GlbBuilder()
        positions = []
        for _ in range(4097):
            positions.extend([(1.0, 0.0, 1.0), (1.01, 0.0, 1.0), (1.0, 0.01, 1.2)])
        primitive = {
            "attributes": {"POSITION": builder.add_vec3(positions)},
            "indices": builder.add_indices(list(range(len(positions)))),
        }
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        blob = builder.to_bytes()
        with TemporaryDirectory() as temporary:
            parent = Path(temporary)
            glb_path = parent / "over.glb"
            glb_path.write_bytes(blob)
            dest = parent / "out"
            with self.assertRaises(rs.RenderSectorError):
                rs.prepare(glb_path, dest)
            self.assertFalse(dest.exists())
            self.assertFalse(any(path.name.endswith(".drm-tmp") for path in parent.iterdir()))

    def test_empty_global_parses_and_json_is_optional(self):
        package = _package(_interior().to_bytes())
        parsed = rs.parse_sector(package.global_sector)
        self.assertEqual(parsed["record_count"], 0)
        self.assertEqual(parsed["records"], [])
        window = rs.assemble_window(package, (0.0, 16.0, 0.0, 16.0))
        self.assertEqual(len(window), 1)
        header = rs.parse_manifest(package.manifest)
        self.assertEqual(header["global_count"], 0)
        self.assertNotIn(b"{", package.manifest[:4])

    def test_reflective_materials_keep_distinct_maps(self):
        builder = GlbBuilder()
        builder.extra["extensionsUsed"] = ["KHR_materials_specular"]
        builder.images.append({"uri": _png_uri((255, 0, 0, 255))})
        builder.images.append({"uri": _png_uri((255, 0, 180, 255))})
        builder.images.append({"uri": _png_uri((0, 255, 0, 1))})
        builder.samplers.append({"wrapS": 33071})
        builder.samplers.append({"wrapT": 33648})
        builder.samplers.append({"wrapS": 33648})
        builder.textures.append({"source": 0, "sampler": 0})
        builder.textures.append({"source": 1, "sampler": 1})
        builder.textures.append({"source": 2, "sampler": 2})
        builder.textures.append({"source": 2})
        spec = {"specularColorTexture": {"index": 2, "texCoord": 0}}
        builder.materials.append({
            "alphaMode": "OPAQUE",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "metallicRoughnessTexture": {"index": 1, "texCoord": 0},
            },
            "extensions": {"KHR_materials_specular": spec},
        })
        builder.materials.append({
            "alphaMode": "OPAQUE",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "metallicRoughnessTexture": {"index": 1, "texCoord": 0},
                "metallicFactor": 0,
            },
            "extensions": {"KHR_materials_specular": spec},
        })
        builder.materials.append({
            "alphaMode": "OPAQUE",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "metallicRoughnessTexture": {"index": 1, "texCoord": 0},
                "roughnessFactor": 1,
            },
            "extensions": {
                "KHR_materials_specular": {
                    "specularColorTexture": {"index": 2},
                    "specularFactor": 0.25,
                    "specularColorFactor": [0.25, 0.5, 0.75],
                }
            },
        })
        builder.materials.append({
            "alphaMode": "OPAQUE",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "metallicRoughnessTexture": {"index": 1},
            },
            "extensions": {"KHR_materials_specular": {"specularColorTexture": {"index": 3}}},
        })
        builder.materials.append({
            "alphaMode": "OPAQUE",
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0},
        })
        positions = builder.add_vec3([(1.0, 0.0, 1.0), (2.0, 0.0, 1.0), (1.0, 1.0, 1.0)])
        indices = builder.add_indices([0, 1, 2])
        primitives = [
            {"attributes": {"POSITION": positions}, "indices": indices, "material": index, "mode": 4}
            for index in range(5)
        ]
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh(primitives))]
        header = rs.parse_manifest(_package(builder.to_bytes()).manifest)
        self.assertEqual(header["texture_count"], 3)
        self.assertEqual(header["material_count"], 5)
        self.assertEqual([item["format"] for item in header["textures"]], [
            rs.TEXTURE_ARGB1555, rs.TEXTURE_STRENGTH_U8, rs.TEXTURE_ARGB1555,
        ])
        self.assertEqual(set(struct.unpack("<64H", header["textures"][0]["pixels"])), {0xFC00})
        self.assertEqual(header["textures"][1]["pixel_bytes"], 64)
        self.assertEqual(set(header["textures"][1]["pixels"]), {180})
        self.assertEqual(set(struct.unpack("<64H", header["textures"][2]["pixels"])), {0x83E0})
        first, metal, factored, wrapped, plain = header["materials"]
        self.assertEqual(first["base_texture"], 0)
        self.assertEqual(first["base_flags"], rs.FLAG_CLAMP_S)
        self.assertEqual(first["strength_texture"], 1)
        self.assertEqual(first["strength_flags"], rs.FLAG_MIRROR_T)
        self.assertEqual(first["spec_color_texture"], 2)
        self.assertEqual(first["spec_flags"], rs.FLAG_MIRROR_S)
        self.assertEqual(first["metallic_factor"], 1.0)
        self.assertEqual(first["roughness_factor"], 1.0)
        self.assertEqual(first["specular_factor"], 1.0)
        self.assertEqual(first["specular_color_factor"], (1.0, 1.0, 1.0))
        self.assertEqual(metal["metallic_factor"], 0.0)
        self.assertEqual(metal["strength_texture"], 1)
        self.assertEqual(metal["spec_color_texture"], 2)
        self.assertEqual(factored["specular_factor"], rs.f32(0.25))
        self.assertEqual(factored["specular_color_factor"], (rs.f32(0.25), rs.f32(0.5), rs.f32(0.75)))
        self.assertEqual(wrapped["spec_color_texture"], 2)
        self.assertEqual(wrapped["spec_flags"], 0)
        self.assertNotEqual(first["spec_flags"], wrapped["spec_flags"])
        self.assertEqual(plain["metallic_factor"], 0.0)
        self.assertEqual(plain["strength_texture"], rs.ABSENT_TEXTURE)
        self.assertEqual(plain["spec_color_texture"], rs.ABSENT_TEXTURE)
        self.assertEqual(plain["specular_factor"], 1.0)
        self.assertEqual(plain["base_flags"] & rs.FLAG_UNLIT, 0)

    def test_near_cutoff_same_f32_splits_base_textures(self):
        bits = struct.unpack("<I", struct.pack("<f", 0.4))[0]
        lo = struct.unpack("<f", struct.pack("<I", bits))[0]
        hi = struct.unpack("<f", struct.pack("<I", bits + 1))[0]
        near = lo + (hi - lo) * 0.25
        self.assertNotEqual(near, lo)
        self.assertEqual(struct.pack("<f", near), struct.pack("<f", lo))
        builder = GlbBuilder()
        builder.images.append({"uri": _png_uri((255, 255, 255, 255))})
        builder.textures.append({"source": 0})
        builder.materials.append({
            "alphaMode": "MASK",
            "alphaCutoff": lo,
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}},
        })
        builder.materials.append({
            "alphaMode": "MASK",
            "alphaCutoff": near,
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}},
        })
        attributes = {"POSITION": builder.add_vec3([(1, 0, 1), (2, 0, 1), (1, 1, 1)])}
        indices = builder.add_indices([0, 1, 2])
        primitives = [
            {"attributes": attributes, "indices": indices, "material": 0},
            {"attributes": attributes, "indices": indices, "material": 1},
        ]
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh(primitives))]
        header = rs.parse_manifest(_package(builder.to_bytes()).manifest)
        self.assertEqual(header["texture_count"], 2)
        self.assertEqual(header["material_count"], 2)
        self.assertEqual(header["materials"][0]["alpha_cutoff"], header["materials"][1]["alpha_cutoff"])

        baked = GlbBuilder()
        baked.images.append({"uri": _png_uri((255, 255, 255, 128))})
        baked.textures.append({"source": 0})
        baked.materials.append({
            "alphaMode": "MASK",
            "alphaCutoff": 0.2,
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}},
        })
        baked.materials.append({
            "alphaMode": "MASK",
            "alphaCutoff": 0.8,
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}},
        })
        attributes = {"POSITION": baked.add_vec3([(1, 0, 1), (2, 0, 1), (1, 1, 1)])}
        indices = baked.add_indices([0, 1, 2])
        baked.scenes[0]["nodes"] = [baked.add_node(mesh=baked.add_mesh([
            {"attributes": attributes, "indices": indices, "material": 0},
            {"attributes": attributes, "indices": indices, "material": 1},
        ]))]
        header = rs.parse_manifest(_package(baked.to_bytes()).manifest)
        self.assertEqual(set(struct.unpack("<64H", header["textures"][0]["pixels"])), {0xFFFF})
        self.assertEqual(set(struct.unpack("<64H", header["textures"][1]["pixels"])), {0x7FFF})

    def test_normals_authored_flat_and_degenerate_rules(self):
        length = (0.5 * 0.5 + 1.0) ** 0.5
        expected = (rs.f32(0.5 / length), rs.f32(1.0 / length), rs.f32(0.0))
        scaled = _one(
            [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)],
            normals=[(1.0, 1.0, 0.0)] * 3,
            node={"translation": [5, 0, 0], "scale": [2, 1, 1]},
        )
        package = _package(scaled.to_bytes())
        face = _face_positions(package)[0]
        self.assertEqual(face["vertices"][0][:3], (5.0, 0.0, 0.0))
        self.assertEqual(face["vertices"][1][:3], (7.0, 0.0, 0.0))
        self.assertEqual([vertex[5:8] for vertex in face["vertices"]], [expected, expected, expected])
        self.assertNotEqual(face["vertices"][0][5:8], (0.0, 0.0, 1.0))

        half = 2.0 ** 0.5 / 2.0
        rotated = _one(
            [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)],
            normals=[(1.0, 0.0, 0.0)] * 3,
            node={"rotation": [0.0, half, 0.0, half]},
        )
        package = _package(rotated.to_bytes())
        face = _face_positions(package)[0]
        # Quaternion normalization leaves roundoff near zero. Test geometry
        # within 1e-7, while preserving the exact source result in the package.
        for actual, expected in zip(face["vertices"][1][:3], (0.0, 0.0, -1.0)):
            self.assertAlmostEqual(actual, expected, delta=1e-7)
        for actual, expected in zip(face["vertices"][0][5:8], (0.0, 0.0, -1.0)):
            self.assertAlmostEqual(actual, expected, delta=1e-7)

        flat = _one(
            [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 1.0)],
            node={"rotation": [0.0, 1.0, 0.0, 0.0]},
        )
        package = _package(flat.to_bytes())
        unit = rs.f32(1.0 / (2.0 ** 0.5))
        face = _face_positions(package)[0]
        self.assertEqual(face["vertices"][1][:3], (-1.0, 0.0, 0.0))
        self.assertEqual(face["vertices"][2][:3], (0.0, 1.0, -1.0))
        self.assertEqual(face["vertices"][0][5:8], (0.0, -unit, -unit))
        self.assertEqual(package.flat_normal_faces, 1)
        self.assertEqual(package.normal_transform_count, 0)

        singular = _one(
            [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)],
            normals=[(0.0, 1.0, 0.0)] * 3,
            node={"scale": [1, 0, 1]},
        )
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(singular.to_bytes())
        self.assertIn("singular matrix", str(caught.exception))

        cached = GlbBuilder()
        positions = cached.add_vec3([(0, 0, 0), (1, 0, 0), (0, 1, 0)])
        normals = cached.add_vec3([(0, 0, 1), (0, 0, 1), (0, 0, 1)])
        indices = cached.add_indices([0, 1, 2])
        primitive = {"attributes": {"POSITION": positions, "NORMAL": normals}, "indices": indices, "mode": 4}
        cached.scenes[0]["nodes"] = [cached.add_node(mesh=cached.add_mesh([primitive, primitive]))]
        package = _package(cached.to_bytes())
        self.assertEqual(package.normal_decode_count, 1)
        self.assertEqual(package.normal_transform_count, 1)
        self.assertEqual(package.position_decode_count, 1)
        self.assertEqual(package.accessor_decode_count, 3)
        self.assertEqual(package.authored_normal_faces, 2)
        self.assertEqual(_face_positions(package)[0]["vertices"][0][5:8], (0.0, 0.0, 1.0))

    def test_unsupported_material_inputs_fail(self):
        cases = []
        cases.append((_solo({
            "extensions": {"KHR_materials_specular": {"specularTexture": {"index": 0}}},
        }).to_bytes(), "specularTexture"))
        cases.append((_solo({"normalTexture": {"index": 0}}).to_bytes(), "normalTexture"))
        cases.append((_solo({"occlusionTexture": {"index": 0}}).to_bytes(), "occlusionTexture"))
        cases.append((_solo({
            "extensions": {
                "KHR_materials_unlit": {},
                "KHR_materials_specular": {"specularFactor": 1},
            },
        }).to_bytes(), "unlit"))
        cases.append((_solo({
            "extensions": {"KHR_materials_clearcoat": {}},
        }).to_bytes(), "unsupported extensions"))
        cases.append((_solo({
            "extensions": {"KHR_materials_specular": {"specularFactor": 1, "shininess": 4}},
        }).to_bytes(), "unsupported extensions"))
        cases.append((_solo(
            {"extensions": {"KHR_materials_specular": {"specularColorTexture": {"index": 0, "texCoord": 1}}}},
            images=[(0, 255, 0, 255)],
            textures=[{"source": 0}],
        ).to_bytes(), "unsupported texture"))
        cases.append((_solo({
            "pbrMetallicRoughness": {"metallicFactor": 1.5},
        }).to_bytes(), "invalid metallic factor"))
        cases.append((_solo(
            {"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}},
            textures=[{"source": 4}],
        ).to_bytes(), "image index out of range"))
        for blob, text in cases:
            with self.subTest(text=text):
                with self.assertRaises(rs.RenderSectorError) as caught:
                    _package(blob)
                self.assertIn(text, str(caught.exception))

        for color in ((254, 0, 180, 255), (255, 1, 180, 255), (255, 0, 180, 254)):
            with self.subTest(color=color):
                blob = _solo(
                    {"pbrMetallicRoughness": {"metallicRoughnessTexture": {"index": 0}}},
                    images=[color],
                    textures=[{"source": 0}],
                ).to_bytes()
                with self.assertRaises(rs.RenderSectorError) as caught:
                    _package(blob)
                self.assertIn("unexpected metalRough channels", str(caught.exception))

        plain = _solo({
            "extensions": {"KHR_materials_unlit": {}},
            "pbrMetallicRoughness": {"metallicFactor": 0},
        }).to_bytes()
        header = rs.parse_manifest(_package(plain).manifest)
        self.assertEqual(header["materials"][0]["base_flags"] & rs.FLAG_UNLIT, rs.FLAG_UNLIT)
        self.assertEqual(header["materials"][0]["metallic_factor"], 0.0)
        self.assertEqual(header["materials"][0]["spec_color_texture"], rs.ABSENT_TEXTURE)

    def test_invalid_normal_bytes_rejected(self):
        package = _package(_interior().to_bytes())
        blob = next(iter(package.sectors.values()))
        parsed = rs.parse_sector(blob)

        def repack(raw):
            compressed = zlib.compress(raw, 9)
            header = rs.SECTOR_HEADER_STRUCT.pack(
                rs.MAGIC_SECTOR,
                rs.FORMAT_VERSION,
                0,
                parsed["ix"],
                parsed["iz"],
                1,
                len(raw),
                len(compressed),
            )
            body = header + compressed
            return body + struct.pack("<II", rs.crc32(raw), rs.crc32(compressed))

        nan = bytearray(parsed["records"][0]["raw"])
        struct.pack_into("<f", nan, 28, float("nan"))
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_sector(repack(bytes(nan)))
        self.assertIn("invalid normal", str(caught.exception))
        stretched = bytearray(parsed["records"][0]["raw"])
        struct.pack_into("<fff", stretched, 28, 2.0, 0.0, 0.0)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_sector(repack(bytes(stretched)))
        self.assertIn("invalid normal", str(caught.exception))

    def test_truncated_deflate_and_compression_bomb(self):
        package = _package(_interior().to_bytes())
        sector = next(iter(package.sectors.values()))
        parsed = rs.parse_sector(sector)
        compressed = sector[32:32 + parsed["compressed_bytes"]]
        truncated = compressed[:-1]
        header = rs.SECTOR_HEADER_STRUCT.pack(
            rs.MAGIC_SECTOR,
            rs.FORMAT_VERSION,
            0,
            parsed["ix"],
            parsed["iz"],
            parsed["record_count"],
            parsed["uncompressed_bytes"],
            len(truncated),
        )
        blob = header + truncated + struct.pack("<II", parsed["payload_crc"], rs.crc32(truncated))
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_sector(blob)
        self.assertIn("malformed sector", str(caught.exception))

        bomb = zlib.compress(b"\x00" * 8192, 9)
        self.assertLessEqual(len(bomb), rs.COMPRESS_SLACK)

        def bomb_sector(count, uncompressed):
            header = rs.SECTOR_HEADER_STRUCT.pack(
                rs.MAGIC_SECTOR, rs.FORMAT_VERSION, rs.SECTOR_FLAG_GLOBAL, 0, 0, count, uncompressed, len(bomb)
            )
            return header + bomb + struct.pack("<II", 0, rs.crc32(bomb))

        for count, uncompressed in ((0, 0), (1, rs.FACE_BYTES)):
            with self.subTest(expected=uncompressed):
                with self.assertRaises(rs.RenderSectorError) as caught:
                    rs.parse_sector(bomb_sector(count, uncompressed))
                self.assertIn("malformed sector", str(caught.exception))

    def test_window_filters_overfetch_before_cap(self):
        builder = GlbBuilder()
        positions = []
        for _ in range(4096):
            positions.extend([(1.0, 0.0, 1.0), (1.01, 0.0, 1.0), (1.0, 0.2, 1.2)])
        for _ in range(4096):
            positions.extend([(1.0, 0.0, 24.0), (1.01, 0.0, 24.0), (1.0, 0.2, 24.2)])
        positions.extend([(5000.0, 0.0, 1.0), (5100.0, 0.0, 1.0), (5000.0, 1.0, 2.0)])
        primitive = {
            "attributes": {"POSITION": builder.add_vec3(positions)},
            "indices": builder.add_indices(list(range(len(positions)))),
        }
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        package = _package(builder.to_bytes())
        self.assertEqual(package.source_count, 8193)
        self.assertEqual(package.summary["replica_count"], 8192)
        self.assertEqual(package.summary["global_count"], 1)
        narrow = rs.assemble_window(package, (0.0, 16.0, 0.0, 16.0))
        self.assertEqual(len(narrow), 4096)
        self.assertEqual(narrow[0]["source_id"], 0)
        self.assertEqual(narrow[-1]["source_id"], 4095)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.assemble_window(package, (0.0, 6000.0, 0.0, 32.0))
        self.assertIn("unique faces exceed window cap", str(caught.exception))

    def test_filtered_mismatch_still_rejected(self):
        positions = [(15.0, 0.0, 1.0), (17.0, 0.0, 1.0), (16.0, 0.2, 1.2)]
        package = _package(_one(positions).to_bytes())
        self.assertEqual(set(package.sectors), {(0, 0), (1, 0)})
        (ix, iz), blob = sorted(package.sectors.items())[0]
        parsed = rs.parse_sector(blob)
        self.assertLess(max(vertex[2] for vertex in parsed["records"][0]["vertices"]), 10.0)
        mutated = bytearray(parsed["records"][0]["raw"])
        mutated[8] ^= 0x01
        sectors = dict(package.sectors)
        sectors[(ix, iz)] = rs.encode_sector([bytes(mutated)], ix, iz, is_global=False)
        resigned = rs.relink_package(package, sectors=sectors)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.assemble_window(resigned, (0.0, 20.0, 10.0, 16.0))
        self.assertIn("replica mismatch", str(caught.exception))


HEADER_SAFE = rs.HEADER_OFFSETS["bounds_min_x"]


class IndependentMaterialCases(unittest.TestCase):
    """Codex review regressions from independent/source-data findings."""
    def test_source_strength_defect_cannot_disappear_in_downsample(self):
        image = Image.new('RGBA', (64, 64), (255, 0, 180, 255))
        image.putpixel((31, 31), (254, 0, 180, 255))
        # Pillow rounds this isolated R defect back to 255 at 8x8. Validating
        # resized pixels alone therefore accepts a source channel it discards.
        self.assertEqual(image.resize((8, 8), Image.Resampling.LANCZOS).getchannel('R').getextrema(), (255, 255))
        raw = BytesIO(); image.save(raw, format='PNG')
        builder = _solo({'pbrMetallicRoughness': {'metallicRoughnessTexture': {'index': 0}}},
                        textures=[{'source': 0}])
        builder.images.append({'uri': 'data:image/png;base64,' + base64.b64encode(raw.getvalue()).decode('ascii')})
        with self.assertRaisesRegex(rs.RenderSectorError, 'metalRough channels'):
            _package(builder.to_bytes(), texture_size=8)

    def test_opaque_rgb_is_independent_of_ignored_alpha(self):
        image = Image.new('RGBA', (64, 64), (255, 0, 0, 0))
        raw = BytesIO(); image.save(raw, format='PNG')
        uri = 'data:image/png;base64,' + base64.b64encode(raw.getvalue()).decode('ascii')
        builder = _solo({'extensions': {'KHR_materials_specular': {'specularColorTexture': {'index': 0}}}},
                        textures=[{'source': 0}])
        builder.images.append({'uri': uri})
        header = rs.parse_manifest(_package(builder.to_bytes(), texture_size=8).manifest)
        material = header['materials'][0]
        pixels = header['textures'][material['spec_color_texture']]['pixels']
        self.assertEqual(set(struct.unpack('<64H', pixels)), {0xFC00})

    def test_double_sided_and_per_role_filter_preserved(self):
        b = _solo({'doubleSided': True,
                   'pbrMetallicRoughness': {'baseColorTexture': {'index': 0},
                                          'metallicRoughnessTexture': {'index': 1}},
                   'extensions': {'KHR_materials_specular': {'specularColorTexture': {'index': 2}}}},
                  images=[(255, 0, 0, 255), (255, 0, 90, 255), (0, 255, 0, 255)],
                  textures=[{'source': 0, 'sampler': 0}, {'source': 1, 'sampler': 0}, {'source': 2}],
                  samplers=[{'minFilter': 9728, 'magFilter': 9728}])
        material = rs.parse_manifest(_package(b.to_bytes()).manifest)['materials'][0]
        self.assertEqual(material['base_flags'], rs.FLAG_DOUBLE_SIDED | rs.FLAG_NEAREST)
        self.assertEqual(material['strength_flags'], rs.FLAG_NEAREST)
        self.assertEqual(material['spec_flags'], 0)
        for mag, minimum in ((9728, 9729), (9729, 9987), (1, 1)):
            b.samplers[0] = {'magFilter': mag, 'minFilter': minimum}
            with self.subTest(mag=mag, minimum=minimum):
                with self.assertRaisesRegex(rs.RenderSectorError, 'sampler filter'):
                    _package(b.to_bytes())

    def test_invalid_accessor_and_image_view_indices_fail(self):
        b = _interior(); b.accessors[0]['bufferView'] = -1
        with self.assertRaisesRegex(rs.RenderSectorError, 'view index'):
            _package(b.to_bytes())
        b = _solo({'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}}}, textures=[{'source': 0}])
        b.images.append({'bufferView': -1})
        with self.assertRaisesRegex(rs.RenderSectorError, 'image buffer view'):
            _package(b.to_bytes())

    def test_impossible_source_and_replica_counts_fail(self):
        p = _package(_interior().to_bytes())
        for name, value in (('source_count', 2), ('global_count', 2)):
            raw = bytearray(p.manifest)
            struct.pack_into('<I', raw, rs.HEADER_OFFSETS[name], value)
            struct.pack_into('<I', raw, len(raw)-4, rs.crc32(raw[:-4]))
            with self.subTest(name=name):
                with self.assertRaises(rs.RenderSectorError):
                    rs.parse_manifest(raw)


if __name__ == "__main__":
    unittest.main()
