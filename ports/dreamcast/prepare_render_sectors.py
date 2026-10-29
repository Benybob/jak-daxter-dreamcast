"""Offline GLB to DRM2 render-sector package (AI-assisted proposal), ISC.

Human-authorized Grok collaboration, reviewed and corrected by Codex. Version
2 is experimental. This host converter ships no game assets and implements no
GPU shader. A separate bounded C reader exists; GPU/cache integration is pending.

The glTF walk matches convert_scene.py. Base-color bake, UV, and vertex color
match convert_textured.py, including Python round/clamp. UV is the accessor
float. There is no extra V flip. Base color factor [2,2,2,2] stays in that
multiply; it is the original PS2 blend compensation.

NORMAL, when present, becomes a world-space unit normal via the inverse
transpose of the node 3x3. When it is absent, each face stores the geometric
normal of its wound world positions, or (0,0,0) when the face has no area.
Specular color and reflection-strength images are kept. They are not dropped
for a base-color-only package.

Huge faces stay whole in one global bucket.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import os
import shutil
import struct
import sys
import zlib
from io import BytesIO
from pathlib import Path

from PIL import Image


MAGIC_MANIFEST = b"DRM2"
MAGIC_SECTOR = b"DRS2"
FORMAT_VERSION = 2

# Vertex <8fI> is x, y, z, u, v, world nx, ny, nz, argb. Three vertices plus
# source id and material id is the 116-byte face.
FACE_BYTES = 116
VERTEX_BYTES = 36
MATERIAL_BYTES = 64
MANIFEST_FLAG_ABSOLUTE = 1
SECTOR_FLAG_GLOBAL = 1
TEXTURE_ARGB1555 = 1
TEXTURE_STRENGTH_U8 = 2
ABSENT_TEXTURE = 0xFFFFFFFF
# Header value 1: world-space float32 normals, each unit length or all zeros.
NORMAL_WORLD_UNIT = 1

FLAG_MASK = 1
FLAG_UNLIT = 2
FLAG_CLAMP_S = 4
FLAG_CLAMP_T = 8
FLAG_MIRROR_S = 16
FLAG_MIRROR_T = 32
FLAG_DOUBLE_SIDED = 64
FLAG_NEAREST = 128
FLAG_WRAP = FLAG_CLAMP_S | FLAG_CLAMP_T | FLAG_MIRROR_S | FLAG_MIRROR_T
FLAG_SAMPLER = FLAG_WRAP | FLAG_NEAREST
FLAG_ALLOWED = FLAG_MASK | FLAG_WRAP
BASE_FLAG_ALLOWED = FLAG_ALLOWED | FLAG_UNLIT | FLAG_DOUBLE_SIDED | FLAG_NEAREST

PER_SECTOR_CAP = 4096
# zlib level for sector payloads. 0 writes stored blocks only, which
# sector_inflate_stored.c decodes without zlib (SDKs lacking a native zlib).
ZLIB_LEVEL = 9
GLOBAL_CAP = 4096
WINDOW_UNIQUE_CAP = 8192
MAX_REPLICAS_PER_TRIANGLE = 36
MAX_SOURCE = 1_000_000
MAX_SECTORS = 8192
MAX_TOTAL_REPLICAS = 2_000_000
MAX_MATERIALS = 256
MAX_TEXTURES = 256
MAX_TEXTURE_BYTES = 2 * 1024 * 1024
# Storage bound, not resident RAM. The complete original training LOD package
# measured 7,899,260 bytes with all material inputs retained (old 6MiB refused).
# Geometry still streams one bounded file/window; none of those limits changed.
MAX_PACKAGE_BYTES = 8 * 1024 * 1024
MAX_AXIS_SPAN_M = 64.0
WORLD_ABS_MAX = 1_000_000.0
UV_ABS_MAX = 65536.0
MIN_CELL_SIZE_M = 4.0
MAX_CELL_SIZE_M = 64.0
DEFAULT_CELL_SIZE_M = 16.0
DEFAULT_TEXTURE_SIZE = 64
MIN_TEXTURE_SIZE = 8
COMPRESS_SLACK = 1024
RAW_SECTOR_LIMIT = 512 * 1024
COMPRESSED_INPUT_LIMIT = 513 * 1024
CANDIDATE_AXIS_GUARD = 128

RANGE_PAD_M = 1e-4
SEPARATION_SLACK_M = 1e-6

COMPONENTS = {
    5120: ("b", 127),
    5121: ("B", 255),
    5122: ("h", 32767),
    5123: ("H", 65535),
    5125: ("I", 4294967295),
    5126: ("f", 1),
}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
ALLOWED_ATTRIBUTES = {"POSITION", "NORMAL", "TANGENT", "TEXCOORD_0", "COLOR_0"}
ALLOWED_REQUIRED = {"KHR_materials_unlit", "KHR_materials_specular"}
SPECULAR_KEYS = {"specularColorTexture", "specularColorFactor", "specularFactor"}
PBR_KEYS = {
    "baseColorFactor",
    "baseColorTexture",
    "metallicFactor",
    "roughnessFactor",
    "metallicRoughnessTexture",
}
MATERIAL_KEYS = {
    "name",
    "pbrMetallicRoughness",
    "alphaMode",
    "alphaCutoff",
    "doubleSided",
    "emissiveFactor",
    "extensions",
}

HEADER_FIELDS = [
    ("magic", "4s"),
    ("version", "I"),
    ("header_bytes", "I"),
    ("flags", "I"),
    ("source_count", "I"),
    ("local_sector_count", "I"),
    ("replica_count", "I"),
    ("global_count", "I"),
    ("material_count", "I"),
    ("texture_count", "I"),
    ("texture_bytes", "I"),
    ("package_bytes", "I"),
    ("cell_size", "f"),
    ("max_axis_span", "f"),
    ("per_sector_cap", "I"),
    ("global_cap", "I"),
    ("window_unique_cap", "I"),
    ("max_replicas_per_triangle", "I"),
    ("source_cap", "I"),
    ("sector_cap", "I"),
    ("replica_cap", "I"),
    ("material_cap", "I"),
    ("texture_cap", "I"),
    ("texture_byte_cap", "I"),
    ("package_byte_cap", "I"),
    ("face_stride", "I"),
    ("vertex_stride", "I"),
    ("ix_min", "i"),
    ("ix_max", "i"),
    ("iz_min", "i"),
    ("iz_max", "i"),
    ("bounds_min_x", "f"),
    ("bounds_min_y", "f"),
    ("bounds_min_z", "f"),
    ("bounds_max_x", "f"),
    ("bounds_max_y", "f"),
    ("bounds_max_z", "f"),
    ("global_uncompressed", "I"),
    ("global_file_bytes", "I"),
    ("global_payload_crc", "I"),
    ("global_compressed_crc", "I"),
    ("source_sha256", "32s"),
    ("texture_blob_bytes", "I"),
    ("tables_bytes", "I"),
    ("observed_max_sector_records", "I"),
    ("material_stride", "I"),
    ("normal_encoding", "I"),
    ("reserved_2", "I"),
    ("reserved_3", "I"),
    ("reserved_4", "I"),
    ("reserved_5", "I"),
    ("reserved_6", "I"),
    ("reserved_7", "I"),
    ("reserved_8", "I"),
    ("reserved_9", "I"),
    ("reserved_10", "I"),
    ("reserved_11", "I"),
]

_KIND_SIZE = {"4s": 4, "32s": 32, "I": 4, "i": 4, "f": 4}


def _field_offsets(fields):
    offsets = {}
    cursor = 0
    for name, kind in fields:
        offsets[name] = cursor
        cursor += _KIND_SIZE[kind]
    return offsets, cursor


HEADER_OFFSETS, HEADER_SIZE = _field_offsets(HEADER_FIELDS)
HEADER_STRUCT = struct.Struct("<" + "".join(kind for _name, kind in HEADER_FIELDS))
FACE_STRUCT = struct.Struct("<II8fI8fI8fI")
SECTOR_HEADER_STRUCT = struct.Struct("<4sIIiiIII")
TEXTURE_STRUCT = struct.Struct("<IIHHI")
MATERIAL_STRUCT = struct.Struct("<IIfIIIffIIf3fII")
ENTRY_STRUCT = struct.Struct("<iiIIIIII")

assert HEADER_SIZE == 256
assert HEADER_STRUCT.size == 256
assert FACE_STRUCT.size == FACE_BYTES == 116
assert VERTEX_BYTES == 36 and struct.calcsize("<8fI") == VERTEX_BYTES
assert SECTOR_HEADER_STRUCT.size == 32
assert TEXTURE_STRUCT.size == 16
assert MATERIAL_STRUCT.size == MATERIAL_BYTES == 64
assert ENTRY_STRUCT.size == 32
assert HEADER_OFFSETS["cell_size"] == 48
assert HEADER_OFFSETS["per_sector_cap"] == 56
assert HEADER_OFFSETS["source_sha256"] == 164
assert HEADER_OFFSETS["material_stride"] == 208
assert HEADER_OFFSETS["normal_encoding"] == 212
assert HEADER_OFFSETS["reserved_11"] + 4 == 256
assert FLAG_UNLIT & FLAG_ALLOWED == 0
assert PER_SECTOR_CAP * FACE_BYTES == 475136
assert PER_SECTOR_CAP * FACE_BYTES < RAW_SECTOR_LIMIT
assert PER_SECTOR_CAP * FACE_BYTES + COMPRESS_SLACK < COMPRESSED_INPUT_LIMIT
assert GLOBAL_CAP * FACE_BYTES < RAW_SECTOR_LIMIT


class RenderSectorError(ValueError):
    """The package was refused. No new manifest is committed for this failure."""


class RenderPackage:
    """In-memory DRM2 package. Bytes are complete before any directory write."""

    def __init__(self, manifest, sectors, global_sector, report_bytes, summary, parts):
        self.manifest = bytes(manifest)
        self.sectors = dict(sectors)
        self.global_sector = bytes(global_sector)
        self.report_bytes = bytes(report_bytes)
        self.summary = summary
        self._parts = parts
        self.accessor_decode_count = parts["accessor_decodes"]
        self.position_decode_count = parts["position_decodes"]
        self.position_transform_count = parts["position_transforms"]
        self.normal_decode_count = parts["normal_decodes"]
        self.normal_transform_count = parts["normal_transforms"]
        self.authored_normal_faces = parts["authored_normal_faces"]
        self.flat_normal_faces = parts["flat_normal_faces"]

    @property
    def source_count(self):
        return self._parts["source_count"]

    @property
    def cell_size(self):
        return self._parts["cell_size"]


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def f32(value):
    try:
        return struct.unpack("<f", struct.pack("<f", float(value)))[0]
    except (OverflowError, ValueError) as error:
        raise RenderSectorError("float32 value out of range") from error


def contract_constants():
    """Live caps recorded in every manifest and checked again by the reader."""
    return {
        "flags": MANIFEST_FLAG_ABSOLUTE,
        "max_axis_span": f32(MAX_AXIS_SPAN_M),
        "per_sector_cap": PER_SECTOR_CAP,
        "global_cap": GLOBAL_CAP,
        "window_unique_cap": WINDOW_UNIQUE_CAP,
        "max_replicas_per_triangle": MAX_REPLICAS_PER_TRIANGLE,
        "source_cap": MAX_SOURCE,
        "sector_cap": MAX_SECTORS,
        "replica_cap": MAX_TOTAL_REPLICAS,
        "material_cap": MAX_MATERIALS,
        "texture_cap": MAX_TEXTURES,
        "texture_byte_cap": MAX_TEXTURE_BYTES,
        "package_byte_cap": MAX_PACKAGE_BYTES,
        "face_stride": FACE_BYTES,
        "vertex_stride": VERTEX_BYTES,
        "material_stride": MATERIAL_BYTES,
        "normal_encoding": NORMAL_WORLD_UNIT,
    }


def quantize_cell_size(cell_size):
    if isinstance(cell_size, bool) or not isinstance(cell_size, (int, float)):
        raise RenderSectorError("cell size out of range")
    numeric = float(cell_size)
    if not math.isfinite(numeric) or not MIN_CELL_SIZE_M <= numeric <= MAX_CELL_SIZE_M:
        raise RenderSectorError("cell size out of range")
    quantized = f32(numeric)
    if not math.isfinite(quantized) or quantized < MIN_CELL_SIZE_M or quantized > MAX_CELL_SIZE_M:
        raise RenderSectorError("cell size out of range")
    return quantized


def quantize_texture_size(size):
    if isinstance(size, bool) or not isinstance(size, int):
        raise RenderSectorError("texture size out of range")
    if size < MIN_TEXTURE_SIZE or size > DEFAULT_TEXTURE_SIZE or size & (size - 1):
        raise RenderSectorError("texture size out of range")
    return size


def parse_glb_bytes(data):
    """glTF 2 binary, same rules as convert_scene.read_glb."""
    try:
        if not isinstance(data, (bytes, bytearray)):
            raise ValueError("expected a complete glTF 2 binary (.glb)")
        data = bytes(data)
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
        doc = json.loads(chunks[0x4E4F534A])
        asset = doc.get("asset") if isinstance(doc, dict) else None
        if not isinstance(asset, dict) or asset.get("version") != "2.0":
            raise ValueError("expected glTF 2 asset")
        binary = chunks.get(0x004E4942, b"")
        buffers = doc.get("buffers", [])
        if not isinstance(buffers, list) or len(buffers) != 1 or not isinstance(buffers[0], dict):
            raise ValueError("only a single embedded GLB buffer is supported")
        declared = buffers[0].get("byteLength")
        if ("uri" in buffers[0] or isinstance(declared, bool) or not isinstance(declared, int)
                or declared < 0 or not declared <= len(binary) <= declared + 3):
            raise ValueError("only a single embedded GLB buffer is supported")
        return doc, binary[:buffers[0]["byteLength"]]
    except RenderSectorError:
        raise
    except (ValueError, KeyError, IndexError, TypeError, struct.error) as error:
        raise RenderSectorError(str(error)) from error


def read_glb(path):
    return parse_glb_bytes(Path(path).read_bytes())


def accessor(doc, binary, index):
    """Decoded accessor. Callers cache by index; this function does not."""
    info = doc["accessors"][index]
    if "sparse" in info:
        raise ValueError("sparse accessors are unsupported")
    view_index = info["bufferView"]
    if isinstance(view_index, bool) or not isinstance(view_index, int) or not 0 <= view_index < len(doc["bufferViews"]):
        raise ValueError("buffer view index out of range")
    view = doc["bufferViews"][view_index]
    if view.get("buffer", 0) != 0:
        raise ValueError("external buffer reference")
    code, maximum = COMPONENTS[info["componentType"]]
    width = WIDTHS[info["type"]]
    layout = struct.Struct("<" + code * width)
    stride = view.get("byteStride", layout.size)
    start = view.get("byteOffset", 0) + info.get("byteOffset", 0)
    end = view.get("byteOffset", 0) + view["byteLength"]
    count = info["count"]
    if any(isinstance(value, bool) or not isinstance(value, int) or value < 0
           for value in (view.get("byteOffset", 0), view["byteLength"],
                         info.get("byteOffset", 0), stride, count)):
        raise ValueError("invalid accessor bounds")
    if (
        count < 0
        or stride < layout.size
        or start < view.get("byteOffset", 0)
        or end > len(binary)
        or start + max(0, count - 1) * stride + (layout.size if count else 0) > end
    ):
        raise ValueError("accessor outside its buffer view")
    result = []
    for i in range(count):
        values = layout.unpack_from(binary, start + i * stride)
        if info.get("normalized") and code != "f":
            values = tuple(max(-1, value / maximum) for value in values)
        if not all(math.isfinite(v) for v in values):
            raise ValueError("non-finite accessor value")
        result.append(values)
    return result


def multiply(a, b):
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def transform(matrix, point):
    return tuple(
        sum(matrix[k * 4 + r] * point[k] for k in range(3)) + matrix[12 + r] for r in range(3)
    )


def node_matrix(node):
    if "matrix" in node:
        matrix = node["matrix"]
        if len(matrix) != 16 or not all(math.isfinite(v) for v in matrix):
            raise ValueError("invalid node matrix")
        if any(abs(matrix[i]) > 1e-6 for i in (3, 7, 11)) or abs(matrix[15] - 1) > 1e-6:
            raise ValueError("projective node matrix unsupported")
        return matrix
    x, y, z, w = node.get("rotation", [0, 0, 0, 1])
    norm = math.sqrt(x * x + y * y + z * z + w * w)
    if not math.isfinite(norm) or norm < 1e-12:
        raise ValueError("invalid quaternion")
    x, y, z, w = (v / norm for v in (x, y, z, w))
    matrix = [
        1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
        2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
        2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
        0, 0, 0, 1,
    ]
    scale = node.get("scale", [1, 1, 1])
    for column in range(3):
        for row in range(3):
            matrix[column * 4 + row] *= scale[column]
    matrix[12:15] = node.get("translation", [0, 0, 0])
    if not all(math.isfinite(v) for v in matrix):
        raise ValueError("non-finite node transform")
    return matrix


def _inverse_transpose_3x3(matrix):
    """Inverse-transpose of the upper 3x3. Singular when a stored normal needs it."""
    m00, m10, m20 = matrix[0], matrix[1], matrix[2]
    m01, m11, m21 = matrix[4], matrix[5], matrix[6]
    m02, m12, m22 = matrix[8], matrix[9], matrix[10]
    c00 = m11 * m22 - m12 * m21
    c01 = m12 * m20 - m10 * m22
    c02 = m10 * m21 - m11 * m20
    c10 = m02 * m21 - m01 * m22
    c11 = m00 * m22 - m02 * m20
    c12 = m01 * m20 - m00 * m21
    c20 = m01 * m12 - m02 * m11
    c21 = m02 * m10 - m00 * m12
    c22 = m00 * m11 - m01 * m10
    det = m00 * c00 + m01 * c01 + m02 * c02
    if not math.isfinite(det) or abs(det) <= 1e-12:
        raise RenderSectorError("singular matrix")
    scale = 1.0 / det
    return (
        c00 * scale, c01 * scale, c02 * scale,
        c10 * scale, c11 * scale, c12 * scale,
        c20 * scale, c21 * scale, c22 * scale,
    )


def _transform_normal(inv_t, normal):
    nx, ny, nz = normal
    return (
        inv_t[0] * nx + inv_t[1] * ny + inv_t[2] * nz,
        inv_t[3] * nx + inv_t[4] * ny + inv_t[5] * nz,
        inv_t[6] * nx + inv_t[7] * ny + inv_t[8] * nz,
    )


def _pack_unit_normal(nx, ny, nz):
    if not all(math.isfinite(value) for value in (nx, ny, nz)):
        raise RenderSectorError("invalid normal")
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if not math.isfinite(length):
        raise RenderSectorError("invalid normal")
    if length <= 1e-20:
        return (0.0, 0.0, 0.0)
    packed = (f32(nx / length), f32(ny / length), f32(nz / length))
    packed_length = math.sqrt(packed[0] * packed[0] + packed[1] * packed[1] + packed[2] * packed[2])
    if not math.isfinite(packed_length) or abs(packed_length - 1.0) > 1e-5:
        raise RenderSectorError("invalid normal")
    return packed


def _face_normal(a, b, c):
    """Unit normal of wound world positions. Zero area stores (0, 0, 0)."""
    ax, ay, az = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    bx, by, bz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    return _pack_unit_normal(
        ay * bz - az * by,
        az * bx - ax * bz,
        ax * by - ay * bx,
    )


def _valid_normal(nx, ny, nz):
    if not all(math.isfinite(value) for value in (nx, ny, nz)):
        return False
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if length == 0.0:
        return True
    return abs(length - 1.0) <= 1e-5


def twiddle(x, y, width, height):
    side = min(width, height)
    mask = side - 1
    value = 0
    for bit in range(side.bit_length() - 1):
        value |= ((y >> bit) & 1) << (bit * 2)
        value |= ((x >> bit) & 1) << (bit * 2 + 1)
    return value + ((x & ~mask) + (y & ~mask)) * side


def _rgba_image(doc, binary, image_id, size, *, strength=False, opaque=False):
    if image_id is None:
        image = Image.new("RGBA", (8, 8), (255, 255, 255, 255))
    else:
        info = doc["images"][image_id]
        if "bufferView" in info:
            view_index = info["bufferView"]
            if ("uri" in info or isinstance(view_index, bool) or not isinstance(view_index, int)
                    or not 0 <= view_index < len(doc["bufferViews"])):
                raise ValueError("invalid image buffer view")
            view = doc["bufferViews"][view_index]
            start = view.get("byteOffset", 0)
            length = view["byteLength"]
            if (view.get("buffer", 0) != 0
                    or any(isinstance(value, bool) or not isinstance(value, int) or value < 0
                           for value in (start, length)) or start + length > len(binary)):
                raise ValueError("image outside its buffer view")
            raw = binary[start:start + view["byteLength"]]
        elif info.get("uri", "").startswith(("data:image/png;base64,", "data:image/jpeg;base64,")):
            raw = base64.b64decode(info["uri"].split(",", 1)[1], validate=True)
        else:
            raise ValueError("only embedded texture images are supported")
        image = Image.open(BytesIO(raw))
        if max(image.size) > 4096:
            raise ValueError("source image too large")
        image = image.convert("RGBA")
    # Check the source, before resampling can hide even a single unsupported
    # channel value. These channels encode fixed OpenGOAL export constants.
    if strength and any(image.getchannel(channel).getextrema() != extrema
                        for channel, extrema in (("R", (255, 255)),
                                                 ("G", (0, 0)),
                                                 ("A", (255, 255)))):
        raise RenderSectorError("unexpected metalRough channels")
    if opaque:
        # RGB of an opaque/specular color map does not depend on source alpha.
        # RGBA resampling premultiplies alpha, so remove it BEFORE resampling.
        image.putalpha(255)
    dims = tuple(max(8, min(size, 1 << (value.bit_length() - 1))) for value in image.size)
    return image.resize(dims, Image.Resampling.LANCZOS)


def texture_pixels(doc, binary, image_id, mode, cutoff, size):
    """Twiddled ARGB1555. Same bake as convert_textured.texture_pixels."""
    image = _rgba_image(doc, binary, image_id, size, opaque=(mode == "OPAQUE"))
    width, height = image.size
    result = bytearray(width * height * 2)
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            opaque = mode == "OPAQUE" or a / 255 >= cutoff
            value = (int(opaque) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
            struct.pack_into("<H", result, twiddle(x, y, width, height) * 2, value)
    return width, height, bytes(result)


def strength_pixels(doc, binary, image_id, size):
    """Twiddled U8 from metallicRoughness B after validating source channels.

    Source pixels are R=255, G=0, B=strength, A=255. Any other R, G, or A
    in the source is a channel this package would have to drop, so it fails.
    """
    image = _rgba_image(doc, binary, image_id, size, strength=True)
    width, height = image.size
    result = bytearray(width * height)
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            red, green, blue, alpha = pixels[x, y]
            if red != 255 or green != 0 or alpha != 255:
                raise RenderSectorError("unexpected metalRough channels")
            if isinstance(blue, bool) or not isinstance(blue, int) or not 0 <= blue <= 255:
                raise RenderSectorError("unexpected metalRough channels")
            result[twiddle(x, y, width, height)] = blue
    return width, height, bytes(result)


def _projections_overlap(axis_x, axis_z, tri, x0, x1, z0, z1):
    length = math.hypot(axis_x, axis_z)
    if length <= 1e-12:
        return True
    ax = axis_x / length
    az = axis_z / length
    projected_tri = [px * ax + pz * az for px, pz in tri]
    projected_rect = (
        x0 * ax + z0 * az,
        x1 * ax + z0 * az,
        x0 * ax + z1 * az,
        x1 * ax + z1 * az,
    )
    if max(projected_tri) < min(projected_rect) - SEPARATION_SLACK_M:
        return False
    if max(projected_rect) < min(projected_tri) - SEPARATION_SLACK_M:
        return False
    return True


def triangle_overlaps_cell(xs, zs, x0, x1, z0, z1):
    """Closed triangle-vs-rectangle test on the XZ projection.

    Y is ignored. A vertical or degenerate face is the segment or point it
    projects to. A separating gap of SEPARATION_SLACK_M meters or less counts
    as overlap. Negative cell edges use the same test.
    """
    if max(xs) < x0 - SEPARATION_SLACK_M or min(xs) > x1 + SEPARATION_SLACK_M:
        return False
    if max(zs) < z0 - SEPARATION_SLACK_M or min(zs) > z1 + SEPARATION_SLACK_M:
        return False
    tri = ((xs[0], zs[0]), (xs[1], zs[1]), (xs[2], zs[2]))
    for index in range(3):
        ax, az = tri[index]
        bx, bz = tri[(index + 1) % 3]
        edge_x = bx - ax
        edge_z = bz - az
        if edge_x * edge_x + edge_z * edge_z <= 1e-24:
            continue
        if not _projections_overlap(-edge_z, edge_x, tri, x0, x1, z0, z1):
            return False
    return True


def cell_bounds(ix, iz, cell_size):
    x0 = ix * cell_size
    z0 = iz * cell_size
    return x0, x0 + cell_size, z0, z0 + cell_size


def _i32(value):
    if not isinstance(value, int) or isinstance(value, bool):
        raise RenderSectorError("cell index")
    if value < -2147483648 or value > 2147483647:
        raise RenderSectorError("cell index exceeds i32")
    return value


def overlapping_cells(xs, zs, cell_size):
    """Closed cells whose infinite-Y prism meets the triangle.

    Returns a list, or None when the candidate window is pathological. None
    and an empty list both mean the whole triangle goes to the global bucket.
    A triangle is never dropped because a cell test missed.
    """
    min_x = min(xs) - RANGE_PAD_M
    max_x = max(xs) + RANGE_PAD_M
    min_z = min(zs) - RANGE_PAD_M
    max_z = max(zs) + RANGE_PAD_M
    ix_lo = _i32(math.ceil(min_x / cell_size) - 1)
    ix_hi = _i32(math.floor(max_x / cell_size))
    iz_lo = _i32(math.ceil(min_z / cell_size) - 1)
    iz_hi = _i32(math.floor(max_z / cell_size))
    if ix_hi < ix_lo or iz_hi < iz_lo:
        return []
    if (ix_hi - ix_lo) > CANDIDATE_AXIS_GUARD or (iz_hi - iz_lo) > CANDIDATE_AXIS_GUARD:
        return None
    hits = []
    seen = set()
    for ix in range(ix_lo, ix_hi + 1):
        for iz in range(iz_lo, iz_hi + 1):
            x0, x1, z0, z1 = cell_bounds(ix, iz, cell_size)
            if triangle_overlaps_cell(xs, zs, x0, x1, z0, z1):
                key = (_i32(ix), _i32(iz))
                if key not in seen:
                    seen.add(key)
                    hits.append(key)
    return hits


def _axis_span(values):
    return max(values) - min(values)


def classify_triangle(xs, ys, zs, cell_size):
    """Return local cell keys, or None when the face belongs in the global bucket.

    Any axis span above 64 m, more than 36 overlapping cells, or no overlapping
    cell keeps the face whole in the global bucket once. Span equal to 64 m can
    stay local. Y span counts. The XZ SAT does not look at Y.
    """
    span = max(_axis_span(xs), _axis_span(ys), _axis_span(zs))
    if not math.isfinite(span) or span > f32(MAX_AXIS_SPAN_M):
        return None
    cells = overlapping_cells(xs, zs, cell_size)
    if not cells or len(cells) > MAX_REPLICAS_PER_TRIANGLE:
        return None
    return cells


def _valid_vertex(x, y, z, u, v):
    if not all(math.isfinite(value) for value in (x, y, z, u, v)):
        return False
    if any(abs(value) > WORLD_ABS_MAX for value in (x, y, z)):
        return False
    if abs(u) > UV_ABS_MAX or abs(v) > UV_ABS_MAX:
        return False
    return True


def pack_face(source_id, material_id, vertices):
    if len(vertices) != 3:
        raise RenderSectorError("record size")
    flat = [source_id, material_id]
    for vertex in vertices:
        if len(vertex) != 9:
            raise RenderSectorError("record size")
        x, y, z, u, v, nx, ny, nz, argb = vertex
        if not _valid_vertex(x, y, z, u, v):
            raise RenderSectorError("invalid position/UV")
        if not _valid_normal(nx, ny, nz):
            raise RenderSectorError("invalid normal")
        if isinstance(argb, bool) or not isinstance(argb, int) or not 0 <= argb <= 0xFFFFFFFF:
            raise RenderSectorError("invalid vertex color")
        flat.extend((x, y, z, u, v, nx, ny, nz, argb))
    raw = FACE_STRUCT.pack(*flat)
    if len(raw) != FACE_BYTES:
        raise RenderSectorError("record size")
    return raw


def unpack_face(raw):
    raw = bytes(raw)
    if len(raw) != FACE_BYTES:
        raise RenderSectorError("record size")
    values = FACE_STRUCT.unpack(raw)
    source_id, material_id = values[0], values[1]
    vertices = []
    cursor = 2
    for _ in range(3):
        x, y, z, u, v, nx, ny, nz, argb = values[cursor:cursor + 9]
        if not _valid_vertex(x, y, z, u, v):
            raise RenderSectorError("invalid position/UV")
        if not _valid_normal(nx, ny, nz):
            raise RenderSectorError("invalid normal")
        vertices.append((x, y, z, u, v, nx, ny, nz, argb))
        cursor += 9
    return {
        "source_id": source_id,
        "material_id": material_id,
        "vertices": tuple(vertices),
        "raw": raw,
    }


def _bounded_inflate(compressed, expected):
    if not isinstance(expected, int) or isinstance(expected, bool):
        raise RenderSectorError("malformed sector")
    if not 0 <= expected <= PER_SECTOR_CAP * FACE_BYTES:
        raise RenderSectorError("malformed sector")
    obj = zlib.decompressobj()
    try:
        data = obj.decompress(compressed, expected + 1)
    except zlib.error as error:
        raise RenderSectorError("malformed sector") from error
    if len(data) != expected or not obj.eof or obj.unconsumed_tail or obj.unused_data:
        raise RenderSectorError("malformed sector")
    return data


def encode_sector(records, ix=0, iz=0, is_global=False):
    """Compress sorted 116-byte faces. Level-9 zlib, exact footer CRCs."""
    ix = _i32(0 if is_global else ix)
    iz = _i32(0 if is_global else iz)
    raws = []
    for record in records:
        if isinstance(record, dict):
            raw = bytes(record["raw"])
        else:
            raw = bytes(record)
        if len(raw) != FACE_BYTES:
            raise RenderSectorError("record size")
        unpack_face(raw)
        raws.append(raw)
    raws.sort(key=lambda raw: struct.unpack_from("<I", raw)[0])
    previous = -1
    payload = bytearray()
    for raw in raws:
        source_id = struct.unpack_from("<I", raw)[0]
        material_id = struct.unpack_from("<I", raw, 4)[0]
        if source_id <= previous:
            raise RenderSectorError("sector order")
        if source_id >= MAX_SOURCE or material_id >= MAX_MATERIALS:
            raise RenderSectorError("source id out of range" if source_id >= MAX_SOURCE else "material id out of range")
        payload += raw
        previous = source_id
    count = len(raws)
    if is_global:
        if count > GLOBAL_CAP:
            raise RenderSectorError("global cap exceeded")
    elif not 1 <= count <= PER_SECTOR_CAP:
        raise RenderSectorError("sector face cap exceeded")
    uncompressed = bytes(payload)
    if len(uncompressed) != count * FACE_BYTES:
        raise RenderSectorError("malformed sector")
    if len(uncompressed) > RAW_SECTOR_LIMIT:
        raise RenderSectorError("malformed sector")
    compressed = zlib.compress(uncompressed, ZLIB_LEVEL)
    if len(compressed) > len(uncompressed) + COMPRESS_SLACK or len(compressed) > COMPRESSED_INPUT_LIMIT:
        raise RenderSectorError("compressed size exceeds bound")
    header = SECTOR_HEADER_STRUCT.pack(
        MAGIC_SECTOR,
        FORMAT_VERSION,
        SECTOR_FLAG_GLOBAL if is_global else 0,
        ix,
        iz,
        count,
        len(uncompressed),
        len(compressed),
    )
    body = header + compressed
    blob = body + struct.pack("<II", crc32(uncompressed), crc32(compressed))
    parsed = parse_sector(blob)
    if parsed["record_count"] != count or parsed["is_global"] != bool(is_global):
        raise RenderSectorError("sector round trip")
    if [item["raw"] for item in parsed["records"]] != raws:
        raise RenderSectorError("sector round trip")
    return blob


def parse_sector(blob):
    """Validate one DRS2 file: exact EOF, both CRCs, count, order, floats."""
    if not isinstance(blob, (bytes, bytearray)):
        raise RenderSectorError("malformed sector")
    blob = bytes(blob)
    if len(blob) < SECTOR_HEADER_STRUCT.size + 8:
        raise RenderSectorError("truncated sector")
    magic, version, flags, ix, iz, count, uncompressed_bytes, compressed_bytes = SECTOR_HEADER_STRUCT.unpack_from(blob, 0)
    if magic != MAGIC_SECTOR or version != FORMAT_VERSION:
        raise RenderSectorError("malformed sector")
    if flags not in (0, SECTOR_FLAG_GLOBAL):
        raise RenderSectorError("malformed sector")
    is_global = flags == SECTOR_FLAG_GLOBAL
    if is_global:
        if ix != 0 or iz != 0 or count > GLOBAL_CAP:
            raise RenderSectorError("malformed sector")
    elif not 1 <= count <= PER_SECTOR_CAP:
        raise RenderSectorError("malformed sector")
    if uncompressed_bytes != count * FACE_BYTES:
        raise RenderSectorError("malformed sector")
    if uncompressed_bytes > RAW_SECTOR_LIMIT:
        raise RenderSectorError("malformed sector")
    if compressed_bytes > uncompressed_bytes + COMPRESS_SLACK or compressed_bytes > COMPRESSED_INPUT_LIMIT:
        raise RenderSectorError("compressed size exceeds bound")
    expected = SECTOR_HEADER_STRUCT.size + compressed_bytes + 8
    if len(blob) != expected:
        raise RenderSectorError("truncated sector" if len(blob) < expected else "malformed sector")
    compressed = blob[SECTOR_HEADER_STRUCT.size:SECTOR_HEADER_STRUCT.size + compressed_bytes]
    payload_crc, compressed_crc = struct.unpack_from("<II", blob, len(blob) - 8)
    if crc32(compressed) != compressed_crc:
        raise RenderSectorError("sector crc mismatch")
    uncompressed = _bounded_inflate(compressed, uncompressed_bytes)
    if crc32(uncompressed) != payload_crc:
        raise RenderSectorError("sector crc mismatch")
    records = []
    previous = -1
    for index in range(count):
        raw = uncompressed[index * FACE_BYTES:(index + 1) * FACE_BYTES]
        face = unpack_face(raw)
        if face["source_id"] <= previous or face["source_id"] >= MAX_SOURCE:
            raise RenderSectorError("sector order")
        if face["material_id"] >= MAX_MATERIALS:
            raise RenderSectorError("material id out of range")
        records.append(face)
        previous = face["source_id"]
    return {
        "is_global": is_global,
        "flags": flags,
        "ix": ix,
        "iz": iz,
        "record_count": count,
        "uncompressed_bytes": uncompressed_bytes,
        "compressed_bytes": compressed_bytes,
        "payload_crc": payload_crc,
        "compressed_crc": compressed_crc,
        "records": records,
    }


def _pack_header(values):
    ordered = []
    for name, _kind in HEADER_FIELDS:
        if name.startswith("reserved_"):
            ordered.append(int(values.get(name, 0)))
        else:
            ordered.append(values[name])
    blob = HEADER_STRUCT.pack(*ordered)
    if len(blob) != HEADER_SIZE:
        raise RenderSectorError("internal header size")
    return blob


def _unpack_header(blob):
    items = HEADER_STRUCT.unpack_from(blob, 0)
    return dict(zip((name for name, _kind in HEADER_FIELDS), items))


def _build_tables(textures, materials, entries):
    directory = bytearray()
    pixels = bytearray()
    for texture in textures:
        offset = len(pixels)
        directory += TEXTURE_STRUCT.pack(
            offset,
            len(texture["pixels"]),
            texture["width"],
            texture["height"],
            texture["format"],
        )
        pixels += texture["pixels"]
    pad = (4 - (len(pixels) % 4)) % 4
    pixels += b"\x00" * pad
    material_blob = bytearray()
    for material in materials:
        color = material["specular_color_factor"]
        material_blob += MATERIAL_STRUCT.pack(
            material["base_texture"],
            material["base_flags"],
            material["alpha_cutoff"],
            material["alpha_mode"],
            material["strength_texture"],
            material["strength_flags"],
            material["metallic_factor"],
            material["roughness_factor"],
            material["spec_color_texture"],
            material["spec_flags"],
            material["specular_factor"],
            color[0],
            color[1],
            color[2],
            0,
            0,
        )
    entry_blob = bytearray()
    previous = None
    for entry in entries:
        key = (entry["ix"], entry["iz"])
        if previous is not None and key <= previous:
            raise RenderSectorError("manifest order")
        entry_blob += ENTRY_STRUCT.pack(
            entry["ix"],
            entry["iz"],
            entry["record_count"],
            entry["uncompressed_bytes"],
            entry["file_bytes"],
            entry["payload_crc"],
            entry["compressed_crc"],
            0,
        )
        previous = key
    tables = bytes(directory) + bytes(pixels) + bytes(material_blob) + bytes(entry_blob)
    return tables, len(pixels), len(pixels) - pad


def _interval_overlap(a0, a1, b0, b1):
    return not (a1 < b0 or b1 < a0)


def _texture_record_ok(texture, pixel_blob, unpadded):
    width = texture["width"]
    height = texture["height"]
    if texture["format"] == TEXTURE_ARGB1555:
        channels = 2
    elif texture["format"] == TEXTURE_STRENGTH_U8:
        channels = 1
    else:
        return False
    if width < MIN_TEXTURE_SIZE or height < MIN_TEXTURE_SIZE:
        return False
    if width > DEFAULT_TEXTURE_SIZE or height > DEFAULT_TEXTURE_SIZE:
        return False
    if width & (width - 1) or height & (height - 1):
        return False
    if texture["pixel_bytes"] != width * height * channels:
        return False
    end = texture["pixel_offset"] + texture["pixel_bytes"]
    if texture["pixel_offset"] < 0 or end > unpadded or end > len(pixel_blob):
        return False
    return True


def _axis_flags_ok(flags, allowed):
    if flags & ~allowed:
        return False
    if (flags & FLAG_CLAMP_S) and (flags & FLAG_MIRROR_S):
        return False
    if (flags & FLAG_CLAMP_T) and (flags & FLAG_MIRROR_T):
        return False
    return True


def _unit_range(value):
    return math.isfinite(value) and 0.0 <= value <= 1.0


def _material_ok(material, textures):
    flags = material["base_flags"]
    if not _axis_flags_ok(flags, BASE_FLAG_ALLOWED):
        return False
    mode = material["alpha_mode"]
    if mode not in (0, 1) or mode != (1 if flags & FLAG_MASK else 0):
        return False
    if not _unit_range(material["alpha_cutoff"]):
        return False
    base = material["base_texture"]
    if not 0 <= base < len(textures) or textures[base]["format"] != TEXTURE_ARGB1555:
        return False
    strength = material["strength_texture"]
    spec = material["spec_color_texture"]
    if strength == ABSENT_TEXTURE:
        if material["strength_flags"] != 0:
            return False
    elif (
        not 0 <= strength < len(textures)
        or textures[strength]["format"] != TEXTURE_STRENGTH_U8
        or not _axis_flags_ok(material["strength_flags"], FLAG_SAMPLER)
    ):
        return False
    if spec == ABSENT_TEXTURE:
        if material["spec_flags"] != 0:
            return False
    elif (
        not 0 <= spec < len(textures)
        or textures[spec]["format"] != TEXTURE_ARGB1555
        or not _axis_flags_ok(material["spec_flags"], FLAG_SAMPLER)
    ):
        return False
    if not _unit_range(material["metallic_factor"]) or not _unit_range(material["roughness_factor"]):
        return False
    if not _unit_range(material["specular_factor"]):
        return False
    color = material["specular_color_factor"]
    if len(color) != 3 or not all(_unit_range(channel) for channel in color):
        return False
    if material["reserved"] != (0, 0):
        return False
    unlit = bool(flags & FLAG_UNLIT)
    if unlit and (
        strength != ABSENT_TEXTURE
        or spec != ABSENT_TEXTURE
        or material["specular_factor"] != f32(1.0)
        or color != (f32(1.0), f32(1.0), f32(1.0))
    ):
        return False
    return True


def parse_manifest(blob):
    """Validate a DRM2 manifest. No JSON. Exact EOF and resigned caps."""
    if not isinstance(blob, (bytes, bytearray)):
        raise RenderSectorError("malformed manifest")
    blob = bytes(blob)
    if len(blob) < HEADER_SIZE + 4:
        raise RenderSectorError("truncated manifest")
    if blob[:4] != MAGIC_MANIFEST:
        raise RenderSectorError("malformed manifest")
    header = _unpack_header(blob)
    expected_len = HEADER_SIZE + header["tables_bytes"] + 4
    if len(blob) < expected_len:
        raise RenderSectorError("truncated manifest")
    if len(blob) > expected_len:
        raise RenderSectorError("malformed manifest")
    if crc32(blob[:-4]) != struct.unpack_from("<I", blob, len(blob) - 4)[0]:
        raise RenderSectorError("manifest crc mismatch")
    if header["version"] != FORMAT_VERSION or header["header_bytes"] != HEADER_SIZE:
        raise RenderSectorError("manifest contract mismatch")
    for name, _kind in HEADER_FIELDS:
        if name.startswith("reserved_") and header[name] != 0:
            raise RenderSectorError("malformed manifest")
    constants = contract_constants()
    for key, expected in constants.items():
        if header[key] != expected:
            raise RenderSectorError("manifest contract mismatch")
    cell = quantize_cell_size(header["cell_size"])
    if header["cell_size"] != cell:
        raise RenderSectorError("manifest contract mismatch")
    counts = {
        "source_count": MAX_SOURCE,
        "local_sector_count": MAX_SECTORS,
        "replica_count": MAX_TOTAL_REPLICAS,
        "global_count": GLOBAL_CAP,
        "material_count": MAX_MATERIALS,
        "texture_count": MAX_TEXTURES,
        "texture_bytes": MAX_TEXTURE_BYTES,
    }
    if header["source_count"] < 1:
        raise RenderSectorError("malformed manifest")
    for key, cap in counts.items():
        if header[key] > cap:
            raise RenderSectorError("package budget exceeded" if key == "texture_bytes" else "malformed manifest")
    local_sources = header["source_count"] - header["global_count"]
    if (local_sources < 0 or header["replica_count"] < local_sources
            or header["replica_count"] > local_sources * MAX_REPLICAS_PER_TRIANGLE
            or header["local_sector_count"] > header["replica_count"]):
        raise RenderSectorError("malformed manifest")
    if header["package_bytes"] > MAX_PACKAGE_BYTES:
        raise RenderSectorError("package budget exceeded")
    if header["global_uncompressed"] != header["global_count"] * FACE_BYTES:
        raise RenderSectorError("malformed manifest")
    compressed_global = header["global_file_bytes"] - SECTOR_HEADER_STRUCT.size - 8
    if compressed_global < 8 or compressed_global > header["global_uncompressed"] + COMPRESS_SLACK:
        raise RenderSectorError("malformed manifest")
    if compressed_global > COMPRESSED_INPUT_LIMIT:
        raise RenderSectorError("malformed manifest")
    bounds = [header[name] for name in (
        "bounds_min_x", "bounds_min_y", "bounds_min_z",
        "bounds_max_x", "bounds_max_y", "bounds_max_z",
    )]
    if not all(math.isfinite(value) and abs(value) <= WORLD_ABS_MAX for value in bounds):
        raise RenderSectorError("malformed manifest")
    if bounds[0] > bounds[3] or bounds[1] > bounds[4] or bounds[2] > bounds[5]:
        raise RenderSectorError("malformed manifest")
    if len(header["source_sha256"]) != 32:
        raise RenderSectorError("malformed manifest")

    texture_count = header["texture_count"]
    material_count = header["material_count"]
    sector_count = header["local_sector_count"]
    directory_bytes = texture_count * TEXTURE_STRUCT.size
    material_bytes = material_count * MATERIAL_STRUCT.size
    entry_bytes = sector_count * ENTRY_STRUCT.size
    tables_bytes = directory_bytes + header["texture_blob_bytes"] + material_bytes + entry_bytes
    if header["tables_bytes"] != tables_bytes:
        raise RenderSectorError("malformed manifest")
    if len(blob) != HEADER_SIZE + tables_bytes + 4:
        raise RenderSectorError("malformed manifest")
    if material_count < 1 or texture_count < 1:
        raise RenderSectorError("malformed manifest")

    cursor = HEADER_SIZE
    textures = []
    for _ in range(texture_count):
        pixel_offset, pixel_bytes, width, height, tex_format = TEXTURE_STRUCT.unpack_from(blob, cursor)
        textures.append({
            "pixel_offset": pixel_offset,
            "pixel_bytes": pixel_bytes,
            "width": width,
            "height": height,
            "format": tex_format,
        })
        cursor += TEXTURE_STRUCT.size
    pixel_blob = blob[cursor:cursor + header["texture_blob_bytes"]]
    cursor += header["texture_blob_bytes"]
    unpadded = header["texture_bytes"]
    if header["texture_blob_bytes"] < unpadded or header["texture_blob_bytes"] - unpadded > 3:
        raise RenderSectorError("malformed manifest")
    if pixel_blob[unpadded:] != b"\x00" * (len(pixel_blob) - unpadded):
        raise RenderSectorError("malformed manifest")
    running = 0
    for texture in textures:
        if texture["pixel_offset"] != running or not _texture_record_ok(texture, pixel_blob, unpadded):
            raise RenderSectorError("malformed manifest")
        texture["pixels"] = pixel_blob[texture["pixel_offset"]:texture["pixel_offset"] + texture["pixel_bytes"]]
        running += texture["pixel_bytes"]
    if running != unpadded:
        raise RenderSectorError("malformed manifest")

    materials = []
    for _ in range(material_count):
        (
            base_texture, base_flags, alpha_cutoff, alpha_mode,
            strength_texture, strength_flags, metallic_factor, roughness_factor,
            spec_color_texture, spec_flags, specular_factor,
            spec_r, spec_g, spec_b, reserved_0, reserved_1,
        ) = MATERIAL_STRUCT.unpack_from(blob, cursor)
        material = {
            "base_texture": base_texture,
            "base_flags": base_flags,
            "alpha_cutoff": alpha_cutoff,
            "alpha_mode": alpha_mode,
            "strength_texture": strength_texture,
            "strength_flags": strength_flags,
            "metallic_factor": metallic_factor,
            "roughness_factor": roughness_factor,
            "spec_color_texture": spec_color_texture,
            "spec_flags": spec_flags,
            "specular_factor": specular_factor,
            "specular_color_factor": (spec_r, spec_g, spec_b),
            "reserved": (reserved_0, reserved_1),
        }
        if not _material_ok(material, textures):
            raise RenderSectorError("malformed manifest")
        materials.append(material)
        cursor += MATERIAL_STRUCT.size

    entries = []
    previous = None
    replica_sum = 0
    observed = 0
    for _ in range(sector_count):
        ix, iz, record_count, uncompressed_bytes, file_bytes, payload_crc, compressed_crc, reserved = ENTRY_STRUCT.unpack_from(blob, cursor)
        key = (ix, iz)
        if previous is not None and key <= previous:
            raise RenderSectorError("manifest order")
        if reserved != 0 or not 1 <= record_count <= PER_SECTOR_CAP:
            raise RenderSectorError("malformed manifest")
        if record_count > local_sources:
            raise RenderSectorError("manifest coverage")
        if uncompressed_bytes != record_count * FACE_BYTES:
            raise RenderSectorError("malformed manifest")
        compressed_bytes = file_bytes - SECTOR_HEADER_STRUCT.size - 8
        if compressed_bytes < 8 or compressed_bytes > uncompressed_bytes + COMPRESS_SLACK:
            raise RenderSectorError("malformed manifest")
        entries.append({
            "ix": ix,
            "iz": iz,
            "record_count": record_count,
            "uncompressed_bytes": uncompressed_bytes,
            "file_bytes": file_bytes,
            "payload_crc": payload_crc,
            "compressed_crc": compressed_crc,
        })
        replica_sum += record_count
        observed = max(observed, record_count)
        previous = key
        cursor += ENTRY_STRUCT.size
    if cursor != HEADER_SIZE + tables_bytes:
        raise RenderSectorError("malformed manifest")
    if replica_sum != header["replica_count"]:
        raise RenderSectorError("malformed manifest")
    if sector_count == 0:
        if header["replica_count"] != 0 or header["observed_max_sector_records"] != 0:
            raise RenderSectorError("malformed manifest")
        if any(header[name] != 0 for name in ("ix_min", "ix_max", "iz_min", "iz_max")):
            raise RenderSectorError("malformed manifest")
    else:
        ixs = [entry["ix"] for entry in entries]
        izs = [entry["iz"] for entry in entries]
        if min(ixs) != header["ix_min"] or max(ixs) != header["ix_max"]:
            raise RenderSectorError("malformed manifest")
        if min(izs) != header["iz_min"] or max(izs) != header["iz_max"]:
            raise RenderSectorError("malformed manifest")
        if observed != header["observed_max_sector_records"]:
            raise RenderSectorError("malformed manifest")
    expected_package = len(blob) + header["global_file_bytes"] + sum(entry["file_bytes"] for entry in entries)
    if header["package_bytes"] != expected_package:
        raise RenderSectorError("malformed manifest")
    header["textures"] = textures
    header["materials"] = materials
    header["entries"] = entries
    header["bounds_min"] = (header["bounds_min_x"], header["bounds_min_y"], header["bounds_min_z"])
    header["bounds_max"] = (header["bounds_max_x"], header["bounds_max_y"], header["bounds_max_z"])
    return header


def _summary(parts, package_bytes, sectors):
    native_files = ["global.drs", "manifest.drm"]
    native_files.extend(f"sectors/{ix}_{iz}.drs" for ix, iz in sorted(sectors))
    bounds_min, bounds_max = parts["bounds_min"], parts["bounds_max"]
    textures = parts["textures"]
    return {
        "format": "DRM2",
        "version": FORMAT_VERSION,
        "experimental": True,
        "public_abi": False,
        "host_budget_is_native_fit": False,
        "source_sha256": parts["source_sha256"].hex(),
        "source_count": parts["source_count"],
        "local_sector_count": len(sectors),
        "replica_count": parts["replica_count"],
        "global_count": parts["global_count"],
        "material_count": len(parts["materials"]),
        "texture_count": len(textures),
        "argb1555_textures": sum(1 for item in textures if item["format"] == TEXTURE_ARGB1555),
        "strength_textures": sum(1 for item in textures if item["format"] == TEXTURE_STRENGTH_U8),
        "texture_bytes": parts["texture_bytes"],
        "package_bytes": package_bytes,
        "cell_size": parts["cell_size"],
        "texture_size": parts["texture_size"],
        "face_bytes": FACE_BYTES,
        "vertex_bytes": VERTEX_BYTES,
        "material_bytes": MATERIAL_BYTES,
        "raw_sector_bytes": PER_SECTOR_CAP * FACE_BYTES,
        "normal_encoding": NORMAL_WORLD_UNIT,
        "bounds_min": list(bounds_min),
        "bounds_max": list(bounds_max),
        "observed_max_sector_records": parts["observed_max_sector_records"],
        "window_unique_cap": WINDOW_UNIQUE_CAP,
        "accessor_decodes": parts["accessor_decodes"],
        "position_decodes": parts["position_decodes"],
        "position_transforms": parts["position_transforms"],
        "normal_decodes": parts["normal_decodes"],
        "normal_transforms": parts["normal_transforms"],
        "authored_normal_faces": parts["authored_normal_faces"],
        "flat_normal_faces": parts["flat_normal_faces"],
        "native_files": native_files,
        "host_files": ["report.json"],
        "caps": {
            "source": MAX_SOURCE,
            "sectors": MAX_SECTORS,
            "replicas": MAX_TOTAL_REPLICAS,
            "per_sector": PER_SECTOR_CAP,
            "global": GLOBAL_CAP,
            "window_unique": WINDOW_UNIQUE_CAP,
            "replicas_per_triangle": MAX_REPLICAS_PER_TRIANGLE,
            "materials": MAX_MATERIALS,
            "textures": MAX_TEXTURES,
            "texture_bytes": MAX_TEXTURE_BYTES,
            "package_bytes": MAX_PACKAGE_BYTES,
            "axis_span_m": MAX_AXIS_SPAN_M,
            "cell_size_m": [MIN_CELL_SIZE_M, MAX_CELL_SIZE_M],
            "texture_size": [MIN_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE],
            "raw_sector_bytes": PER_SECTOR_CAP * FACE_BYTES,
        },
        "lod_policy": "source triangles are preserved whole; this tool does not swap LODs or simplify",
        "uv_policy": "accessor UV stored as float32; no extra V flip",
        "position_policy": "absolute world meters, float32, not recentered",
        "normal_policy": "world-space unit or zero; inverse-transpose when NORMAL is present; flat wound normal when absent",
        "material_policy": "base, reflection strength, and specular color stay in the package; factors keep glTF defaults",
        "gpu_policy": "no GPU or shader implementation in this package",
    }


def _coverage(source_count, sectors, global_sector):
    local_ids = set()
    replica_count = 0
    for blob in sectors.values():
        parsed = parse_sector(blob)
        replica_count += parsed["record_count"]
        for face in parsed["records"]:
            local_ids.add(face["source_id"])
    global_parsed = parse_sector(global_sector)
    global_ids = {face["source_id"] for face in global_parsed["records"]}
    if local_ids & global_ids:
        raise RenderSectorError("triangle stored in both global and local")
    if local_ids | global_ids != set(range(source_count)):
        raise RenderSectorError("missing source id")
    if global_parsed["record_count"] != len(global_ids):
        raise RenderSectorError("missing source id")
    return replica_count, global_parsed["record_count"]


def link_package(parts, sectors, global_sector):
    """Build manifest bytes from finished sector files. Does not touch disk.

    Cross-sector payload equality is intentionally not checked here. The window
    oracle rejects a resigned replica whose bytes differ.
    """
    if not isinstance(parts["source_sha256"], (bytes, bytearray)) or len(parts["source_sha256"]) != 32:
        raise RenderSectorError("source sha256 must be 32 bytes")
    sector_items = sorted(sectors.items())
    linked_sectors = {}
    entries = []
    observed = 0
    for (ix, iz), blob in sector_items:
        parsed = parse_sector(blob)
        if parsed["is_global"] or parsed["ix"] != ix or parsed["iz"] != iz:
            raise RenderSectorError("sector index mismatch")
        if parsed["record_count"] > PER_SECTOR_CAP:
            raise RenderSectorError("sector face cap exceeded")
        linked_sectors[(ix, iz)] = bytes(blob)
        observed = max(observed, parsed["record_count"])
        entries.append({
            "ix": ix,
            "iz": iz,
            "record_count": parsed["record_count"],
            "uncompressed_bytes": parsed["uncompressed_bytes"],
            "file_bytes": len(blob),
            "payload_crc": parsed["payload_crc"],
            "compressed_crc": parsed["compressed_crc"],
        })
    global_parsed = parse_sector(global_sector)
    if not global_parsed["is_global"]:
        raise RenderSectorError("malformed sector")
    replica_count, global_count = _coverage(parts["source_count"], linked_sectors, global_sector)
    if replica_count > MAX_TOTAL_REPLICAS:
        raise RenderSectorError("replica cap exceeded")
    if len(linked_sectors) > MAX_SECTORS:
        raise RenderSectorError("sector cap exceeded")
    if global_count > GLOBAL_CAP:
        raise RenderSectorError("global cap exceeded")
    tables, texture_blob_bytes, texture_bytes = _build_tables(parts["textures"], parts["materials"], entries)
    if texture_bytes != sum(len(texture["pixels"]) for texture in parts["textures"]):
        raise RenderSectorError("texture budget exceeded")
    if texture_bytes > MAX_TEXTURE_BYTES:
        raise RenderSectorError("texture budget exceeded")
    manifest_len = HEADER_SIZE + len(tables) + 4
    package_bytes = manifest_len + len(global_sector) + sum(len(blob) for blob in linked_sectors.values())
    if package_bytes > MAX_PACKAGE_BYTES:
        raise RenderSectorError("package budget exceeded")
    if len(linked_sectors) == 0:
        ix_min = ix_max = iz_min = iz_max = 0
    else:
        ixs = [ix for ix, _iz in linked_sectors]
        izs = [iz for _ix, iz in linked_sectors]
        ix_min, ix_max = min(ixs), max(ixs)
        iz_min, iz_max = min(izs), max(izs)
    values = {
        "magic": MAGIC_MANIFEST,
        "version": FORMAT_VERSION,
        "header_bytes": HEADER_SIZE,
        "source_count": parts["source_count"],
        "local_sector_count": len(linked_sectors),
        "replica_count": replica_count,
        "global_count": global_count,
        "material_count": len(parts["materials"]),
        "texture_count": len(parts["textures"]),
        "texture_bytes": texture_bytes,
        "package_bytes": package_bytes,
        "cell_size": parts["cell_size"],
        "ix_min": ix_min,
        "ix_max": ix_max,
        "iz_min": iz_min,
        "iz_max": iz_max,
        "bounds_min_x": parts["bounds_min"][0],
        "bounds_min_y": parts["bounds_min"][1],
        "bounds_min_z": parts["bounds_min"][2],
        "bounds_max_x": parts["bounds_max"][0],
        "bounds_max_y": parts["bounds_max"][1],
        "bounds_max_z": parts["bounds_max"][2],
        "global_uncompressed": global_parsed["uncompressed_bytes"],
        "global_file_bytes": len(global_sector),
        "global_payload_crc": global_parsed["payload_crc"],
        "global_compressed_crc": global_parsed["compressed_crc"],
        "source_sha256": bytes(parts["source_sha256"]),
        "texture_blob_bytes": texture_blob_bytes,
        "tables_bytes": len(tables),
        "observed_max_sector_records": observed,
    }
    values.update(contract_constants())
    body = _pack_header(values) + tables
    manifest = body + struct.pack("<I", crc32(body))
    parsed = parse_manifest(manifest)
    if parsed["package_bytes"] != package_bytes or parsed["source_sha256"] != bytes(parts["source_sha256"]):
        raise RenderSectorError("manifest count mismatch")
    if parsed["global_payload_crc"] != global_parsed["payload_crc"]:
        raise RenderSectorError("sector crc mismatch")
    stored = dict(parts)
    stored["replica_count"] = replica_count
    stored["global_count"] = global_count
    stored["texture_bytes"] = texture_bytes
    stored["observed_max_sector_records"] = observed
    summary = _summary(stored, package_bytes, linked_sectors)
    report = (json.dumps(summary, indent=2, sort_keys=True) + "\n").encode("utf-8")
    return RenderPackage(manifest, linked_sectors, global_sector, report, summary, stored)


def relink_package(package, sectors=None, global_sector=None):
    """Resign a package after a test replaces sector bytes. Not a disc writer."""
    return link_package(
        package._parts,
        package.sectors if sectors is None else sectors,
        package.global_sector if global_sector is None else global_sector,
    )


def _unit_scalar(value, message):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RenderSectorError(message)
    number = float(value)
    if not math.isfinite(number) or number < 0.0 or number > 1.0:
        raise RenderSectorError(message)
    return f32(number)


def _unit_rgb(value, message):
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise RenderSectorError(message)
    return (
        _unit_scalar(value[0], message),
        _unit_scalar(value[1], message),
        _unit_scalar(value[2], message),
    )


def _wrap_flags(sampler):
    flags = 0
    mag = sampler.get("magFilter", 9729)
    min_filter = sampler.get("minFilter", mag)
    # PVR's single base-level filter can represent matched nearest or linear.
    # Mipmapped or independent min/mag modes need another native path.
    if mag not in (9728, 9729) or min_filter != mag:
        raise RenderSectorError("unsupported sampler filter")
    if mag == 9728:
        flags |= FLAG_NEAREST
    for axis, clamp_bit, mirror_bit in (("S", FLAG_CLAMP_S, FLAG_MIRROR_S), ("T", FLAG_CLAMP_T, FLAG_MIRROR_T)):
        wrap = sampler.get("wrap" + axis, 10497)
        if wrap == 33071:
            flags |= clamp_bit
        elif wrap == 33648:
            flags |= mirror_bit
        elif wrap != 10497:
            raise RenderSectorError("unsupported sampler wrap")
    return flags


def _texture_ref(doc, info):
    if not isinstance(info, dict) or set(info) - {"index", "texCoord"}:
        raise RenderSectorError("unsupported texture coordinates/transform")
    if info.get("texCoord", 0) != 0:
        raise RenderSectorError("unsupported texture coordinates/transform")
    textures = doc.get("textures", [])
    tex_index = info.get("index")
    if isinstance(tex_index, bool) or not isinstance(tex_index, int) or not 0 <= tex_index < len(textures):
        raise RenderSectorError("texture index out of range")
    texture = textures[tex_index]
    if not isinstance(texture, dict):
        raise RenderSectorError("unsupported extensions")
    if texture.get("extensions") or set(texture) - {"source", "sampler", "name"}:
        raise RenderSectorError("unsupported extensions")
    image_id = texture.get("source")
    images = doc.get("images", [])
    if isinstance(image_id, bool) or not isinstance(image_id, int) or not 0 <= image_id < len(images):
        raise RenderSectorError("image index out of range")
    sampler = {}
    if "sampler" in texture:
        samplers = doc.get("samplers", [])
        sampler_index = texture["sampler"]
        if isinstance(sampler_index, bool) or not isinstance(sampler_index, int) or not 0 <= sampler_index < len(samplers):
            raise RenderSectorError("sampler index out of range")
        sampler = samplers[sampler_index] or {}
        if not isinstance(sampler, dict):
            raise RenderSectorError("unsupported sampler wrap")
        if sampler.get("extensions") or set(sampler) - {"wrapS", "wrapT", "magFilter", "minFilter", "name"}:
            raise RenderSectorError("unsupported sampler wrap")
    return image_id, _wrap_flags(sampler)


def _register_texture(state, key, width, height, pixels, tex_format):
    found = state["texture_map"].get(key)
    if found is not None:
        return found
    if len(state["textures"]) >= MAX_TEXTURES:
        raise RenderSectorError("texture cap exceeded")
    if sum(len(item["pixels"]) for item in state["textures"]) + len(pixels) > MAX_TEXTURE_BYTES:
        raise RenderSectorError("texture budget exceeded")
    index = len(state["textures"])
    state["texture_map"][key] = index
    state["textures"].append({
        "width": width,
        "height": height,
        "pixels": pixels,
        "format": tex_format,
    })
    return index


def _consume_primitive(doc, binary, world, primitive, texture_size, state):
    if not isinstance(primitive, dict):
        raise RenderSectorError("malformed primitive")
    if primitive.get("extensions"):
        raise RenderSectorError("unsupported extensions")
    if "targets" in primitive:
        raise RenderSectorError("morph targets are unsupported")
    if primitive.get("mode", 4) != 4:
        raise RenderSectorError("unsupported primitive mode")
    attributes = primitive.get("attributes")
    if not isinstance(attributes, dict) or "POSITION" not in attributes:
        raise RenderSectorError("POSITION required")
    if "JOINTS_0" in attributes or "WEIGHTS_0" in attributes:
        raise RenderSectorError("skinned geometry is unsupported")
    unknown = set(attributes) - ALLOWED_ATTRIBUTES
    if unknown:
        raise RenderSectorError("unsupported vertex attribute")
    pos_index = attributes["POSITION"]
    positions = state["world_positions"](world, pos_index)
    if "TEXCOORD_0" in attributes:
        uvs = state["cached"](attributes["TEXCOORD_0"])
    else:
        uvs = [(0.0, 0.0)] * len(positions)
    if "COLOR_0" in attributes:
        colors = state["cached"](attributes["COLOR_0"])
    else:
        colors = [(1.0, 1.0, 1.0, 1.0)] * len(positions)
    if len(uvs) != len(positions) or len(colors) != len(positions):
        raise RenderSectorError("attribute count mismatch")
    authored = None
    if "NORMAL" in attributes:
        authored = state["world_normals"](world, attributes["NORMAL"])
        if len(authored) != len(positions):
            raise RenderSectorError("attribute count mismatch")
    if "indices" in primitive:
        selected = state["cached"](primitive["indices"])
    else:
        selected = [(i,) for i in range(len(positions))]
    if len(selected) % 3:
        raise RenderSectorError("invalid triangle indices")
    for entry in selected:
        if (
            len(entry) != 1
            or isinstance(entry[0], bool)
            or not isinstance(entry[0], int)
            or entry[0] < 0
            or entry[0] >= len(positions)
        ):
            raise RenderSectorError("invalid triangle indices")
    material_id = state["material_for"](primitive)
    for start in range(0, len(selected), 3):
        if len(state["faces"]) >= MAX_SOURCE:
            raise RenderSectorError("source cap exceeded")
        corners = []
        for entry in selected[start:start + 3]:
            index = entry[0]
            xyz = positions[index]
            uv = uvs[index]
            color = colors[index]
            if len(xyz) != 3 or len(uv) != 2 or len(color) not in (3, 4):
                raise RenderSectorError("invalid vertex attribute width")
            if any(not math.isfinite(value) or abs(value) > UV_ABS_MAX for value in uv):
                raise RenderSectorError("invalid position/UV")
            u = f32(uv[0])
            v = f32(uv[1])
            if not math.isfinite(u) or not math.isfinite(v) or abs(u) > UV_ABS_MAX or abs(v) > UV_ABS_MAX:
                raise RenderSectorError("invalid position/UV")
            rgba_in = list(color) + [1.0] * (4 - len(color))
            factor = state["factor"]
            rgba = [round(max(0.0, min(1.0, rgba_in[channel] * factor[channel])) * 255) for channel in range(4)]
            argb = (rgba[3] << 24) | (rgba[0] << 16) | (rgba[1] << 8) | rgba[2]
            corners.append((xyz, u, v, argb, index))
        if authored is None:
            normal = _face_normal(corners[0][0], corners[1][0], corners[2][0])
            normals = (normal, normal, normal)
            state["flat_normal_faces"] += 1
        else:
            normals = tuple(authored[corner[4]] for corner in corners)
            state["authored_normal_faces"] += 1
        verts = []
        for (xyz, u, v, argb, _index), normal in zip(corners, normals):
            verts.append((xyz[0], xyz[1], xyz[2], u, v, normal[0], normal[1], normal[2], argb))
        state["faces"].append((material_id, tuple(verts)))


def _material_for(doc, binary, primitive, texture_size, state):
    material = {}
    if "material" in primitive:
        materials = doc.get("materials", [])
        index = primitive["material"]
        if isinstance(index, bool) or not isinstance(index, int) or not 0 <= index < len(materials):
            raise RenderSectorError("material index out of range")
        material = materials[index]
        if not isinstance(material, dict):
            raise RenderSectorError("malformed material")
    for key in ("normalTexture", "occlusionTexture", "emissiveTexture"):
        if key in material:
            raise RenderSectorError("unsupported " + key)
    if set(material) - MATERIAL_KEYS:
        raise RenderSectorError("unsupported material field")
    if "doubleSided" in material and not isinstance(material["doubleSided"], bool):
        raise RenderSectorError("invalid doubleSided")
    if "emissiveFactor" in material:
        emissive = material["emissiveFactor"]
        if (
            not isinstance(emissive, (list, tuple))
            or len(emissive) != 3
            or any(isinstance(value, bool) or not isinstance(value, (int, float)) or float(value) != 0.0 for value in emissive)
        ):
            raise RenderSectorError("unsupported emissiveFactor")
    extensions = material.get("extensions", {})
    if extensions is None:
        extensions = {}
    if not isinstance(extensions, dict):
        raise RenderSectorError("unsupported extensions")
    if set(extensions) - {"KHR_materials_unlit", "KHR_materials_specular"}:
        raise RenderSectorError("unsupported extensions")
    has_unlit = "KHR_materials_unlit" in extensions
    has_spec = "KHR_materials_specular" in extensions
    if has_unlit and has_spec:
        raise RenderSectorError("unlit material cannot carry specular")
    if has_unlit and extensions["KHR_materials_unlit"] not in (None, {}):
        raise RenderSectorError("unsupported extensions")
    mode = material.get("alphaMode", "OPAQUE")
    if mode not in ("OPAQUE", "MASK"):
        raise RenderSectorError("BLEND requires a separate translucent path")
    cutoff = material.get("alphaCutoff", 0.5)
    if isinstance(cutoff, bool) or not isinstance(cutoff, (int, float)):
        raise RenderSectorError("invalid alpha cutoff")
    if not math.isfinite(cutoff) or not 0 <= cutoff <= 1:
        raise RenderSectorError("invalid alpha cutoff")
    cutoff_f = f32(cutoff)
    pbr = material.get("pbrMetallicRoughness", {})
    if pbr is None:
        pbr = {}
    if not isinstance(pbr, dict) or set(pbr) - PBR_KEYS:
        raise RenderSectorError("unsupported material field")
    factor = pbr.get("baseColorFactor", [1, 1, 1, 1])
    if (
        not isinstance(factor, (list, tuple))
        or len(factor) != 4
        or any(isinstance(component, bool) or not isinstance(component, (int, float)) for component in factor)
        or not all(math.isfinite(component) for component in factor)
    ):
        raise RenderSectorError("invalid base color")
    state["factor"] = factor
    if has_unlit and "metallicRoughnessTexture" in pbr:
        raise RenderSectorError("unlit material cannot carry specular")

    image_id = None
    base_wrap = 0
    if "baseColorTexture" in pbr:
        image_id, base_wrap = _texture_ref(doc, pbr["baseColorTexture"])
    # Original Python float, not the float32 bits. Nearby cutoffs that collide
    # in float32 still bake and dedup the way convert_textured does.
    base_key = ("base", image_id, mode, float(cutoff))
    width, height, pixels = texture_pixels(doc, binary, image_id, mode, cutoff, texture_size)
    base_index = _register_texture(state, base_key, width, height, pixels, TEXTURE_ARGB1555)

    metallic = f32(1.0)
    roughness = f32(1.0)
    if "metallicFactor" in pbr:
        metallic = _unit_scalar(pbr["metallicFactor"], "invalid metallic factor")
    if "roughnessFactor" in pbr:
        roughness = _unit_scalar(pbr["roughnessFactor"], "invalid roughness factor")
    strength_index = ABSENT_TEXTURE
    strength_flags = 0
    if "metallicRoughnessTexture" in pbr:
        strength_image, strength_flags = _texture_ref(doc, pbr["metallicRoughnessTexture"])
        width, height, pixels = strength_pixels(doc, binary, strength_image, texture_size)
        strength_index = _register_texture(
            state, ("strength", strength_image), width, height, pixels, TEXTURE_STRENGTH_U8
        )

    spec_index = ABSENT_TEXTURE
    spec_flags = 0
    specular_factor = f32(1.0)
    spec_color = (f32(1.0), f32(1.0), f32(1.0))
    if has_spec:
        spec = extensions["KHR_materials_specular"]
        if not isinstance(spec, dict):
            raise RenderSectorError("unsupported extensions")
        if "specularTexture" in spec:
            raise RenderSectorError("unsupported specularTexture")
        if set(spec) - SPECULAR_KEYS:
            raise RenderSectorError("unsupported extensions")
        if "specularFactor" in spec:
            specular_factor = _unit_scalar(spec["specularFactor"], "invalid specular factor")
        if "specularColorFactor" in spec:
            spec_color = _unit_rgb(spec["specularColorFactor"], "invalid specular color factor")
        if "specularColorTexture" in spec:
            spec_image, spec_flags = _texture_ref(doc, spec["specularColorTexture"])
            width, height, pixels = texture_pixels(doc, binary, spec_image, "OPAQUE", 1.0, texture_size)
            spec_index = _register_texture(
                state, ("spec", spec_image), width, height, pixels, TEXTURE_ARGB1555
            )

    flags = (FLAG_MASK if mode == "MASK" else 0) | base_wrap
    if has_unlit:
        flags |= FLAG_UNLIT
    if material.get("doubleSided", False):
        flags |= FLAG_DOUBLE_SIDED
    material_key = (
        base_index,
        flags,
        cutoff_f,
        1 if mode == "MASK" else 0,
        strength_index,
        strength_flags,
        metallic,
        roughness,
        spec_index,
        spec_flags,
        specular_factor,
        spec_color,
    )
    if material_key not in state["material_map"]:
        if len(state["materials"]) >= MAX_MATERIALS:
            raise RenderSectorError("material cap exceeded")
        state["material_map"][material_key] = len(state["materials"])
        state["materials"].append({
            "base_texture": base_index,
            "base_flags": flags,
            "alpha_cutoff": cutoff_f,
            "alpha_mode": 1 if mode == "MASK" else 0,
            "strength_texture": strength_index,
            "strength_flags": strength_flags,
            "metallic_factor": metallic,
            "roughness_factor": roughness,
            "spec_color_texture": spec_index,
            "spec_flags": spec_flags,
            "specular_factor": specular_factor,
            "specular_color_factor": spec_color,
        })
    return state["material_map"][material_key]


def extract_scene(doc, binary, texture_size):
    """Active-scene instances in walk order. Accessor and transform caches are local."""
    if not isinstance(doc, dict):
        raise RenderSectorError("malformed GLB")
    required = doc.get("extensionsRequired", [])
    if not isinstance(required, list) or set(required) - ALLOWED_REQUIRED:
        raise RenderSectorError("unsupported extensions")
    scenes = doc.get("scenes")
    if not isinstance(scenes, list) or not scenes:
        raise RenderSectorError("no selected triangles")
    scene_index = doc.get("scene", 0)
    if isinstance(scene_index, bool) or not isinstance(scene_index, int) or not 0 <= scene_index < len(scenes):
        raise RenderSectorError("active scene is invalid")
    scene = scenes[scene_index]
    if not isinstance(scene, dict) or not isinstance(scene.get("nodes", []), list):
        raise RenderSectorError("active scene is invalid")
    nodes = doc.get("nodes", [])
    if not isinstance(nodes, list):
        raise RenderSectorError("malformed GLB")
    state = {
        "cache": {},
        "transformed": {},
        "normal_cache": {},
        "inverse_cache": {},
        "faces": [],
        "textures": [],
        "texture_map": {},
        "materials": [],
        "material_map": {},
        "accessor_decodes": 0,
        "position_decodes": 0,
        "position_transforms": 0,
        "normal_decodes": 0,
        "normal_transforms": 0,
        "authored_normal_faces": 0,
        "flat_normal_faces": 0,
        "factor": [1.0, 1.0, 1.0, 1.0],
    }

    def cached(index):
        if isinstance(index, bool) or not isinstance(index, int) or index < 0:
            raise RenderSectorError("invalid accessor index")
        if index not in state["cache"]:
            state["accessor_decodes"] += 1
            state["cache"][index] = accessor(doc, binary, index)
        return state["cache"][index]

    def world_positions(matrix, pos_index):
        if pos_index not in state["cache"]:
            state["position_decodes"] += 1
        raw_positions = cached(pos_index)
        key = (tuple(matrix), pos_index)
        hit = state["transformed"].get(key)
        if hit is None:
            state["position_transforms"] += 1
            cooked = []
            for item in raw_positions:
                if len(item) != 3:
                    raise RenderSectorError("POSITION must be VEC3")
                point = transform(matrix, item)
                xyz = (f32(point[0]), f32(point[1]), f32(point[2]))
                if not all(math.isfinite(value) for value in xyz):
                    raise RenderSectorError("non-finite transformed position")
                if any(abs(value) > WORLD_ABS_MAX for value in xyz):
                    raise RenderSectorError("coordinate exceeds world bound")
                cooked.append(xyz)
            hit = tuple(cooked)
            state["transformed"][key] = hit
        return hit

    def world_normals(matrix, normal_index):
        if normal_index not in state["cache"]:
            state["normal_decodes"] += 1
        raw_normals = cached(normal_index)
        key = (tuple(matrix), normal_index)
        hit = state["normal_cache"].get(key)
        if hit is None:
            matrix_key = tuple(matrix)
            inv_t = state["inverse_cache"].get(matrix_key)
            if inv_t is None:
                inv_t = _inverse_transpose_3x3(matrix)
                state["inverse_cache"][matrix_key] = inv_t
            state["normal_transforms"] += 1
            cooked = []
            for item in raw_normals:
                if len(item) != 3:
                    raise RenderSectorError("NORMAL must be VEC3")
                cooked.append(_pack_unit_normal(*_transform_normal(inv_t, item)))
            hit = tuple(cooked)
            state["normal_cache"][key] = hit
        return hit

    state["cached"] = cached
    state["world_positions"] = world_positions
    state["world_normals"] = world_normals
    state["material_for"] = lambda primitive: _material_for(doc, binary, primitive, texture_size, state)

    def walk(index, parent, ancestors):
        if isinstance(index, bool) or not isinstance(index, int) or not 0 <= index < len(nodes):
            raise RenderSectorError("node index out of range")
        if index in ancestors:
            raise RenderSectorError("cycle in node graph")
        node = nodes[index]
        if not isinstance(node, dict):
            raise RenderSectorError("malformed node")
        if node.get("extensions"):
            raise RenderSectorError("unsupported extensions")
        if "skin" in node:
            raise RenderSectorError("skinned geometry is unsupported")
        if "matrix" in node and any(key in node for key in ("translation", "rotation", "scale")):
            raise RenderSectorError("ambiguous matrix and TRS")
        world = multiply(parent, node_matrix(node))
        mesh_id = node.get("mesh")
        if mesh_id is not None:
            meshes = doc.get("meshes", [])
            if isinstance(mesh_id, bool) or not isinstance(mesh_id, int) or not 0 <= mesh_id < len(meshes):
                raise RenderSectorError("mesh index outside mesh array")
            mesh = meshes[mesh_id]
            if not isinstance(mesh, dict):
                raise RenderSectorError("malformed mesh")
            if mesh.get("extensions"):
                raise RenderSectorError("unsupported extensions")
            if "targets" in mesh:
                raise RenderSectorError("morph targets are unsupported")
            for primitive in mesh.get("primitives", []):
                _consume_primitive(doc, binary, world, primitive, texture_size, state)
        children = node.get("children", [])
        if not isinstance(children, list):
            raise RenderSectorError("malformed node")
        for child in children:
            walk(child, world, ancestors | {index})

    for root in scene.get("nodes", []):
        walk(root, IDENTITY, set())
    if not state["faces"]:
        raise RenderSectorError("no selected triangles")
    if len(state["faces"]) > MAX_SOURCE:
        raise RenderSectorError("source cap exceeded")
    if not state["materials"]:
        raise RenderSectorError("malformed material")
    return state


def _scene_bounds(faces):
    xs, ys, zs = [], [], []
    for _material, vertices in faces:
        for x, y, z, _u, _v, _nx, _ny, _nz, _argb in vertices:
            xs.append(x)
            ys.append(y)
            zs.append(z)
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def partition_faces(faces, cell_size):
    buckets = {}
    global_records = []
    replica_count = 0
    for source_id, (material_id, vertices) in enumerate(faces):
        raw = pack_face(source_id, material_id, vertices)
        xs = (vertices[0][0], vertices[1][0], vertices[2][0])
        ys = (vertices[0][1], vertices[1][1], vertices[2][1])
        zs = (vertices[0][2], vertices[1][2], vertices[2][2])
        cells = classify_triangle(xs, ys, zs, cell_size)
        if cells is None:
            if len(global_records) >= GLOBAL_CAP:
                raise RenderSectorError("global cap exceeded")
            global_records.append(raw)
            continue
        replica_count += len(cells)
        if replica_count > MAX_TOTAL_REPLICAS:
            raise RenderSectorError("replica cap exceeded")
        for key in cells:
            bucket = buckets.get(key)
            if bucket is None:
                if len(buckets) >= MAX_SECTORS:
                    raise RenderSectorError("sector cap exceeded")
                bucket = []
                buckets[key] = bucket
            if len(bucket) >= PER_SECTOR_CAP:
                raise RenderSectorError("sector face cap exceeded")
            bucket.append(raw)
    for key in buckets:
        buckets[key].sort(key=lambda item: struct.unpack_from("<I", item)[0])
    global_records.sort(key=lambda item: struct.unpack_from("<I", item)[0])
    return buckets, global_records, replica_count


def build_package(doc, binary, cell_size=DEFAULT_CELL_SIZE_M, texture_size=DEFAULT_TEXTURE_SIZE, source_sha256=None):
    """Validate a parsed GLB and return an in-memory package. No filesystem writes."""
    cell = quantize_cell_size(cell_size)
    size = quantize_texture_size(texture_size)
    if not isinstance(binary, (bytes, bytearray)):
        raise RenderSectorError("malformed GLB")
    binary = bytes(binary)
    if source_sha256 is None:
        source_sha256 = hashlib.sha256(binary).digest()
    else:
        source_sha256 = bytes(source_sha256)
        if len(source_sha256) != 32:
            raise RenderSectorError("source sha256 must be 32 bytes")
    try:
        state = extract_scene(doc, binary, size)
        faces = state["faces"]
        buckets, global_records, _replica_count = partition_faces(faces, cell)
        sectors = {}
        for (ix, iz), records in buckets.items():
            sectors[(ix, iz)] = encode_sector(records, ix, iz, is_global=False)
        global_sector = encode_sector(global_records, is_global=True)
        bounds_min, bounds_max = _scene_bounds(faces)
        parts = {
            "source_count": len(faces),
            "textures": state["textures"],
            "materials": state["materials"],
            "bounds_min": bounds_min,
            "bounds_max": bounds_max,
            "cell_size": cell,
            "texture_size": size,
            "source_sha256": source_sha256,
            "accessor_decodes": state["accessor_decodes"],
            "position_decodes": state["position_decodes"],
            "position_transforms": state["position_transforms"],
            "normal_decodes": state["normal_decodes"],
            "normal_transforms": state["normal_transforms"],
            "authored_normal_faces": state["authored_normal_faces"],
            "flat_normal_faces": state["flat_normal_faces"],
            "texture_bytes": sum(len(item["pixels"]) for item in state["textures"]),
            "replica_count": 0,
            "global_count": len(global_records),
            "observed_max_sector_records": 0,
        }
        return link_package(parts, sectors, global_sector)
    except RenderSectorError:
        raise
    except (ValueError, KeyError, IndexError, TypeError, OSError, struct.error, zlib.error) as error:
        raise RenderSectorError(str(error)) from error


def _query_bounds(bounds):
    if not isinstance(bounds, (tuple, list)) or len(bounds) != 4:
        raise RenderSectorError("query")
    if any(isinstance(value, bool) or not isinstance(value, (int, float)) for value in bounds):
        raise RenderSectorError("query")
    min_x, max_x, min_z, max_z = (float(value) for value in bounds)
    if not all(math.isfinite(value) for value in (min_x, max_x, min_z, max_z)):
        raise RenderSectorError("query")
    if min_x > max_x or min_z > max_z:
        raise RenderSectorError("query")
    return min_x, max_x, min_z, max_z


def _face_overlaps_xz(face, bounds):
    min_x, max_x, min_z, max_z = bounds
    xs = [vertex[0] for vertex in face["vertices"]]
    zs = [vertex[2] for vertex in face["vertices"]]
    return _interval_overlap(min(xs), max(xs), min_x, max_x) and _interval_overlap(min(zs), max(zs), min_z, max_z)


def _dedup_faces(records, bounds=None):
    found = {}
    for face in records:
        source_id = face["source_id"]
        if not isinstance(source_id, int) or isinstance(source_id, bool):
            raise RenderSectorError("source id out of range")
        previous = found.get(source_id)
        if previous is None:
            if bounds is None and len(found) >= WINDOW_UNIQUE_CAP:
                raise RenderSectorError("unique faces exceed window cap")
            found[source_id] = face["raw"]
        elif previous != face["raw"]:
            raise RenderSectorError("replica mismatch")
    result = [unpack_face(found[source_id]) for source_id in sorted(found)]
    if bounds is not None:
        result = [face for face in result if _face_overlaps_xz(face, bounds)]
    if len(result) > WINDOW_UNIQUE_CAP:
        raise RenderSectorError("unique faces exceed window cap")
    return result


def assemble_window(package, bounds):
    """Whole faces for a closed XZ window, source-id order.

    Every global record is read, and every record in a local cell whose
    rectangle touches the query is read. Those payloads are compared first,
    including a face the bbox will drop. The XZ AABB filter then runs on the
    local and global faces together. The 8192 cap is the filtered count.
    A face that passes is stored whole. This host dict is the oracle; a later
    native loader can use two passes and must still compare before it drops.
    """
    header = parse_manifest(package.manifest)
    window = _query_bounds(bounds)
    global_parsed = parse_sector(package.global_sector)
    if (
        not global_parsed["is_global"]
        or global_parsed["record_count"] != header["global_count"]
        or global_parsed["payload_crc"] != header["global_payload_crc"]
        or global_parsed["compressed_crc"] != header["global_compressed_crc"]
        or len(package.global_sector) != header["global_file_bytes"]
    ):
        raise RenderSectorError("sector crc mismatch")
    entry_map = {(entry["ix"], entry["iz"]): entry for entry in header["entries"]}
    if set(entry_map) != set(package.sectors):
        raise RenderSectorError("malformed package")
    collected = []
    global_ids = set()
    for face in global_parsed["records"]:
        if face["source_id"] >= header["source_count"] or face["material_id"] >= header["material_count"]:
            raise RenderSectorError("source id out of range")
        collected.append(face)
        global_ids.add(face["source_id"])
    cell = header["cell_size"]
    for key, entry in entry_map.items():
        ix, iz = key
        x0, x1, z0, z1 = cell_bounds(ix, iz, cell)
        if not (_interval_overlap(x0, x1, window[0], window[1]) and _interval_overlap(z0, z1, window[2], window[3])):
            continue
        blob = package.sectors[key]
        parsed = parse_sector(blob)
        if (
            parsed["is_global"]
            or parsed["ix"] != ix
            or parsed["iz"] != iz
            or parsed["record_count"] != entry["record_count"]
            or parsed["payload_crc"] != entry["payload_crc"]
            or parsed["compressed_crc"] != entry["compressed_crc"]
            or len(blob) != entry["file_bytes"]
        ):
            raise RenderSectorError("sector crc mismatch")
        for face in parsed["records"]:
            if face["source_id"] in global_ids:
                raise RenderSectorError("triangle stored in both global and local")
            if face["source_id"] >= header["source_count"] or face["material_id"] >= header["material_count"]:
                raise RenderSectorError("source id out of range")
            collected.append(face)
    return _dedup_faces(collected, window)


def _is_within(path, root):
    resolved = Path(path).resolve()
    owned = Path(root).resolve()
    if resolved == owned:
        return False
    try:
        resolved.relative_to(owned)
    except ValueError:
        return False
    return True


def assert_contained(path, root):
    """Refuse a cleanup or move target that is the owned root or outside it."""
    if not _is_within(path, root):
        raise RenderSectorError("refusing to delete outside owned root")
    return Path(path).resolve()


def _remove_temp(temp, parent_resolved):
    if not temp.exists():
        return
    resolved = temp.resolve()
    if resolved == parent_resolved or not _is_within(resolved, parent_resolved):
        raise RenderSectorError("refusing to delete outside owned root")
    if not (resolved.name.startswith(".") and resolved.name.endswith(".drm-tmp")):
        raise RenderSectorError("refusing to delete unexpected directory")
    shutil.rmtree(resolved)


def _write_inside(root, relative, blob):
    parts = Path(relative).parts
    if not parts or any(part in ("", ".", "..") for part in parts):
        raise RenderSectorError("package path escaped")
    if len(parts) == 1:
        target = root / parts[0]
        parent = root
    elif len(parts) == 2 and parts[0] == "sectors":
        parent = root / "sectors"
        target = parent / parts[1]
        if parent.exists() and parent.is_symlink():
            raise RenderSectorError("package path escaped")
        parent.mkdir(exist_ok=True)
    else:
        raise RenderSectorError("package path escaped")
    if parent.resolve() != root.resolve() and not _is_within(parent, root):
        raise RenderSectorError("package path escaped")
    partial = target.with_name(target.name + ".partial")
    if partial.exists() or target.exists():
        raise RenderSectorError("package path escaped")
    with partial.open("wb") as handle:
        handle.write(blob)
        handle.flush()
        os.fsync(handle.fileno())
    partial.replace(target)
    if not _is_within(target, root):
        raise RenderSectorError("package path escaped")
    return target


def publish_package(output_dir, package, fail_after=None):
    """Install a new directory as a temp child of the resolved parent, then rename.

    The destination must not exist. The manifest is the last file installed.
    Failure deletes only a temp directory that still resolves inside the parent.
    """
    destination = Path(output_dir)
    if destination.exists():
        raise RenderSectorError("output directory already exists")
    if not destination.parent.is_dir():
        raise RenderSectorError("output parent is missing")
    parent_resolved = destination.parent.resolve()
    final_name = destination.name
    if not final_name or final_name in (".", "..") or any(mark in final_name for mark in ("/", "\\", ":")):
        raise RenderSectorError("output directory name is invalid")
    final = parent_resolved / final_name
    if final.exists() or final.parent.resolve() != parent_resolved:
        raise RenderSectorError("output directory already exists" if final.exists() else "output escaped owned root")
    temp = parent_resolved / f".{final_name}.drm-tmp"
    if temp.parent.resolve() != parent_resolved:
        raise RenderSectorError("temp escaped owned root")
    if temp.exists():
        raise RenderSectorError("temp output already exists")
    files = [(f"sectors/{ix}_{iz}.drs", package.sectors[(ix, iz)]) for ix, iz in sorted(package.sectors)]
    files.append(("global.drs", package.global_sector))
    files.append(("report.json", package.report_bytes))
    files.append(("manifest.drm", package.manifest))
    created = False
    try:
        temp.mkdir()
        created = True
        if temp.resolve().parent != parent_resolved or not _is_within(temp, parent_resolved):
            raise RenderSectorError("temp escaped owned root")
        written = 0
        for relative, blob in files:
            if fail_after is not None and written >= fail_after:
                raise RenderSectorError("write aborted before manifest commit")
            _write_inside(temp, relative, blob)
            written += 1
        if final.exists() or not _is_within(temp, parent_resolved):
            raise RenderSectorError("output escaped owned root")
        temp.rename(final)
        created = False
        if not final.is_dir() or final.resolve().parent != parent_resolved or not _is_within(final, parent_resolved):
            raise RenderSectorError("move escaped owned root")
    except Exception:
        if created:
            _remove_temp(temp, parent_resolved)
        raise
    return final


def prepare(glb_path, output_dir, cell_size=DEFAULT_CELL_SIZE_M, texture_size=DEFAULT_TEXTURE_SIZE, fail_after=None):
    """Read one GLB, hash the file bytes, publish a new directory, return a summary dict.

    The summary and report.json have no filesystem paths. source_sha256 is the
    SHA-256 of the GLB file, not of an extracted buffer and not of a path.
    """
    path = Path(glb_path)
    blob = path.read_bytes()
    digest = hashlib.sha256(blob).digest()
    doc, binary = parse_glb_bytes(blob)
    package = build_package(doc, binary, cell_size, texture_size, digest)
    publish_package(output_dir, package, fail_after=fail_after)
    return package.summary


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Prepare an experimental DRM2 render-sector package from a static GLB. Host only."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cell-size", type=float, default=DEFAULT_CELL_SIZE_M)
    parser.add_argument("--texture-size", type=int, default=DEFAULT_TEXTURE_SIZE)
    parser.add_argument("--zlib-level", type=int, choices=(0, 9), default=9,
                        help="0: stored blocks only, for sector_inflate_stored.c")
    args = parser.parse_args(argv)
    global ZLIB_LEVEL
    ZLIB_LEVEL = args.zlib_level
    try:
        summary = prepare(args.input, args.output, args.cell_size, args.texture_size)
    except (RenderSectorError, OSError) as error:
        print(f"render-sectors: {error}", file=sys.stderr)
        return 1
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
