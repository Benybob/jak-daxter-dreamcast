"""Layout byte identity, adjacency, runs and corruption checks (AI-assisted), ISC."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import pack_disc_level as disc
import prepare_render_sectors as sectors
from test_render_sectors import _package, _interior


class DiscLayoutTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="disc-layout-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "level"
        self.output = Path(self.tmp.name) / "fixture.bin"
        package = _package(_interior().to_bytes())
        sectors.publish_package(self.root, package)
        (self.root / "global.drs").rename(self.root / "global.drx")
        for path in (self.root / "sectors").glob("*.drs"):
            path.unlink()
        (self.root / "cells").mkdir()
        for z in range(-9, -6):
            for x in range(-9, -6):
                # Opaque payload bytes: layout correctness is independent of
                # DRX/DCI semantic validation, which belongs to their loaders.
                (self.root / f"sectors/{x}_{z}.drx").write_bytes(
                    bytes([(x-z) % 256]) * (2048 + (x+9)*17 + z+10))
                if (x, z) != (-8, -8):
                    (self.root / f"cells/{x}_{z}.dci").write_bytes(bytes([z % 256]) * 52)
        (self.root / "cells/6250_6250.dci").write_bytes(b"distant instance")
        (self.root / "sectors/6250_6249.drx").write_bytes(b"distant terrain")
        (self.root / "package.dci").write_bytes(b"resident prototype fixture")

    def packed(self):
        disc.pack(self.root, self.output)
        blob = self.output.read_bytes()
        return blob, disc.read_index(blob)

    def resign(self, blob, index=False):
        blob = bytearray(blob)
        if index:
            fields = list(disc.HEADER.unpack_from(blob))
            fields[12] = zlib.crc32(blob[disc.SECTOR:disc.SECTOR+fields[11]])
            disc.HEADER.pack_into(blob, 0, *fields)
        struct.pack_into("<I", blob, disc.SECTOR-4, zlib.crc32(blob[:disc.SECTOR-4]))
        return blob

    def test_every_source_byte_texture_page_pair_and_determinism(self):
        blob, index = self.packed()
        expected = {p.relative_to(self.root).as_posix(): p.read_bytes()
                    for pattern in ("global.drx", "sectors/*.drx", "cells/*.dci", "package.dci", "manifest.drm")
                    for p in self.root.glob(pattern)}
        parsed = sectors.parse_manifest(expected["manifest.drm"])
        expected.update((f"texture/{i}", t["pixels"]) for i, t in enumerate(parsed["textures"]))
        self.assertEqual(set(expected), set(index["entries"]))
        for name, source in expected.items():
            entry = index["entries"][name]
            self.assertEqual(entry.offset % 2048, 0)
            self.assertEqual(entry.length, len(source))
            self.assertEqual(blob[entry.offset:entry.offset+entry.length], source)
        for x, z in index["coords"]:
            a, b = f"sectors/{x}_{z}.drx", f"cells/{x}_{z}.dci"
            if a in index["entries"] and b in index["entries"]:
                a, b = index["entries"][a], index["entries"][b]
                self.assertEqual(a.offset + disc.align(a.length), b.offset)
        self.assertEqual(index["origin"], (-9, -9))
        self.assertGreater(index["index_bytes"], 2048)
        self.assertLess(index["index_bytes"], 16384)
        disc.pack(self.root, self.output)
        self.assertEqual(blob, self.output.read_bytes())

    def test_known_morton_order_with_negative_origin(self):
        points = [(x, z) for z in range(4) for x in range(4)]
        self.assertEqual(sorted(points, key=lambda p: disc.morton(*p)),
                         [(0, 0), (1, 0), (0, 1), (1, 1), (2, 0), (3, 0), (2, 1), (3, 1),
                          (0, 2), (1, 2), (0, 3), (1, 3), (2, 2), (3, 2), (2, 3), (3, 3)])
        _, index = self.packed()
        ordered = sorted(index["coords"], key=lambda p: disc.morton(p[0]+9, p[1]+9))
        offsets = []
        for x, z in ordered:
            offsets.extend(index["entries"][n].offset for n in
                           (f"sectors/{x}_{z}.drx", f"cells/{x}_{z}.dci") if n in index["entries"])
        self.assertEqual(offsets, sorted(offsets))

    def test_three_by_three_runs_cover_only_selected_sectors(self):
        _, index = self.packed()
        for cx in range(-10, -5):
            for cz in range(-10, -5):
                expected = set()
                for name, entry in index["entries"].items():
                    if not name.startswith(("sectors/", "cells/")):
                        continue
                    x, z = map(int, Path(name).stem.split("_"))
                    if abs(x-cx) <= 1 and abs(z-cz) <= 1:
                        expected.update(range(entry.offset, entry.offset+disc.align(entry.length), 2048))
                runs = disc.window_runs(index, cx, cz)
                actual = set()
                for off, size in runs:
                    actual.update(range(off, off+size, 2048))
                self.assertEqual(expected, actual)
                for a, b in zip(runs, runs[1:]):
                    self.assertLess(sum(a), b[0])  # all adjacent runs already coalesced
        self.assertEqual(disc.window_runs(index, 0, 0), [])

    def test_truncation_header_index_payload_and_padding_corruption(self):
        blob, index = self.packed()
        bad = [blob[:n] for n in (0, 1, 2047, 2048, len(blob)-1)] + [blob+b"x"]
        entry = index["entries"]["cells/-9_-9.dci"]
        for pos in (0, 17, 100, 2044, 2048, entry.offset, entry.offset+entry.length):
            changed = bytearray(blob); changed[pos] ^= 1; bad.append(changed)
        for changed in bad:
            with self.subTest(length=len(changed)):
                with self.assertRaises(ValueError):
                    disc.read_index(changed)

    def test_resigned_gap_overlap_misalignment_and_bad_tile_table(self):
        blob, _ = self.packed()
        fields = disc.HEADER.unpack_from(blob)
        # First texture extent and first dense tile pointer, with index/header
        # CRCs repaired: semantic validation must still reject the changes.
        for offset, value in ((disc.SECTOR, 2049), (disc.SECTOR, len(blob)),
                              (disc.SECTOR + fields[9]*disc.EXTENT.size + 8, 0)):
            changed = bytearray(blob); struct.pack_into("<I", changed, offset, value)
            with self.assertRaises(ValueError):
                disc.read_index(self.resign(changed, index=True))
        global_off = struct.unpack_from("<I", blob, disc.HEADER.size)[0]
        for position in (global_off - 2048, global_off + 2048):
            changed = bytearray(blob); struct.pack_into("<I", changed, disc.HEADER.size, position)
            with self.assertRaises(ValueError):
                disc.read_index(self.resign(changed))

    def test_source_change_missing_input_and_output_source_refusal(self):
        self.packed()
        (self.root / "cells/-9_-9.dci").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "source byte mismatch"):
            disc.verify(self.root, self.output)
        with self.assertRaisesRegex(ValueError, "outside source"):
            disc.pack(self.root, self.root / "global.drx")
        (self.root / "package.dci").unlink()
        with self.assertRaises(OSError):
            disc.pack(self.root, self.output)

    def test_bad_names_empty_payload_and_index_budget(self):
        with patch.object(disc, "MAX_INDEX", 1):
            with self.assertRaisesRegex(ValueError, "index budget"):
                disc.pack(self.root, self.output)
        with patch.object(disc, "MAX_BYTES", 2048):
            with self.assertRaisesRegex(ValueError, "file budget"):
                disc.pack(self.root, self.output)
        path = self.root / "cells/01_0.dci"; path.write_bytes(b"x")
        with self.assertRaisesRegex(ValueError, "noncanonical"):
            disc.pack(self.root, self.output)
        path.unlink()
        (self.root / "cells/6250_6250.dci").write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "empty payload"):
            disc.pack(self.root, self.output)


if __name__ == "__main__":
    unittest.main()
