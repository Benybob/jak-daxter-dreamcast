"""Measure GLB texture residency without decoding pixels (AI-assisted), ISC.

Run from ports/dreamcast: python texture_budget.py GLB [--json OUT], or
python texture_budget.py --all LEVELS_DIR --json build/texture-budget.json.
Only embedded PNG/JPEG dimensions are read; no game pixels or packages are
written. These are exact dimension-based payload costs, not encoded textures,
quality measurements, or proof that a complete level can load or render.

Deduplication follows prepare_render_sectors._material_for: base image/mode/
cutoff, separate specular image and strength image; sampler and vertex factors
do not duplicate pixels. Untextured materials allocate its shared 8x8 white
fallback. Only active-scene mesh materials are counted, including all LOD roots.
Strength U8 is CPU-only (render_gpu.c). Color maps include resident specular
maps even though render_base.c currently draws only base maps.

Raw profiles use twiddled ARGB1555, or ARGB4444 for BLEND, with the converter's
per-axis floor-power-of-two, minimum 8 and cap 64/32 (half the size cap).
No mipmaps: render_base.c uses ordinary bilinear/nearest contexts, and DCT1 VQ
also has no mip chain. RGB565 conversion is not implemented by these tools.
VQ profiles use vq_texture.payload_size for square ARGB1555 only, falling back
to raw for rectangles and BLEND. The full 2048-byte codebook is charged even
when larger than raw. VQ encoding/DCT1 upload exists, but DRM2/render_gpu.c does
not accept VQ: these mixed profiles require integration into the resident path.

VRAM layout follows flythrough_main.c and the pinned KOS pvr_buffers.c:
two sets, each aligned to 128 after vertex, OPB, tile matrix and framebuffer.
640x480 RGB565; 20x15 tiles; three 32-word lists; eight extra OPB sets;
2 MiB vertex buffer per set. This leaves 877312 bytes out of 8 MiB. Fit means
payload-only fit in that arena, before allocator metadata/fragmentation and
other textures; the report also includes SH-4 dlmalloc chunk estimates
(request2size: align32(request + 4)), excluding arena setup/top-chunk costs.
No manifest caps are overridden or geometry converted by this measurement.
(AI-assisted), ISC.
"""

from __future__ import annotations

import argparse
import ast
import base64
import json
import math
from pathlib import Path
import re
import struct
import sys

from vq_texture import payload_size


PORT = Path(__file__).resolve().parent
PROFILES = ("raw64", "raw32", "vq64", "vq32")


def align(value, boundary):
    return (value + boundary - 1) // boundary * boundary


def flythrough_budget(width=640, height=480, vertex_bytes=2 * 1024 * 1024,
                      bins=(32, 0, 32, 0, 32), overflow=8):
    """Reproduce pvr_allocate_buffers, including both interleaved VRAM banks."""
    height = align(height, 32)
    tiles = width // 32 * (height // 32)
    opb = sum(bins) * 4 * tiles * (1 + overflow)
    matrix = 4 * (18 + 6 * tiles)
    framebuffer = width * height * 2
    end = 0
    for size in (vertex_bytes, opb, matrix, framebuffer):
        end = align(end + size, 128)
    reserved = 2 * end
    return dict(total_bytes=8 * 1024 * 1024, sets=2, tiles=tiles,
                vertex_bytes_per_set=vertex_bytes, opb_bytes_per_set=opb,
                tile_matrix_bytes_per_set=matrix,
                framebuffer_bytes_per_set=framebuffer,
                alignment_bytes_per_set=end - vertex_bytes - opb - matrix - framebuffer,
                reserved_bytes=reserved, texture_arena_bytes=8 * 1024 * 1024 - reserved)


def _integer(node):
    if isinstance(node, ast.Constant) and type(node.value) is int:
        return node.value
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Mult):
        return _integer(node.left) * _integer(node.right)
    raise ValueError("unrecognized integer expression in texture policy")


def repository_policy():
    """Read constants without importing prepare_render_sectors (requires PIL)."""
    names = {"DEFAULT_TEXTURE_SIZE", "MIN_TEXTURE_SIZE", "MAX_TEXTURES", "MAX_TEXTURE_BYTES"}
    values = {}
    tree = ast.parse((PORT / "prepare_render_sectors.py").read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id in names:
                    values[target.id] = _integer(node.value)
    if set(values) != names or values["DEFAULT_TEXTURE_SIZE"] != 64 or values["MIN_TEXTURE_SIZE"] != 8:
        raise ValueError("texture policy changed; review budget profiles")
    source = (PORT / "flythrough_main.c").read_text(encoding="utf-8")
    compact = re.sub(r"\s+", "", source)
    expected = ("VERTEX_BUFFER=2048*1024", "vid_set_mode(DM_640x480,PM_RGB565)",
                ".opb_sizes={PVR_BINSIZE_32,0,PVR_BINSIZE_32,0,PVR_BINSIZE_32}",
                ".vertex_buf_size=VERTEX_BUFFER", ".opb_overflow_count=8")
    if not all(item in compact for item in expected):
        raise ValueError("flythrough PVR configuration changed; review VRAM model")
    return values


def _index(items, index, label):
    if type(index) is not int or not 0 <= index < len(items):
        raise ValueError(label + " index out of range")
    return items[index]


def read_glb(path):
    """Read JSON and locate BIN, without loading or visiting geometry payloads."""
    with Path(path).open("rb") as stream:
        header = stream.read(12)
        if len(header) != 12:
            raise ValueError("short GLB header")
        magic, version, length = struct.unpack("<4sII", header)
        if magic != b"glTF" or version != 2 or length != Path(path).stat().st_size:
            raise ValueError("invalid GLB header/length")
        doc, binary = None, None
        while stream.tell() < length:
            chunk = stream.read(8)
            if len(chunk) != 8:
                raise ValueError("short GLB chunk header")
            size, kind = struct.unpack("<I4s", chunk)
            start = stream.tell()
            if size % 4 or start + size > length:
                raise ValueError("invalid GLB chunk size")
            if kind == b"JSON":
                if doc is not None or start != 20:
                    raise ValueError("duplicate or misplaced GLB JSON")
                doc = json.loads(stream.read(size))
            elif kind == b"BIN\0":
                if binary is not None:
                    raise ValueError("duplicate GLB BIN")
                binary = (start, size)
            stream.seek(start + size)
    if not isinstance(doc, dict):
        raise ValueError("missing GLB JSON")
    return doc, binary


def image_dimensions(raw):
    """Read PNG IHDR or JPEG SOF metadata; this is not a pixel validator."""
    if raw.startswith(b"\x89PNG\r\n\x1a\n") and len(raw) >= 33:
        if raw[8:16] != b"\0\0\0\rIHDR":
            raise ValueError("invalid PNG IHDR")
        width, height = struct.unpack_from(">II", raw, 16)
    elif raw.startswith(b"\xff\xd8"):
        pos = 2
        while pos < len(raw):
            if raw[pos] != 255:
                raise ValueError("invalid JPEG marker")
            while pos < len(raw) and raw[pos] == 255:
                pos += 1
            if pos >= len(raw):
                raise ValueError("short JPEG marker")
            marker = raw[pos]
            pos += 1
            if marker in (0xD9, 0xDA):
                break
            if marker == 0x01 or 0xD0 <= marker <= 0xD8:
                continue
            if pos + 2 > len(raw):
                raise ValueError("short JPEG segment")
            size = struct.unpack_from(">H", raw, pos)[0]
            if size < 2 or pos + size > len(raw):
                raise ValueError("invalid JPEG segment size")
            if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                          0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
                if size < 8:
                    raise ValueError("short JPEG SOF")
                height, width = struct.unpack_from(">HH", raw, pos + 3)
                break
            pos += size
        else:
            raise ValueError("missing JPEG SOF")
        if marker not in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                          0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            raise ValueError("missing JPEG SOF")
    else:
        raise ValueError("expected embedded PNG or JPEG")
    if not 1 <= width <= 4096 or not 1 <= height <= 4096:
        raise ValueError("source image dimensions out of converter range")
    return width, height


def _image_bytes(path, doc, binary, image):
    if "bufferView" in image:
        if "uri" in image or binary is None:
            raise ValueError("invalid image buffer view")
        view = _index(doc.get("bufferViews", []), image["bufferView"], "bufferView")
        start, size = view.get("byteOffset", 0), view["byteLength"]
        if (view.get("buffer", 0) != 0 or type(start) is not int or type(size) is not int
                or min(start, size) < 0 or start + size > binary[1]):
            raise ValueError("image outside BIN chunk")
        with Path(path).open("rb") as stream:
            stream.seek(binary[0] + start)
            return stream.read(size)
    uri = image.get("uri", "")
    if not uri.startswith(("data:image/png;base64,", "data:image/jpeg;base64,")):
        raise ValueError("only embedded texture images are supported")
    return base64.b64decode(uri.split(",", 1)[1], validate=True)


def used_materials(doc):
    """Walk active nodes once; instancing does not duplicate texture owners."""
    scene = _index(doc.get("scenes", []), doc.get("scene", 0), "scene")
    pending = list(scene.get("nodes", []))
    seen, materials = set(), set()
    while pending:
        index = pending.pop()
        node = _index(doc.get("nodes", []), index, "node")
        if index in seen:
            continue
        seen.add(index)
        pending.extend(node.get("children", []))
        if "mesh" in node:
            mesh = _index(doc.get("meshes", []), node["mesh"], "mesh")
            for primitive in mesh.get("primitives", []):
                material = primitive.get("material")
                if material is not None:
                    _index(doc.get("materials", []), material, "material")
                materials.add(material)
    return sorted(materials, key=lambda value: -1 if value is None else value)


def texture_variants(doc, materials):
    variants = {}

    def image_for(info):
        texture = _index(doc.get("textures", []), info["index"], "texture")
        if texture.get("extensions") or info.get("texCoord", 0) != 0 or info.get("extensions"):
            raise ValueError("unsupported texture extension/coordinates")
        index = texture.get("source")
        _index(doc.get("images", []), index, "image")
        return index

    def register(key, role, image, mode="OPAQUE"):
        variants.setdefault(key, dict(role=role, image=image, mode=mode,
                                      format="U8" if role == "strength" else
                                      "ARGB4444" if mode == "BLEND" else "ARGB1555"))

    for index in materials:
        material = {} if index is None else doc["materials"][index]
        if any(key in material for key in ("normalTexture", "occlusionTexture", "emissiveTexture")):
            raise ValueError("unsupported material texture role")
        pbr = material.get("pbrMetallicRoughness") or {}
        mode, cutoff = material.get("alphaMode", "OPAQUE"), material.get("alphaCutoff", 0.5)
        if mode not in ("OPAQUE", "MASK", "BLEND"):
            raise ValueError("unsupported alphaMode")
        if type(cutoff) not in (int, float) or not math.isfinite(cutoff) or not 0 <= cutoff <= 1:
            raise ValueError("invalid alpha cutoff")
        image = image_for(pbr["baseColorTexture"]) if "baseColorTexture" in pbr else None
        register(("base", image, mode, float(cutoff)), "base", image, mode)
        if "metallicRoughnessTexture" in pbr:
            image = image_for(pbr["metallicRoughnessTexture"])
            register(("strength", image), "strength", image)
        spec = (material.get("extensions") or {}).get("KHR_materials_specular") or {}
        if "specularTexture" in spec:
            raise ValueError("unsupported specularTexture")
        if "specularColorTexture" in spec:
            image = image_for(spec["specularColorTexture"])
            register(("spec", image), "spec", image)
    return list(variants.values())


def target_dimensions(width, height, cap):
    return tuple(max(8, min(cap, 1 << (value.bit_length() - 1))) for value in (width, height))


def measure(path, policy=None):
    policy = repository_policy() if policy is None else policy
    doc, binary = read_glb(path)
    materials = used_materials(doc)
    variants = texture_variants(doc, materials)
    images = {}
    for variant in variants:
        index = variant["image"]
        if index is not None and index not in images:
            info = doc["images"][index]
            width, height = image_dimensions(_image_bytes(path, doc, binary, info))
            images[index] = dict(index=index, name=info.get("name", "image-%d" % index),
                                 width=width, height=height, pixels=width * height)
    arena = flythrough_budget()["texture_arena_bytes"]
    profiles = {}
    for name in PROFILES:
        cap = int(name[-2:])
        payload = cpu_strength = slots = vq_count = fallback_count = 0
        format_bytes, format_slots = {}, {}
        for variant in variants:
            image = images.get(variant["image"], dict(width=8, height=8))
            width, height = target_dimensions(image["width"], image["height"], cap)
            if variant["role"] == "strength":
                cpu_strength += width * height
                continue
            size = width * height * 2
            output_format = variant["format"]
            if name.startswith("vq"):
                if width == height and variant["format"] == "ARGB1555":
                    size = payload_size(width, height)
                    output_format = "VQ_ARGB1555"
                    vq_count += 1
                else:
                    fallback_count += 1
            payload += size
            slots += 1
            format_bytes[output_format] = format_bytes.get(output_format, 0) + size
            format_slots[output_format] = format_slots.get(output_format, 0) + 1
        profiles[name] = dict(cap=cap, payload_bytes=payload,
                              allocator_chunk_estimate_bytes=payload + slots * 32,
                              cpu_strength_bytes=cpu_strength, color_slots=slots,
                              format_bytes=format_bytes, format_slots=format_slots,
                              vq_slots=vq_count, raw_fallback_slots=fallback_count,
                              headroom_bytes=arena - payload, fits_payload=payload <= arena,
                              within_texture_catalog_caps=(len(variants) <= policy["MAX_TEXTURES"]
                                  and payload + cpu_strength <= policy["MAX_TEXTURE_BYTES"]),
                              resident_path_supported=not name.startswith("vq"))
    return dict(level=Path(path).parent.name, input=str(Path(path).resolve()),
                input_bytes=Path(path).stat().st_size, embedded_images=len(doc.get("images", [])),
                distinct_textures=len(images), source_pixels=sum(i["pixels"] for i in images.values()),
                used_materials=len(materials), catalog_slots=len(variants),
                color_slots=sum(v["role"] != "strength" for v in variants),
                strength_slots=sum(v["role"] == "strength" for v in variants),
                fallback_slots=sum(v["image"] is None for v in variants), profiles=profiles,
                largest_textures=sorted(images.values(), key=lambda i: (-i["pixels"], i["index"]))[:10])


def table(levels):
    lines = ["Level | Textures | Source pixels | raw64 B | raw32 B | VQ64 B | VQ32 B | Fits (payload)",
             "--- | ---: | ---: | ---: | ---: | ---: | ---: | ---"]
    for level in levels:
        costs = [str(level["profiles"][p]["payload_bytes"]) for p in PROFILES]
        fits = ", ".join(p for p in PROFILES if level["profiles"][p]["fits_payload"]) or "none"
        lines.append(" | ".join([level["level"], str(level["distinct_textures"]),
                                  str(level["source_pixels"]), *costs, fits]))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("glb", nargs="?", type=Path)
    parser.add_argument("--all", dest="levels_dir", type=Path)
    parser.add_argument("--json", dest="output", type=Path)
    args = parser.parse_args(argv)
    if (args.glb is None) == (args.levels_dir is None):
        parser.error("provide either GLB or --all LEVELS_DIR")
    try:
        policy = repository_policy()
        paths = [args.glb] if args.glb else sorted(args.levels_dir.glob("*/*-background.glb"))
        if not paths:
            raise ValueError("no level background GLBs found")
        levels = [measure(path, policy) for path in paths]
        report = dict(schema_version=1, measurement="dimension-based payload costs; no encoding or GPU run",
                      vram=flythrough_budget(), policy=policy, assumptions=__doc__, levels=levels)
        if args.levels_dir:
            report["directories_without_background_glb"] = sorted(
                p.name for p in args.levels_dir.iterdir() if p.is_dir()
                and not list(p.glob("*-background.glb")))
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("(AI-assisted) Texture arena: %d bytes; no mipmaps. VQ requires resident-path integration."
              % report["vram"]["texture_arena_bytes"])
        print(table(levels))
        if args.glb:
            print("Largest referenced source images (index, name, dimensions, pixels):")
            for image in levels[0]["largest_textures"]:
                print("%d %s %dx%d %d" % (image["index"], image["name"], image["width"],
                                                image["height"], image["pixels"]))
        return 0
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print("texture_budget: %s (AI-assisted)" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
