import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROBE = ROOT / "build" / ("dgo-probe.exe" if sys.platform == "win32" else "dgo-probe")
sys.path.insert(0, str(ROOT))
from make_fixture import archive


class ProbeTest(unittest.TestCase):
    def run_probe(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "TEST.DGO"
            path.write_bytes(data)
            result = subprocess.run([str(PROBE), str(path)],
                                    capture_output=True, text=True)
        return result, [json.loads(line) for line in result.stdout.splitlines()]

    def test_padding_offsets_and_crc(self):
        entries = [(str(size).encode(), bytes(range(size))) for size in range(33)]
        result, rows = self.run_probe(archive(entries))
        self.assertEqual(result.returncode, 0, result.stderr)
        offset = 64
        for row, (name, payload) in zip(rows[:-1], entries):
            self.assertEqual(row["data_offset"], offset + 64)
            self.assertEqual(row["data_offset"] % 16, 0)
            self.assertEqual(row["size"], len(payload))
            self.assertEqual(row["name"], name.decode())
            self.assertEqual(row["crc32"], f"{zlib.crc32(payload):08x}")
            offset += 64 + ((len(payload) + 15) // 16) * 16
        self.assertEqual(rows[-1]["bytes"], offset)
        self.assertEqual(rows[-1]["object_count"], 33)

    def test_multi_megabyte_payload(self):
        payload = bytes(range(256)) * 20481
        result, rows = self.run_probe(archive([(b"large", payload), (b"tail", b"123456789")]))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(rows[0]["crc32"], f"{zlib.crc32(payload):08x}")
        self.assertEqual(rows[1]["crc32"], "cbf43926")
        self.assertEqual(rows[-1]["largest_object"], len(payload))

    def test_empty_archive(self):
        result, rows = self.run_probe(archive([]))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(rows[0]["bytes"], 64)

    def test_every_truncated_prefix_is_rejected(self):
        data = archive([(b"one", b"abc"), (b"two", b"123456789")])
        for size in range(len(data)):
            with self.subTest(size=size):
                result, _ = self.run_probe(data[:size])
                self.assertNotEqual(result.returncode, 0)

    def test_bad_names_count_trailing_and_size(self):
        cases = [
            struct.pack("<I", 0) + b"x" * 60,
            struct.pack("<I60s", 0, b"a\0b"),
            struct.pack("<I60s", 65537, b"limit"),
            archive([]) + b"extra",
            b"oZlB" + b"\0" * 60,
            struct.pack("<I60s", 1, b"test") + struct.pack("<I", 0) + b"x" * 60,
            struct.pack("<I60sI60s", 1, b"test", 0xffffffff, b"huge"),
        ]
        for data in cases:
            with self.subTest(data=data[:16]):
                result, _ = self.run_probe(data)
                self.assertNotEqual(result.returncode, 0)

    def test_name_json_escaping(self):
        result, rows = self.run_probe(archive([(b'quote"\\\x01\xff', b"")]))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(rows[0]["name"], 'quote"\\\x01\xff')

    def test_audit_mixed_success_and_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "GOOD.DGO").write_bytes(archive([(b"object", b"a" * 2048)]))
            (root / "BAD.CGO").write_bytes(b"bad")
            result = subprocess.run([sys.executable, str(ROOT / "audit_assets.py"),
                                     str(root), "--staging-mib", "0.001"],
                                    capture_output=True, text=True)
            report = json.loads(result.stdout)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(len(report["archives"]), 1)
            self.assertEqual(len(report["failures"]), 1)
            self.assertEqual(len(report["archives"][0]["objects_over_staging_budget"]), 1)

    def test_audit_no_archives_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, str(ROOT / "audit_assets.py"), directory],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("no DGO/CGO", result.stderr)


if __name__ == "__main__":
    unittest.main()
