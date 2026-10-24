"""Synthetic DCR1 tests (AI-assisted), ISC. for the offline collision-sector partition.

The bytes in this module are constructed here. Nothing is read from a game
asset. Run with the standard library: python -m unittest test_partition_collision
"""

import struct
import tempfile
import unittest
import zlib
from pathlib import Path

import sys
PORT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PORT))
import partition_collision as pc


def mode_pat(mode, noentity=0, event=0):
    return (noentity & 1) | ((mode & 7) << pc.PAT_MODE_SHIFT) | ((event & 63) << pc.PAT_EVENT_SHIFT)


def make_dcr(records):
    payload = bytearray(struct.pack("<4sIII", b"DCR1", 1, 43, len(records)))
    for coords, pat in records:
        payload += struct.pack("<9fI", *coords, pat)
    return bytes(payload)


def ground(x, z, y=0.0, pat=0, size=1.0):
    return (
        (x, y, z, x + size, y, z, x, y + 0.25, z + size),
        pat,
    )


def cells_of(package):
    header = pc.parse_manifest(package.manifest)
    return [(entry["ix"], entry["iz"]) for entry in header["entries"]]


def sector_records(package, ix, iz):
    return pc.parse_sector(package.sectors[(ix, iz)])["raw_records"]


class DcrValidationTests(unittest.TestCase):
    def test_malformed_eof_magic_version_and_count(self):
        good = make_dcr([ground(1.0, 1.0)])
        self.assertEqual(len(good), 16 + 40)
        for blob in (
            good[:-1],
            good + b"\x00",
            good[:8],
            b"",
            struct.pack("<4sIII", b"DCR1", 1, 43, 0),
            struct.pack("<4sIII", b"DCR1", 1, 43, 2) + good[16:],
            struct.pack("<4sIII", b"XXXX", 1, 43, 1) + good[16:],
            struct.pack("<4sIII", b"DCR1", 2, 43, 1) + good[16:],
            struct.pack("<4sIII", b"DCR1", 1, 42, 1) + good[16:],
            struct.pack("<4sIII", b"DCR1", 1, 43, pc.MAX_SOURCE_TRIANGLES + 1),
        ):
            with self.assertRaises(pc.PartitionError):
                pc.build_package(blob)

    def test_nan_and_inf_are_refused(self):
        for bad in (float("nan"), float("inf"), float("-inf")):
            coords = (0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, bad, 1.0)
            with self.assertRaises(pc.PartitionError) as caught:
                pc.build_package(make_dcr([(coords, mode_pat(0))]))
            self.assertIn("nonfinite", str(caught.exception))

    def test_world_coordinate_bound(self):
        coords = (pc.WORLD_ABS_MAX_M + 10.0, 0.0, 0.0, pc.WORLD_ABS_MAX_M + 11.0, 0.0, 0.0, pc.WORLD_ABS_MAX_M + 10.0, 1.0, 1.0)
        with self.assertRaises(pc.PartitionError) as caught:
            pc.build_package(make_dcr([(coords, 0)]))
        self.assertIn("world bound", str(caught.exception))

    def test_unsupported_pat_mode_refuses_every_value_above_obstacle(self):
        for mode in (3, 4, 5, 6, 7):
            with self.assertRaises(pc.PartitionError) as caught:
                pc.build_package(make_dcr([ground(1.0, 1.0, pat=mode_pat(mode))]))
            self.assertIn("unsupported PAT mode", str(caught.exception))

    def test_supported_modes_build(self):
        records = [ground(float(mode), 1.0, pat=mode_pat(mode)) for mode in (0, 1, 2)]
        package = pc.build_package(make_dcr(records))
        self.assertEqual(package.source_count, 3)


class PartitionGeometryTests(unittest.TestCase):
    def test_boundary_touch_is_in_both_closed_cells(self):
        # Entire face lies on z = 16, the shared edge of iz 0 and iz 1.
        records = [((1.0, 0.0, 16.0, 5.0, 0.0, 16.0, 3.0, 1.0, 16.0), mode_pat(0))]
        package = pc.build_package(make_dcr(records))
        self.assertEqual(cells_of(package), [(0, 0), (0, 1)])
        self.assertEqual(package.replica_count, 2)
        self.assertEqual(package.source_count, 1)
        for cell in ((0, 0), (0, 1)):
            self.assertEqual(sector_records(package, *cell)[0][0], 0)

    def test_edge_crossing_two_cells(self):
        records = [((1.0, 0.0, 1.0, 20.0, 0.0, 1.0, 20.0, 0.0, 4.0), mode_pat(1))]
        package = pc.build_package(make_dcr(records))
        self.assertEqual(cells_of(package), [(0, 0), (1, 0)])
        self.assertEqual(package.replica_count, 2)
        self.assertEqual(package.observed_max_faces, 1)

    def test_crossing_included_when_all_vertices_lie_outside_the_cell(self):
        # Edge from (-1, 8) to (17, -1) passes through cell (0, 0). No vertex
        # lies in the closed rectangle [0, 16] x [0, 16].
        records = [((-1.0, 0.0, 8.0, 17.0, 0.0, -1.0, 17.0, 0.0, 17.0), mode_pat(0))]
        package = pc.build_package(make_dcr(records))
        self.assertIn((0, 0), cells_of(package))
        self.assertNotIn((5, 5), cells_of(package))
        raw = sector_records(package, 0, 0)[0][1]
        self.assertEqual(raw, make_dcr(records)[16:])

    def test_negative_cells_and_negative_boundary(self):
        interior = ((-24.0, 0.0, -24.0, -20.0, 1.0, -24.0, -22.0, 0.0, -20.0), mode_pat(0))
        package = pc.build_package(make_dcr([interior]))
        self.assertEqual(cells_of(package), [(-2, -2)])
        self.assertEqual(package.ix_min, -2)
        self.assertEqual(package.iz_min, -2)

        boundary = ((-16.0, 0.0, -8.0, -16.0, 2.0, -8.0, -16.0, 1.0, -6.0), mode_pat(2))
        package = pc.build_package(make_dcr([boundary]))
        self.assertEqual(cells_of(package), [(-2, -1), (-1, -1)])

    def test_vertical_face_inside_cell_and_on_grid_line(self):
        interior = ((1.0, 0.0, 1.0, 1.0, 4.0, 1.0, 1.0, 2.0, 3.0), mode_pat(1))
        package = pc.build_package(make_dcr([interior]))
        self.assertEqual(cells_of(package), [(0, 0)])

        on_line = ((0.0, 0.0, 1.0, 0.0, 4.0, 1.0, 0.0, 2.0, 3.0), mode_pat(1))
        package = pc.build_package(make_dcr([on_line]))
        self.assertEqual(cells_of(package), [(-1, 0), (0, 0)])

    def test_y_extent_stays_on_one_xz_cell(self):
        tall = ((0.5, 0.0, 0.5, 1.5, 30.0, 0.5, 0.5, 0.0, 1.5), mode_pat(0))
        package = pc.build_package(make_dcr([tall]))
        self.assertEqual(package.cell_count, 1)
        self.assertEqual(cells_of(package), [(0, 0)])

    def test_span_cap_refuses_without_trimming(self):
        huge = ((0.0, 0.0, 0.0, 100.0, 0.0, 0.0, 0.0, 0.0, 10.0), mode_pat(0))
        with self.assertRaises(pc.PartitionError) as caught:
            pc.build_package(make_dcr([huge]))
        self.assertIn("triangle span exceeds cap", str(caught.exception))

    def test_many_cell_triangle_is_refused(self):
        # 60 m span is under the 64 m span cap. At 4 m cells the covered column
        # count is above MAX_CELLS_PER_TRIANGLE.
        wide = ((0.0, 0.0, 0.0, 60.0, 0.0, 0.0, 30.0, 0.0, 60.0), mode_pat(0))
        with self.assertRaises(pc.PartitionError) as caught:
            pc.build_package(make_dcr([wide]), cell_size=4.0)
        self.assertIn("triangle overlaps too many cells", str(caught.exception))

    def test_sparse_assignment_does_not_broadcast(self):
        near = ground(1.0, 1.0, pat=mode_pat(0))
        far = ground(40.0, 40.0, pat=mode_pat(0))
        package = pc.build_package(make_dcr([near, far]))
        self.assertEqual(package.cell_count, 2)
        self.assertEqual(package.replica_count, 2)
        self.assertEqual(sector_records(package, 0, 0)[0][0], 0)
        self.assertEqual(sector_records(package, 2, 2)[0][0], 1)
        self.assertNotIn(1, [item[0] for item in sector_records(package, 0, 0)])

    def test_stable_ids_follow_source_order_not_grid_order(self):
        high = ground(30.0, 1.0, pat=mode_pat(0, event=4))
        low = ground(-20.0, -5.0, pat=mode_pat(1, noentity=1, event=9))
        package = pc.build_package(make_dcr([high, low]))
        self.assertEqual(cells_of(package)[0][0], -2)
        self.assertEqual(sector_records(package, -2, -1)[0][0], 1)
        self.assertEqual(sector_records(package, 1, 0)[0][0], 0)

    def test_noentity_pat_and_winding_bytes_are_retained(self):
        pat = mode_pat(2, noentity=1, event=17)
        coords = (0.1, -0.2, 0.3, 1.25, -1.5, 1.75, 2.5, -2.25, 2.75)
        blob = make_dcr([(coords, pat)])
        package = pc.build_package(blob)
        source_id, raw = sector_records(package, 0, 0)[0]
        self.assertEqual(source_id, 0)
        self.assertEqual(raw, blob[16:])
        stored_pat = struct.unpack_from("<I", raw, 36)[0]
        self.assertEqual(stored_pat, pat)
        self.assertEqual(stored_pat & 1, 1)
        self.assertEqual((stored_pat >> 14) & 63, 17)
        self.assertEqual(struct.unpack("<9f", raw[:36]), struct.unpack("<9f", blob[16:52]))

    def test_determinism_and_crc_vector(self):
        self.assertEqual(pc.crc32(b"123456789"), 0xCBF43926)
        records = (
            ((1.0, 0.0, 1.0, 20.0, 0.0, 1.0, 20.0, 1.0, 4.0), mode_pat(0, noentity=1, event=2)),
            ground(-18.0, 3.0, pat=mode_pat(1)),
            ground(2.0, -20.0, pat=mode_pat(2, event=6)),
        )
        blob = make_dcr(records)
        first = pc.build_package(blob, 16.0)
        second = pc.build_package(blob, 16.0)
        self.assertEqual(first.manifest, second.manifest)
        self.assertEqual(first.sectors, second.sectors)
        header = pc.parse_manifest(first.manifest)
        self.assertEqual(header["source_crc"], zlib.crc32(blob) & 0xFFFFFFFF)
        self.assertEqual(header["flags"], pc.FLAG_GEOMETRY_ONLY)
        self.assertEqual(header["cache_cap"], 8192)
        self.assertEqual(header["candidate_cap"], 1024)
        self.assertEqual(header["cache_record_bytes"], 76)
        self.assertEqual(header["input_record_bytes"], 40)
        self.assertEqual(header["camera_arm"], pc._f32(10.4))
        keys = [(entry["ix"], entry["iz"]) for entry in header["entries"]]
        self.assertEqual(keys, sorted(keys))
        flipped = bytearray(first.manifest)
        flipped[HEADER_BYTE] ^= 0xFF
        with self.assertRaises(pc.PartitionError):
            pc.parse_manifest(bytes(flipped))


HEADER_BYTE = 40


class RuntimeReferenceTests(unittest.TestCase):
    def test_camera_arm_widens_the_neighborhood(self):
        window = pc.query_window((8.0, 1.0, 8.0), (8.0, 1.0, 8.0), 0.5)
        selected = pc.select_cells(*window, 16.0)
        self.assertEqual({ix for ix, _iz in selected}, {-1, 0, 1})
        self.assertEqual({iz for _ix, iz in selected}, {-1, 0, 1})
        tight = pc.select_cells(7.5, 8.5, 7.5, 8.5, 16.0)
        self.assertEqual(tight, [(0, 0)])

    def test_displacement_and_radius_caps(self):
        pc.query_window((0.0, 0.0, 0.0), (8.0, 0.0, 0.0), 2.0)
        with self.assertRaises(pc.PartitionError) as caught:
            pc.query_window((0.0, 0.0, 0.0), (9.0, 0.0, 0.0), 0.5)
        self.assertIn("displacement exceeds 8", str(caught.exception))
        with self.assertRaises(pc.PartitionError) as caught:
            pc.query_window((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 2.5)
        self.assertIn("capsule radius exceeds allowance", str(caught.exception))

    def test_negative_query_cell(self):
        selected = pc.select_cells(-20.5, -19.5, -20.5, -19.5, 16.0)
        self.assertEqual(selected, [(-2, -2)])
        boundary = pc.select_cells(-16.0, -16.0, 0.0, 0.0, 16.0)
        self.assertIn((-2, -1), boundary)
        self.assertIn((-1, 0), boundary)

    def test_dedup_collapses_replicas_and_refuses_mismatch_or_overflow(self):
        face = ((1.0, 0.0, 1.0, 20.0, 0.0, 1.0, 20.0, 0.0, 4.0), mode_pat(1, noentity=1, event=3))
        package = pc.build_package(make_dcr([face]))
        gathered = []
        for cell in cells_of(package):
            gathered.extend(sector_records(package, *cell))
        self.assertGreater(len(gathered), 1)
        unique = pc.unique_records(gathered)
        self.assertEqual(len(unique), 1)
        self.assertEqual(unique[0][0], 0)
        self.assertEqual(struct.unpack_from("<I", unique[0][1], 36)[0], face[1])

        raw = unique[0][1]
        other = bytearray(raw)
        other[-1] ^= 0x01
        with self.assertRaises(pc.PartitionError) as caught:
            pc.unique_records([(0, raw), (0, bytes(other))])
        self.assertIn("replica mismatch", str(caught.exception))

        overflow = [(index, raw) for index in range(pc.CACHE_TRIANGLES + 1)]
        with self.assertRaises(pc.PartitionError) as caught:
            pc.unique_records(overflow)
        self.assertIn("unique faces exceed cache", str(caught.exception))

    def test_rebase_keeps_pat_and_refuses_past_4096(self):
        pat = mode_pat(0, noentity=1, event=1)
        records = [((5000.0, 10.0, -20.0, 5001.0, 10.0, -20.0, 5000.0, 11.0, -19.0), pat)]
        package = pc.build_package(make_dcr(records))
        raw = sector_records(package, *cells_of(package)[0])
        near = pc.rebase_records(raw, (5000.0, 10.0, -20.0))
        local = struct.unpack("<9f", near[0][1][:36])
        self.assertLessEqual(max(abs(value) for value in local), 2.0)
        self.assertEqual(near[0][1][36:], raw[0][1][36:])
        with self.assertRaises(pc.PartitionError) as caught:
            pc.rebase_records(raw, (-2000.0, 10.0, -20.0))
        self.assertIn("local coordinate exceeds 4096", str(caught.exception))

    def test_missing_region_and_known_empty_halo(self):
        package = pc.build_package(make_dcr([ground(1.0, 1.0, pat=mode_pat(0))]))
        self.assertFalse(pc.cell_is_missing(package, 0, 0))
        self.assertFalse(pc.cell_is_missing(package, 2, 0))
        self.assertTrue(pc.cell_is_missing(package, 3, 0))
        with self.assertRaises(pc.PartitionError) as caught:
            pc.assemble_neighborhood(package, [(0, 0), (3, 0)])
        self.assertIn("missing region", str(caught.exception))
        air = pc.assemble_neighborhood(package, [(2, 0)])
        self.assertEqual(air, [])
        covered = pc.assemble_neighborhood(package, [(0, 0), (2, -2)])
        self.assertEqual(len(covered), 1)
        self.assertEqual(covered[0][0], 0)

    def test_neighborhood_guarantee_and_memory_budget(self):
        budget = pc.memory_budget()
        self.assertEqual(budget["cache_bytes"], 8192 * 76)
        self.assertEqual(budget["per_sector_cap_16m"], 2048)
        self.assertEqual(budget["neighborhood_cells_16m"], 16)
        self.assertEqual(budget["peak_bytes"], 1720320)
        self.assertLessEqual(budget["peak_with_sector_held_bytes"], budget["ceiling_bytes"])
        self.assertEqual(budget["ceiling_bytes"], 2 * 1024 * 1024)
        for cell_size in (4.0, 8.0, 16.0, 32.0, 64.0):
            axis, neighborhood, per_sector = pc.neighborhood_limits(cell_size)
            self.assertEqual(per_sector, pc.MAX_SECTOR_FACES)
            self.assertEqual(neighborhood, axis * axis)
            self.assertGreaterEqual(per_sector, 1)
        with self.assertRaises(pc.PartitionError):
            pc.build_package(make_dcr([ground(1.0, 1.0)]), cell_size=3.9)
        with self.assertRaises(pc.PartitionError):
            pc.build_package(make_dcr([ground(1.0, 1.0)]), cell_size=64.1)


class WriteTests(unittest.TestCase):
    def test_atomic_failure_removes_the_directory_and_manifest(self):
        package = pc.build_package(make_dcr([ground(1.0, 2.0), ground(24.0, 2.0)]))
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "sectors-out"
            with self.assertRaises(OSError):
                pc.write_package(out, package, fail_after=1)
            self.assertFalse(out.exists())
            self.assertFalse((out / "manifest.dcs").exists())

    def test_existing_directory_is_left_unchanged(self):
        package = pc.build_package(make_dcr([ground(1.0, 1.0)]))
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "sectors-out"
            out.mkdir()
            (out / "keep.txt").write_bytes(b"keep")
            with self.assertRaises(pc.PartitionError) as caught:
                pc.write_package(out, package)
            self.assertIn("already exists", str(caught.exception))
            self.assertEqual((out / "keep.txt").read_bytes(), b"keep")
            self.assertFalse((out / "manifest.dcs").exists())

    def test_cli_writes_a_complete_directory_and_refuses_a_bad_file(self):
        records = [ground(1.0, 1.0, pat=mode_pat(0, noentity=1))]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            dcr_path = root / "sample.dcr"
            dcr_path.write_bytes(make_dcr(records))
            out = root / "pkg"
            self.assertEqual(pc.main([str(dcr_path), str(out), "--cell-size", "16"]), 0)
            self.assertTrue((out / "manifest.dcs").is_file())
            self.assertTrue((out / "sectors" / "0_0.dcs").is_file())
            self.assertEqual(list(out.rglob("*.partial")), [])
            header = pc.parse_manifest((out / "manifest.dcs").read_bytes())
            self.assertEqual(header["source_count"], 1)
            self.assertEqual(header["cell_size"], 16.0)

            bad = root / "bad.dcr"
            bad.write_bytes(make_dcr([((0.0, 0.0, 0.0, 100.0, 0.0, 0.0, 0.0, 0.0, 1.0), 0)]))
            rejected = root / "rejected"
            self.assertEqual(pc.main([str(bad), str(rejected)]), 1)
            self.assertFalse(rejected.exists())

            nan = root / "nan.dcr"
            nan.write_bytes(make_dcr([((0.0, float("nan"), 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0), 0)]))
            nan_out = root / "nan-out"
            self.assertEqual(pc.main([str(nan), str(nan_out)]), 1)
            self.assertFalse(nan_out.exists())


if __name__ == "__main__":
    unittest.main()
