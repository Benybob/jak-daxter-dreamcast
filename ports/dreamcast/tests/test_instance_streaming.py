"""Synthetic instance-cell residency and DCI2/DCI3 byte accounting (AI-assisted), ISC."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import instance_streaming as stream  # noqa: E402
import make_flythrough_fixture as fly  # noqa: E402
import make_instance_fixture as mf  # noqa: E402
import tie_lods  # noqa: E402


def instance(proto, x, z, y=0.0):
    return (proto, 0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, x, y, z)


def package(instances, compact=True):
    geoms = [dict(name=f'g{i}', material=i,
                  verts=[(0.0, 0.0, 0.0, 0.0, 0.0, 0xffffffff),
                         (1.0, 0.0, 0.0, 0.0, 0.0, 0xffffffff),
                         (0.0, 1.0, 0.0, 0.0, 0.0, 0xffffffff)], indices=[0, 1, 2])
             for i in range(3)]
    # Prototype 0: two materials at LOD 0, a third geometry at LOD 1.
    # Prototype 1 shares its LOD 0 with prototype 0's second material.
    table = [((0, 2, tie_lods.NONE, 2), (1.0, 2.0, 3.0), (1, 0, 0, 0)),
             ((1, tie_lods.NONE, tie_lods.NONE, tie_lods.NONE), (tie_lods.NO_SWITCH,) * 3)]
    return tie_lods.pack(geoms, table, instances, compact=compact)


class InstanceStreamingTest(unittest.TestCase):
    def test_byte_accounting_and_index_padding_in_both_formats(self):
        for compact, stride in ((False, 24), (True, 16)):
            with self.subTest(compact=compact):
                blob = package([instance(0, 0.0, 0.0)], compact)
                a = stream.Package(blob).accounting
                self.assertEqual(a['instance_stride'], 52)
                self.assertEqual(a['instance_bytes'], 52)
                self.assertEqual(a['vertex_bytes'], 9 * stride)
                self.assertEqual(a['index_bytes'], 18)
                self.assertEqual(a['index_padding_bytes'], 2)
                self.assertEqual(a['quantization_bytes'], 48 if compact else 0)
                self.assertEqual(a['fixed_bytes'] + a['geometry_bytes'] + a['instance_bytes'], len(blob))

    def test_cells_use_floor_and_catalog_keeps_every_original_record_once(self):
        p = stream.Package(package([instance(0, -0.01, -16.0), instance(0, 0.0, 0.0),
                                    instance(1, 16.0, 16.0)]))
        index = stream.Cells(p, 16)
        self.assertEqual(index.records, {(-1, -1): [0], (0, 0): [1], (1, 1): [2]})
        self.assertEqual(sorted(i for c in index.catalog() for i in c['instance_ids']), [0, 1, 2])

    def test_disk_cell_intersection_and_whole_cell_overfetch(self):
        index = stream.Cells(stream.Package(package([instance(0, 3.0, 4.0),
                                                     instance(0, 15.0, 15.0),
                                                     instance(1, 16.0, 16.0)])), 16)
        row = index.query((0, 10000, 0), 5)
        self.assertEqual(row['resident_cells'], [[0, 0]])
        self.assertEqual((row['drawn_instances'], row['resident_instances'], row['overfetch_instances']), (1, 2, 1))
        self.assertEqual(row['record_bytes'], 104)
        self.assertEqual(row['record_freed_bytes'], 52)

    def test_float32_rule_at_and_just_beyond_radius(self):
        next_float = struct.unpack('<f', struct.pack('<I', 0x40a00001))[0]
        ins = [instance(0, 3.0, 4.0), instance(0, 5.0, 0.0), instance(0, next_float, 0.0),
               instance(1, -5.0, 0.0), instance(1, 0.0, -next_float)]
        blob = package(ins)
        # This target and radius round to zero and five in the native rule.
        target, draw = (1e-46, 10000, 1e-46), 5.00000001
        for cell in (0.3, 1, 16, 32, 64):
            with self.subTest(cell=cell):
                index = stream.Cells(stream.Package(blob), cell)
                row = index.query(target, draw)
                self.assertEqual(row['drawn_instances'], mf.expected(*mf.read(blob), target, draw)[0])
                self.assertEqual(row['drawn_instances'], 3)
                resident = {i for k in row['resident_cells'] for i in index.records[tuple(k)]}
                self.assertTrue({0, 1, 3} <= resident)

    def test_geometries_keep_all_lods_materials_and_deduplicate_shared_runs(self):
        index = stream.Cells(stream.Package(package([instance(0, 0, 0), instance(1, 1, 0)])), 16)
        row = index.query((0, 0, 0), 2)
        self.assertEqual(row['active_prototypes'], [0, 1])
        self.assertEqual(row['geometry_resident_count'], 3)
        self.assertEqual(row['geometry_bytes'], 3 * (3 * 16 + 6))
        self.assertEqual(row['geometry_freed_bytes'], 0)

    def test_only_drawn_prototypes_keep_geometry_even_when_cell_records_overfetch(self):
        p = stream.Package(package([instance(1, 0, 0), instance(0, 15, 15)]))
        row = stream.Cells(p, 16).query((0, 0, 0), 1)
        self.assertEqual(row['resident_instances'], 2)
        self.assertEqual(row['active_prototypes'], [1])
        self.assertEqual((row['geometry_resident_count'], row['geometry_bytes']), (1, 54))
        self.assertEqual(row['geometry_freed_bytes'], 108)
        self.assertEqual(row['resident_bytes'] + row['freed_bytes'], p.accounting['package_bytes'])

    def test_far_target_frees_payload_but_keeps_all_metadata(self):
        p = stream.Package(package([instance(0, 0, 0)]))
        row = stream.Cells(p, 16).query((1000, 0, 1000), 5)
        self.assertEqual(row['resident_cells'], [])
        self.assertEqual(row['resident_bytes'], p.accounting['fixed_bytes'])
        self.assertEqual(row['freed_bytes'], p.accounting['instance_bytes'] + p.accounting['geometry_bytes'])

    def test_sparse_sweep_matches_exhaustive_rectangular_grid(self):
        p = stream.Package(package([instance(0, -65, -49), instance(1, 97, 81)]))
        for cell in (16, 32, 64):
            for draw in (5, 40, 80):
                with self.subTest(cell=cell, draw=draw):
                    index = stream.Cells(p, cell)
                    got = stream.sweep(index, draw)
                    x0, x1, z0, z1 = got['grid_index_bounds']
                    full = [index.query(((x + 0.5) * 16, 0, (z + 0.5) * 16), draw)
                            for x in range(x0, x1 + 1) for z in range(z0, z1 + 1)]
                    self.assertEqual(got['samples'], len(full))
                    self.assertEqual(got['rows'], [r for r in full if r['resident_instances']])
                    for key in ('record_bytes', 'geometry_bytes', 'resident_bytes', 'freed_bytes'):
                        self.assertEqual(got['summary'][key]['max'], max(r[key] for r in full))
                        self.assertEqual(got['summary'][key]['min'], min(r[key] for r in full))

    def test_path_and_sampling_are_independent_of_streaming_cell_size(self):
        blob = package([instance(0, 0, 0), instance(1, 40, 30)])
        out = stream.report(blob, (16, 32, 64), (80, 40))
        self.assertEqual(len(out['runs']), 6)
        targets = fly.path(mf.camera(*mf.read(blob)), 16, 30)
        for run in out['runs']:
            self.assertEqual([r['target_m'] for r in run['path']['rows']], targets)
            for target, row in zip(targets, run['path']['rows']):
                self.assertEqual(row['drawn_instances'], mf.expected(*mf.read(blob), target, run['draw_m'])[0])
            self.assertEqual(run['sweep']['samples'], 6)

    def test_rejects_bad_size_reference_and_missing_lod_zero(self):
        blob = package([instance(0, 0, 0)])
        bad_ref = bytearray(blob)
        struct.pack_into('<H', bad_ref, len(blob) - 52, 2)
        bad_lod = bytearray(blob)
        struct.pack_into('<H', bad_lod, 16 + 52 * 3, tie_lods.NONE)
        for bad in (b'', b'DCI1' + blob[4:], blob[:-1], blob + b'\0', bad_ref, bad_lod):
            with self.subTest(size=len(bad)):
                with self.assertRaises(ValueError):
                    stream.Package(bad)

    def test_invalid_grid_and_draw_parameters(self):
        p = stream.Package(package([instance(0, 0, 0)]))
        for value in (0, -1, float('nan'), float('inf')):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    stream.Cells(p, value)
                with self.assertRaises(ValueError):
                    stream.Cells(p, 16).query((0, 0, 0), value)

    def test_cli_accepts_comma_lists_and_prints_json_without_writing_input(self):
        blob = package([instance(0, 0, 0)])
        output = io.StringIO()
        with patch.object(Path, 'read_bytes', return_value=blob), redirect_stdout(output):
            self.assertEqual(stream.main(['synthetic.dci3', '--cell', '16,32,64', '--draw', '80,40']), 0)
        out = json.loads(output.getvalue())
        self.assertEqual([(r['cell_m'], r['draw_m']) for r in out['runs']],
                         [(16, 80), (16, 40), (32, 80), (32, 40), (64, 80), (64, 40)])


if __name__ == '__main__':
    unittest.main()
