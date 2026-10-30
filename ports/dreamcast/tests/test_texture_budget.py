"""Synthetic texture residency and CLI checks (AI-assisted), ISC."""

import base64
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import texture_budget as tb


def png(width, height):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    pixels = (b"\0" + b"\x12\x34\x56\xff" * width) * height
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def glb(doc, binary=b""):
    text = json.dumps(doc).encode("utf-8")
    text += b" " * (-len(text) % 4)
    binary += b"\0" * (-len(binary) % 4)
    chunks = struct.pack("<I4s", len(text), b"JSON") + text
    if binary:
        chunks += struct.pack("<I4s", len(binary), b"BIN\0") + binary
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks


def scene(dimensions=((64, 64),), materials=None, embedded_view=False):
    images, views, binary = [], [], b""
    for index, dims in enumerate(dimensions):
        raw = png(*dims)
        if embedded_view:
            views.append(dict(buffer=0, byteOffset=len(binary), byteLength=len(raw)))
            images.append(dict(name="synthetic-%d" % index, bufferView=index, mimeType="image/png"))
            binary += raw
            binary += b"\0" * (-len(binary) % 4)
        else:
            images.append(dict(name="synthetic-%d" % index,
                               uri="data:image/png;base64," + base64.b64encode(raw).decode("ascii")))
    if materials is None:
        materials = [dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=i)))
                     for i in range(len(images))]
    doc = dict(asset=dict(version="2.0"), images=images,
               textures=[dict(source=i) for i in range(len(images))], materials=materials,
               meshes=[dict(primitives=[dict(material=i) for i in range(len(materials))])],
               nodes=[dict(mesh=0)], scenes=[dict(nodes=[0])], scene=0,
               buffers=[dict(byteLength=len(binary))], bufferViews=views)
    return doc, binary


class TextureBudgetTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.policy = tb.repository_policy()

    def measure(self, doc, binary=b""):
        path = self.root / "synthetic-background.glb"
        path.write_bytes(glb(doc, binary))
        return tb.measure(path, self.policy)

    def test_vram_layout_counts_both_sets_and_overflow(self):
        budget = tb.flythrough_budget()
        self.assertEqual(budget["tiles"], 300)
        self.assertEqual(budget["opb_bytes_per_set"], 1036800)
        self.assertEqual(budget["tile_matrix_bytes_per_set"], 7272)
        self.assertEqual(budget["alignment_bytes_per_set"], 24)
        self.assertEqual(budget["framebuffer_bytes_per_set"], 614400)
        self.assertEqual(budget["reserved_bytes"], 7511296)
        self.assertEqual(budget["texture_arena_bytes"], 877312)

    def test_vram_rounds_height_and_each_allocation(self):
        budget = tb.flythrough_budget(640, 479, 1, (8, 0, 0, 0, 0), 0)
        self.assertEqual(budget["tiles"], 300)
        self.assertEqual(budget["opb_bytes_per_set"], 9600)
        self.assertEqual(budget["reserved_bytes"], 2 * (128 + 9600 + 7296 + 614400))

    def test_png_and_jpeg_dimensions(self):
        self.assertEqual(tb.image_dimensions(png(17, 31)), (17, 31))
        # Minimal SOI/APP/SOF metadata, sufficient for a metadata reader.
        jpeg = (b"\xff\xd8\xff\xe0\0\4ab\xff\xc2"
                + struct.pack(">HBHHB", 8, 8, 32, 128, 1))
        self.assertEqual(tb.image_dimensions(jpeg), (128, 32))

    def test_bad_image_headers_are_refused(self):
        for raw in (b"", b"\xff\xd8", b"\xff\xd8\xff\xd9", b"\xff\xd8\xff\xe0\0\1",
                    b"\xff\xd8\xff\xc0\0\20\0", png(0, 8), png(4097, 1),
                    b"\x89PNG\r\n\x1a\n" + b"\0" * 25):
            with self.subTest(header=raw[:16]), self.assertRaises(ValueError):
                tb.image_dimensions(raw)

    def test_converter_dimensions_floor_clamp_and_half_cap(self):
        self.assertEqual(tb.target_dimensions(127, 19, 64), (64, 16))
        self.assertEqual(tb.target_dimensions(127, 19, 32), (32, 16))
        self.assertEqual(tb.target_dimensions(3, 1, 32), (8, 8))

    def test_raw_and_vq_exact_costs_and_codebook_padding(self):
        report = self.measure(*scene(((128, 128), (8, 8))))
        self.assertEqual(report["distinct_textures"], 2)
        self.assertEqual(report["source_pixels"], 16448)
        profiles = report["profiles"]
        self.assertEqual(profiles["raw64"]["payload_bytes"], 8320)
        self.assertEqual(profiles["raw32"]["payload_bytes"], 2176)
        self.assertEqual(profiles["vq64"]["payload_bytes"], 3072 + 2080)
        self.assertEqual(profiles["vq32"]["payload_bytes"], 2304 + 2080)
        self.assertEqual(profiles["raw64"]["allocator_chunk_estimate_bytes"], 8384)
        self.assertEqual(report["largest_textures"][0]["width"], 128)
        self.assertFalse(profiles["vq64"]["resident_path_supported"])

    def test_base_dedup_uses_image_mode_cutoff_not_sampler_or_factor(self):
        base = dict(baseColorTexture=dict(index=0))
        materials = [dict(pbrMetallicRoughness=dict(base, baseColorFactor=[1, 0, 0, 1])),
                     dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=1))),
                     dict(pbrMetallicRoughness=base, alphaMode="MASK"),
                     dict(pbrMetallicRoughness=base, alphaMode="MASK", alphaCutoff=0.25)]
        doc, binary = scene(materials=materials)
        doc["textures"].append(dict(source=0, sampler=0))
        doc["samplers"] = [dict(wrapS=33071)]
        report = self.measure(doc, binary)
        self.assertEqual(report["distinct_textures"], 1)
        self.assertEqual(report["catalog_slots"], 3)
        self.assertEqual(report["profiles"]["raw64"]["payload_bytes"], 24576)

    def test_strength_is_cpu_only_and_specular_is_separate_owner(self):
        materials = [dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=0),
                                                  metallicRoughnessTexture=dict(index=0)),
                          extensions=dict(KHR_materials_specular=dict(specularColorTexture=dict(index=0))))]
        report = self.measure(*scene(materials=materials))
        self.assertEqual(report["source_pixels"], 4096)
        self.assertEqual((report["color_slots"], report["strength_slots"]), (2, 1))
        self.assertEqual(report["profiles"]["raw64"]["payload_bytes"], 16384)
        self.assertEqual(report["profiles"]["raw64"]["cpu_strength_bytes"], 4096)

    def test_blend_and_rectangular_vq_fallbacks(self):
        materials = [dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=0))),
                     dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=1)), alphaMode="BLEND")]
        report = self.measure(*scene(((64, 16), (64, 64)), materials))
        profile = report["profiles"]["vq64"]
        self.assertEqual(profile["payload_bytes"], 10240)
        self.assertEqual(profile["raw_fallback_slots"], 2)
        self.assertEqual(profile["vq_slots"], 0)
        self.assertEqual(profile["format_bytes"], dict(ARGB1555=2048, ARGB4444=8192))
        self.assertEqual(profile["format_slots"], dict(ARGB1555=1, ARGB4444=1))

    def test_no_material_allocates_shared_white_fallback(self):
        doc, binary = scene((), [{}, {}])
        doc["meshes"][0]["primitives"].append({})
        report = self.measure(doc, binary)
        self.assertEqual(report["distinct_textures"], 0)
        self.assertEqual(report["fallback_slots"], 1)
        self.assertEqual(report["profiles"]["raw64"]["payload_bytes"], 128)

    def test_active_scene_excludes_unused_images_materials_and_meshes(self):
        doc, binary = scene(((64, 64), (128, 128)))
        doc["meshes"][0]["primitives"] = [dict(material=0)]
        doc["meshes"].append(dict(primitives=[dict(material=1)]))
        doc["nodes"] += [dict(mesh=1), dict(mesh=0)]
        doc["scenes"] += [dict(nodes=[1])]
        doc["scenes"][0]["nodes"] += [2]
        report = self.measure(doc, binary)
        self.assertEqual(report["embedded_images"], 2)
        self.assertEqual(report["distinct_textures"], 1)
        self.assertEqual(report["color_slots"], 1)

    def test_empty_scene_has_zero_cost(self):
        doc, binary = scene((), [])
        doc["scenes"][0]["nodes"] = []
        report = self.measure(doc, binary)
        self.assertEqual(report["profiles"]["raw64"]["payload_bytes"], 0)

    def test_embedded_buffer_view_and_data_uri_agree(self):
        a = self.measure(*scene(((32, 64),)))
        b = self.measure(*scene(((32, 64),), embedded_view=True))
        self.assertEqual(a["profiles"], b["profiles"])
        self.assertEqual(a["largest_textures"], b["largest_textures"])

    def test_external_uri_and_bad_view_are_refused(self):
        doc, binary = scene()
        doc["images"][0]["uri"] = "private.png"
        with self.assertRaisesRegex(ValueError, "embedded"):
            self.measure(doc, binary)
        doc, binary = scene(embedded_view=True)
        doc["bufferViews"][0]["byteLength"] = len(binary) + 100
        with self.assertRaisesRegex(ValueError, "outside BIN"):
            self.measure(doc, binary)

    def test_bad_indices_and_alpha_are_refused(self):
        for mutate in (lambda d: d.update(scene=True),
                       lambda d: d["materials"][0].update(alphaMode="OTHER"),
                       lambda d: d["materials"][0].update(alphaCutoff=float("nan")),
                       lambda d: d["textures"][0].update(source=100)):
            doc, binary = scene()
            mutate(doc)
            with self.assertRaises(ValueError):
                self.measure(doc, binary)

    def test_unsupported_texture_roles_are_not_silently_omitted(self):
        for role in ("normalTexture", "occlusionTexture", "emissiveTexture"):
            doc, binary = scene()
            doc["materials"][0][role] = dict(index=0)
            with self.assertRaisesRegex(ValueError, "texture role"):
                self.measure(doc, binary)
        doc, binary = scene()
        doc["materials"][0]["extensions"] = dict(
            KHR_materials_specular=dict(specularTexture=dict(index=0)))
        with self.assertRaisesRegex(ValueError, "specularTexture"):
            self.measure(doc, binary)

    def test_bad_glb_header_and_chunk_are_refused(self):
        path = self.root / "bad.glb"
        valid = glb(*scene())
        for raw in (b"", valid[:-1], b"xxxx" + valid[4:],
                    valid[:12] + struct.pack("<I", 0xFFFFFFFC) + valid[16:]):
            path.write_bytes(raw)
            with self.assertRaises(ValueError):
                tb.read_glb(path)

    def test_payload_fit_and_catalog_caps_are_separate(self):
        report = self.measure(*scene(((64, 64),) * 108))
        self.assertEqual(report["profiles"]["raw64"]["payload_bytes"], 884736)
        self.assertFalse(report["profiles"]["raw64"]["fits_payload"])
        self.assertTrue(report["profiles"]["raw32"]["fits_payload"])
        self.assertTrue(report["profiles"]["raw64"]["within_texture_catalog_caps"])
        policy = dict(self.policy, MAX_TEXTURES=1)
        path = self.root / "synthetic-background.glb"
        self.assertFalse(tb.measure(path, policy)["profiles"]["raw32"]["within_texture_catalog_caps"])

    def test_cli_single_and_all_write_json_and_print_table(self):
        for name in ("beta", "alpha"):
            directory = self.root / name
            directory.mkdir()
            (directory / (name + "-background.glb")).write_bytes(glb(*scene()))
        (self.root / "missing").mkdir()
        output = self.root / "report.json"
        stdout = StringIO()
        with redirect_stdout(stdout):
            self.assertEqual(tb.main(["--all", str(self.root), "--json", str(output)]), 0)
        report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual([r["level"] for r in report["levels"]], ["alpha", "beta"])
        self.assertEqual(report["directories_without_background_glb"], ["missing"])
        self.assertIn("raw64 B", stdout.getvalue())
        with redirect_stdout(stdout):
            self.assertEqual(tb.main([str(self.root / "alpha/alpha-background.glb")]), 0)
        self.assertIn("Largest referenced source images", stdout.getvalue())

    def test_cli_missing_inputs_fails_without_writing_report(self):
        output = self.root / "report.json"
        with redirect_stderr(StringIO()):
            self.assertEqual(tb.main(["--all", str(self.root), "--json", str(output)]), 1)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
