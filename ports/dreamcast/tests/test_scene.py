import copy
import json
import math
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCENE_CHECK = ROOT / "build" / ("scene-check.exe" if sys.platform == "win32" else "scene-check")
sys.path.insert(0, str(ROOT))
from convert_scene import accessor, convert, node_matrix, read_glb, transform
from make_scene_fixture import fixture


class SceneTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.directory.name)
        path = self.root / "fixture.glb"
        fixture(path)
        self.doc, self.binary = read_glb(path)

    def tearDown(self):
        self.directory.cleanup()

    def check_native(self, data):
        path = self.root / "scene.dcm"
        path.write_bytes(data)
        return subprocess.run([str(SCENE_CHECK), str(path)],
                              capture_output=True, text=True)

    def test_conversion_and_shared_native_loader(self):
        data, report = convert(self.doc, self.binary)
        result = self.check_native(data)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["triangles"], 512)
        self.assertEqual(len(data), 8 + 512*3*16)
        self.assertFalse(report["truncated"])
        self.assertEqual(report["textured_primitives_without_textures"], 0)
        radii = [sum(v*v for v in struct.unpack_from("<fff", data, o))
                 for o in range(8, len(data), 16)]
        self.assertAlmostEqual(max(radii), 1, places=6)

    def test_mesh_selection_and_explicit_truncation(self):
        with self.assertRaisesRegex(ValueError, "no selected"):
            convert(self.doc, self.binary, "missing")
        with self.assertRaisesRegex(ValueError, "exceeds"):
            convert(self.doc, self.binary, limit=3)
        data, report = convert(self.doc, self.binary, limit=3, truncate=True)
        self.assertTrue(report["truncated"])
        self.assertEqual(report["source_triangles"], 512)
        self.assertEqual(report["output_triangles"], 3)
        self.assertEqual(self.check_native(data).returncode, 0)

    def test_translation_rotation_scale_and_column_major_matrix(self):
        node = {"translation": [10, 20, 30], "scale": [2, 3, 4],
                "rotation": [0, 0, math.sqrt(0.5), math.sqrt(0.5)]}
        result = transform(node_matrix(node), (1, 2, 3))
        for actual, expected in zip(result, (4, 22, 42)):
            self.assertAlmostEqual(actual, expected)
        matrix = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 7, 8, 9, 1]
        self.assertEqual(transform(node_matrix({"matrix": matrix}), (1, 2, 3)), (8, 10, 12))

    def test_exact_unnamed_mesh_index(self):
        self.doc["meshes"][0].pop("name", None)
        self.doc["meshes"].append(copy.deepcopy(self.doc["meshes"][0]))
        self.doc["nodes"].append({"mesh": 1})
        self.doc["scenes"][0]["nodes"].append(1)
        self.doc["nodes"][0]["matrix"] = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 53]
        data, report = convert(self.doc, self.binary, mesh_index=1)
        self.assertEqual(report["output_triangles"], 512)
        self.assertEqual(self.check_native(data).returncode, 0)
        for index in (-1, 2):
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, "mesh index"):
                convert(self.doc, self.binary, mesh_index=index)

    def test_instancing_and_node_cycles(self):
        self.doc["nodes"].append({"mesh": 0, "translation": [5, 0, 0]})
        self.doc["scenes"][0]["nodes"].append(1)
        _, report = convert(self.doc, self.binary)
        self.assertEqual(report["output_triangles"], 1024)
        self.doc["nodes"][0]["children"] = [0]
        with self.assertRaisesRegex(ValueError, "cycle"):
            convert(self.doc, self.binary)

    def test_interleaved_normalized_accessor_with_offsets(self):
        doc = {"bufferViews": [{"buffer": 0, "byteOffset": 4, "byteLength": 8, "byteStride": 4}],
               "accessors": [{"bufferView": 0, "byteOffset": 1, "componentType": 5121,
                              "type": "VEC3", "count": 2, "normalized": True}]}
        values = accessor(doc, b"----" + bytes([0, 255, 0, 128, 0, 0, 255, 255]), 0)
        self.assertEqual(values[0][:2], (1, 0))
        self.assertAlmostEqual(values[0][2], 128/255)
        self.assertEqual(values[1], (0, 1, 1))
        doc["accessors"][0]["count"] = 3
        with self.assertRaisesRegex(ValueError, "outside"):
            accessor(doc, b"----" + b"\0"*8, 0)

    def test_bad_indices_sparse_nan_and_primitive_mode(self):
        for mutation in ("indices", "sparse", "nan", "mode"):
            doc, binary = copy.deepcopy(self.doc), self.binary
            if mutation == "indices":
                offset = doc["bufferViews"][2]["byteOffset"]
                binary = binary[:offset] + b"\xff\xff" + binary[offset+2:]
            elif mutation == "sparse":
                doc["accessors"][0]["sparse"] = {}
            elif mutation == "nan":
                binary = struct.pack("<f", float("nan")) + binary[4:]
            else:
                doc["meshes"][0]["primitives"][0]["mode"] = 5
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                convert(doc, binary)

    def test_reject_bad_native_headers_counts_nan_bounds_trailing_and_truncation(self):
        data, _ = convert(self.doc, self.binary, limit=1, truncate=True)
        for bad in (b"BAD!"+data[4:], b"DCM1"+struct.pack("<I", 24579),
                    b"DCM1"+struct.pack("<I", 2), data+b"x",
                    data[:8]+struct.pack("<f", float("nan"))+data[12:],
                    data[:8]+struct.pack("<f", 100)+data[12:]):
            self.assertNotEqual(self.check_native(bad).returncode, 0)
        for size in range(len(data)):
            with self.subTest(size=size):
                self.assertNotEqual(self.check_native(data[:size]).returncode, 0)

    def test_truncated_glb_container(self):
        path = self.root / "fixture.glb"
        data = path.read_bytes()
        for cut in (0, 11, 12, 20, len(data)-1):
            path.write_bytes(data[:cut])
            with self.assertRaises((ValueError, KeyError)):
                read_glb(path)


if __name__ == "__main__":
    unittest.main()
