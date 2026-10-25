"""Synthetic Grok audit regressions, independently verified by Codex (AI-assisted), ISC.

Codex executes these. They were not run here. They import the copied
converter and the synthetic GLB helpers in test_render_sectors.
"""

from __future__ import annotations

import base64
import json
import struct
import unittest

import prepare_render_sectors as rs
from test_render_sectors import GlbBuilder, _interior, _package, _png_uri


def _resign(manifest):
    body = bytes(manifest[:-4])
    return body + struct.pack("<I", rs.crc32(body))


def _patch_header(manifest, **fields):
    blob = bytearray(manifest)
    for name, value in fields.items():
        struct.pack_into("<I", blob, rs.HEADER_OFFSETS[name], value)
    return _resign(blob)


def _entry_offset(header):
    return (
        rs.HEADER_SIZE
        + header["texture_count"] * rs.TEXTURE_STRUCT.size
        + header["texture_blob_bytes"]
        + header["material_count"] * rs.MATERIAL_STRUCT.size
    )


def _png_bytes():
    uri = _png_uri((255, 0, 0, 255), 8)
    return base64.b64decode(uri.split(",", 1)[1])


def _minimal_glb(byte_length, payload=b"\x00"):
    doc = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": byte_length}]}
    json_bytes = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    json_bytes += b" " * ((4 - len(json_bytes) % 4) % 4)
    raw = bytes(payload)
    padded = raw + b"\x00" * ((4 - len(raw) % 4) % 4)
    total = 12 + 8 + len(json_bytes) + 8 + len(padded)
    out = struct.pack("<4sII", b"glTF", 2, total)
    out += struct.pack("<II", len(json_bytes), 0x4E4F534A) + json_bytes
    out += struct.pack("<II", len(padded), 0x004E4942) + padded
    return out


def _triangle_with_png(mutate):
    """One triangle plus a PNG in the last bufferView. mutate edits that view."""
    builder = GlbBuilder()
    positions = [(1.0, 0.0, 1.0), (2.0, 0.0, 1.0), (1.0, 1.0, 1.0)]
    primitive = {
        "attributes": {"POSITION": builder.add_vec3(positions)},
        "indices": builder.add_indices([0, 1, 2]),
        "mode": 4,
    }
    png = _png_bytes()
    view = builder.add_view(png)
    image_view = mutate(builder, view, png)
    builder.images.append({"mimeType": "image/png", "bufferView": image_view})
    builder.textures.append({"source": 0})
    builder.materials.append({
        "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}},
    })
    primitive["material"] = 0
    builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
    return builder.to_bytes()


class ReviewCaseTests(unittest.TestCase):
    def test_tightly_bounded_embedded_png_stays_valid(self):
        package = _package(_triangle_with_png(lambda builder, view, png: view))
        self.assertEqual(package.source_count, 1)
        self.assertEqual(len(rs.assemble_window(package, (0, 16, 0, 16))), 1)

    def test_authored_normal_shear_and_reflection(self):
        from test_render_sectors import _one, _face_positions
        import math
        shear = [1,0,0,0, 2,1,0,0, 0,0,1,0, 0,0,0,1]
        cases = (({'matrix': shear}, (1/math.sqrt(5), -2/math.sqrt(5), 0)),
                 ({'scale': [-1,1,1]}, (-1,0,0)))
        for node, expected in cases:
            b = _one([(0,0,0), (1,0,0), (0,1,0)], normals=[(1,0,0)]*3, node=node)
            face = _face_positions(_package(b.to_bytes()))[0]
            for actual, want in zip(face['vertices'][0][5:8], expected):
                self.assertAlmostEqual(actual, want, delta=1e-7)

    def test_negative_glb_buffer_length_is_rejected(self):
        blob = _minimal_glb(-1, b"\x00")
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_glb_bytes(blob)

    def test_image_buffer_view_past_end_is_rejected(self):
        def mutate(builder, view, png):
            builder.buffer_views[view]["byteLength"] = len(png) + 32
            return view

        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(_triangle_with_png(mutate))

    def test_image_negative_buffer_view_index_is_rejected(self):
        def mutate(builder, view, png):
            del builder, view, png
            return -1

        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(_triangle_with_png(mutate))

    def test_image_external_buffer_index_is_rejected(self):
        def mutate(builder, view, png):
            del png
            builder.buffer_views[view]["buffer"] = 1
            return view

        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(_triangle_with_png(mutate))

    def test_accessor_negative_buffer_view_index_is_rejected(self):
        builder = GlbBuilder()
        positions = [(1.0, 0.0, 1.0), (2.0, 0.0, 1.0), (1.0, 1.0, 1.0)]
        accessor_index = builder.add_vec3(positions)
        self.assertEqual(len(builder.buffer_views), 1)
        builder.accessors[accessor_index]["bufferView"] = -1
        primitive = {"attributes": {"POSITION": accessor_index}, "mode": 4}
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(builder.to_bytes())

    def test_accessor_negative_view_offset_is_rejected(self):
        # View offset -4 plus accessor offset 4 lands on the real floats.
        builder = GlbBuilder()
        positions = [(1.0, 0.0, 1.0), (2.0, 0.0, 1.0), (1.0, 1.0, 1.0)]
        raw = b"".join(struct.pack("<3f", *point) for point in positions)
        view = builder.add_view(raw)
        builder.buffer_views[view]["byteOffset"] = -4
        builder.buffer_views[view]["byteLength"] = len(raw) + 4
        accessor_index = builder.add_accessor(view, 5126, "VEC3", 3, byteOffset=4)
        primitive = {"attributes": {"POSITION": accessor_index}, "mode": 4}
        builder.scenes[0]["nodes"] = [builder.add_node(mesh=builder.add_mesh([primitive]))]
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(builder.to_bytes())

    def test_accessor_zero_stride_is_rejected(self):
        builder = _interior()
        view = builder.accessors[0]["bufferView"]
        builder.buffer_views[view]["byteStride"] = 0
        with self.assertRaises(rs.RenderSectorError) as caught:
            _package(builder.to_bytes())

    def test_manifest_source_count_beyond_stored_faces_is_rejected(self):
        package = _package(_interior().to_bytes())
        header = rs.parse_manifest(package.manifest)
        self.assertEqual((header["source_count"], header["global_count"], header["replica_count"]), (1, 0, 1))
        self.assertEqual(header["entries"][0]["record_count"], 1)
        package.manifest = _patch_header(package.manifest, source_count=3)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_manifest(package.manifest)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.assemble_window(package, (0.0, 16.0, 0.0, 16.0))

    def test_manifest_sector_record_count_above_local_sources_is_rejected(self):
        package = _package(_interior().to_bytes())
        header = rs.parse_manifest(package.manifest)
        self.assertEqual(header["source_count"] - header["global_count"], 1)
        blob = bytearray(package.manifest)
        offset = _entry_offset(header)
        struct.pack_into("<I", blob, offset + 8, 2)
        struct.pack_into("<I", blob, offset + 12, 2 * rs.FACE_BYTES)
        forged = _patch_header(bytes(blob), replica_count=2, observed_max_sector_records=2)
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.parse_manifest(forged)

    def test_window_rejects_same_source_in_global_and_local(self):
        # S=2, G=1, R=1 is count-legal. Both buckets hold source id 0.
        package = _package(_interior().to_bytes())
        header = rs.parse_manifest(package.manifest)
        self.assertEqual(set(package.sectors), {(0, 0)})
        self.assertEqual((header["source_count"], header["global_count"], header["replica_count"]), (1, 0, 1))
        local = package.sectors[(0, 0)]
        raw = rs.parse_sector(local)["records"][0]["raw"]
        new_global = rs.encode_sector([raw], is_global=True)
        parsed = rs.parse_sector(new_global)
        entries_sum = sum(entry["file_bytes"] for entry in header["entries"])
        package.global_sector = new_global
        package.manifest = _patch_header(
            package.manifest,
            source_count=2,
            global_count=1,
            global_uncompressed=parsed["uncompressed_bytes"],
            global_file_bytes=len(new_global),
            global_payload_crc=parsed["payload_crc"],
            global_compressed_crc=parsed["compressed_crc"],
            package_bytes=len(package.manifest) + len(new_global) + entries_sum,
        )
        with self.assertRaises(rs.RenderSectorError) as caught:
            rs.assemble_window(package, (0.0, 16.0, 0.0, 16.0))


if __name__ == "__main__":
    unittest.main()
