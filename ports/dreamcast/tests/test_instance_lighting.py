"""Synthetic placement lighting, byte accounting and CLI tests (AI-assisted), ISC.
"""
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import instance_lighting as lighting  # noqa: E402
import prototype_geometry as pg  # noqa: E402


def triangle(x=0.0, colours=None, material=0):
    colours = colours or ((10, 20, 30), (40, 50, 60), (70, 80, 90))
    return (((x, 0.0, 0.0), (x + 1, 0.0, 0.0), (x, 1.0, 0.0)),
            ((0.0, 0.0), (1.0, 0.0), (0.0, 1.0)),
            tuple((*lighting.glb_rgb(c), 0.5) for c in colours), material)


def instance(x=0.0):
    return {'proto': 0, 'x': x, 'y': 0.0, 'z': 0.0, 'r': 2.0,
            'origin': [4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 4096, 0, 0, 0, 0, 0]}


def analyse(base, cuts, factors=None, palette=()):
    raw, masks, means, _ = lighting.collect(base, cuts)
    return lighting.analyse_prototype(base, raw, masks, means, factors or {},
                                       [(i, 0) for i in range(len(raw))], palette)


class ErrorsTest(unittest.TestCase):
    def test_nearest_rank_and_per_channel(self):
        e = lighting.Errors()
        for _ in range(19):
            e.add((0, 10, 20), (0, 0, 0))
        e.add((255, 255, 255), (0, 0, 0))
        r = e.report()
        self.assertEqual(r['corners'], 20)
        self.assertEqual(r['p95_rgb'], [0, 10, 20])
        self.assertEqual(r['mean_rgb'], [12.75, 22.25, 31.75])

    def test_merge_weights_corners_not_prototypes(self):
        a, b = lighting.Errors(), lighting.Errors()
        a.add((100, 100, 100), (0, 0, 0))
        for _ in range(9):
            b.add((0, 0, 0), (0, 0, 0))
        a.merge(b)
        self.assertEqual(a.report()['mean_channel'], 10)
        self.assertEqual(a.report()['p95_channel'], 100)

    def test_no_samples_is_null(self):
        self.assertIsNone(lighting.Errors().report()['mean_channel'])


class TintTest(unittest.TestCase):
    def test_independent_channel_ls_gain(self):
        mean = [(10, 20, 40), (20, 40, 80)]
        truth = [(20, 10, 120), (40, 20, 240)]
        gains = lighting.fit_tint(mean, truth, [1, 1])
        self.assertEqual(gains, (8192, 2048, 12288))
        self.assertEqual([lighting.tint(c, gains) for c in mean], truth)

    def test_zero_denominator_missing_and_clipping(self):
        gains = lighting.fit_tint([(0, 1, 20), (100, 100, 100)],
                                  [(255, 255, 40), (0, 0, 0)], [1, 0])
        self.assertEqual(gains, (4096, 65535, 8192))
        self.assertEqual(lighting.tint((0, 20, 200), gains), (0, 255, 255))

    def test_spatial_change_cannot_be_explained_by_one_tint(self):
        a = triangle(colours=((20, 20, 20),) * 3)
        b = triangle(colours=((100, 100, 100), (20, 20, 20), (20, 20, 20)))
        _, errors = analyse([a], [[a], [b]])
        self.assertGreater(errors['B_tint'].report()['mean_channel'], 0)
        self.assertLess(errors['B_tint'].report()['mean_channel'], errors['A_mean'].report()['mean_channel'])


class CollectionTest(unittest.TestCase):
    def test_grid_matches_full_cut_with_negative_cells_and_large_sphere(self):
        source = [triangle(x) for x in (-60, -10, 0, 10, 60)]
        grid = lighting.CutSource(source)
        for x, r in ((-10, 2), (0, 1000), (0, 0.01), (100, 2)):
            inst = dict(instance(x), r=r)
            self.assertEqual(grid.cut(inst), pg.cut(source, inst))

    def test_selection_is_pg_rule(self):
        a, b, stranger = triangle(), triangle(0.001), triangle(50)
        self.assertEqual(lighting.select_base([[a, stranger], [b]], 1), [a])
        self.assertEqual(lighting.select_base([[a]], 1), [a])
        self.assertIsNone(lighting.select_base([[a], [b]], 2))

    def test_mean_matches_existing_flag_all_placements_and_missing(self):
        base = [triangle(), triangle(10)]
        cuts = [[triangle(colours=((i, 2 * i, 3 * i),) * 3)] for i in range(12)] + [[]]
        raw, masks, means, mismatch = lighting.collect(base, cuts)
        existing = pg.mean_vertex_colours(base, cuts)
        self.assertEqual(means, [c[:3] for t in existing for c in t[2]])
        self.assertEqual(len(raw), 13)
        self.assertEqual(sum(map(sum, masks)), 36)
        self.assertEqual(mismatch, 0)

    def test_reordered_duplicates_match_once_material_mismatch_excluded(self):
        a, b = triangle(), triangle(5)
        raw, masks, _, mismatch = lighting.collect([a, a, b], [[b, triangle(material=4)]])
        self.assertEqual(sum(masks[0]), 3)
        self.assertEqual(mismatch, 1)
        self.assertEqual(len(raw[0]), 27)


class EncodingTest(unittest.TestCase):
    def test_exact_roundtrips_and_slot_sharing(self):
        a = triangle(colours=((10, 20, 30),) * 3)
        b = triangle(colours=((30, 40, 50),) * 3)
        row, errors = analyse([a], [[a], [b], [a]], palette=((10, 20, 30, 255), (30, 40, 50, 255)))
        self.assertEqual(row['lighting_slots'], 1)
        self.assertEqual(row['palette_extra_rgb'], 0)
        self.assertEqual(row['costs']['C_argb']['unique_tables'], 2)
        self.assertEqual(row['costs']['D_palette_slots']['unique_tables'], 2)
        for option in ('C_argb', 'D_palette_slots'):
            self.assertEqual(errors[option].report()['mean_rgb'], [0, 0, 0])
            self.assertEqual(errors[option].report()['p95_rgb'], [0, 0, 0])

    def test_material_scaling_rounding_clipping_uses_export_float32(self):
        a = triangle(colours=((1, 3, 5), (128, 129, 130), (254, 255, 255)))
        _, errors = analyse([a], [[a]], {0: (0.5, 2.0, 1.0, 2.0)})
        self.assertEqual(lighting.pack_rgb(lighting.glb_rgb((1, 3, 5)), (0.5, 2, 1, 2)), (1, 6, 5))
        for option in lighting.OPTIONS:
            self.assertEqual(errors[option].report()['mean_channel'], 0)

    def test_mean_is_before_material_clamp(self):
        a = triangle(colours=((0, 0, 0),) * 3)
        b = triangle(colours=((255, 255, 255),) * 3)
        _, errors = analyse([a], [[a], [b]], {0: (2, 2, 2, 1)})
        self.assertEqual(errors['A_mean'].report()['mean_channel'], 127.5)
        self.assertEqual(errors['A_mean'].report()['p95_channel'], 255)

    def test_mean_merge_requires_split_for_opposite_trajectories(self):
        dark = triangle(colours=((10, 10, 10),) * 3)
        light = triangle(colours=((30, 30, 30),) * 3)
        row, errors = analyse([dark, light], [[dark, light], [light, dark]])
        self.assertEqual(row['baseline_vertices'], 3)
        self.assertEqual(row['lighting_vertices'], 6)
        self.assertEqual(row['extra_vertex_bytes'], 48)
        self.assertEqual(row['lighting_slots'], 2)
        self.assertEqual(errors['D_palette_slots'].report()['mean_channel'], 0)

    def test_palette_widens_after_256_entries(self):
        a = triangle(colours=((0, 0, 0),) * 3)
        palette = [(i, 0, 0, 255) for i in range(256)]
        cuts = [[triangle(colours=((i, 0, 0),) * 3)] for i in range(256)]
        cuts.append([triangle(colours=((0, 0, 1),) * 3)])
        row, errors = analyse([a], cuts, palette=palette)
        self.assertEqual(row['palette_entries'], 257)
        self.assertEqual(row['palette_index_bytes'], 2)
        self.assertEqual(row['palette_extra_rgb'], 1)
        self.assertEqual(errors['D_palette_slots'].report()['p95_channel'], 0)

    def test_unused_palette_entries_removed_and_equal_material_palettes_shared(self):
        a, b = triangle(), triangle(5, material=1)
        row, _ = analyse([a, b], [[a, b]], palette=[(i, 0, 0, 255) for i in range(256)])
        self.assertEqual(row['native_palette_unique_rgb'], 256)
        self.assertEqual(row['palette_entries'], 3)
        self.assertEqual(row['materials'], 2)
        self.assertEqual(row['baked_palette_tables'], 1)
        self.assertEqual(row['costs']['D_palette_slots']['resident_bytes'], 8 + 8 + 12)

    def test_missing_truth_is_not_scored_and_still_costs_storage(self):
        a = triangle()
        row, errors = analyse([a], [[a], []])
        self.assertEqual(row['expected_corners'], 6)
        self.assertEqual(row['matched_corners'], 3)
        self.assertEqual(row['complete_placements'], 1)
        self.assertEqual(row['costs']['B_tint']['added_bytes'], 16)
        self.assertEqual(errors['D_palette_slots'].report()['corners'], 3)

    def test_cell_duplication_alignment_and_reference_cost(self):
        cost = lighting.table_cost([b'a', b'a', b'a', b'abcde'], [(0, 0), (0, 0), (1, 0), (1, 0)], 8)
        self.assertEqual(cost['global_payload_bytes'], 12)
        self.assertEqual(cost['cell_payload_bytes'], 16)
        self.assertEqual(cost['reference_bytes'], 16)
        self.assertEqual(cost['added_bytes'], 40)
        self.assertEqual(cost['added_bytes_per_placement'], 10)
        self.assertEqual(lighting.INSTANCE_BYTES, 52)
        with self.assertRaises(ValueError):
            lighting.table_cost([b'a'], [])

    def test_sampled_working_set_uses_streamed_cell_touch_rule(self):
        cells = {(0, 0): 100, (2, 0): 200, (100, 100): 50}
        sample = lighting.sampled_working_set(cells)
        self.assertEqual(sample['samples'], 3)
        self.assertEqual(sample['max_added_bytes'], 300)
        self.assertEqual(sample['target_xz'], [8, 8])
        self.assertEqual(lighting.sampled_working_set({})['max_added_bytes'], 0)


class NativeAndCliTest(unittest.TestCase):
    def test_native_offsets_palette_and_all_lod_streams(self):
        data = bytearray(7000)
        bucket, inst, palette, indices = 4, 180, 260, 4500
        struct.pack_into('<4B4B4H', data, bucket + 108, 1, 1, 1, 1, 0, 1, 2, 3, 0, 1, 2, 3)
        struct.pack_into('<I4B', data, bucket + 140, palette, 1, 1, 1, 1)
        struct.pack_into('<3I', data, palette, 8, 2, 0)
        for i in range(16):
            struct.pack_into('<4B', data, palette + 12 + 4 * i, i, i + 1, i + 2, 128)
        struct.pack_into('<2I', data, inst + 4, indices, bucket)
        data[indices:indices + 64] = bytes(range(64))
        basics = [(bucket - 4, 'prototype-bucket-tie'), (inst - 4, 'instance-tie'), (240, 'instance-shrubbery')]
        with patch.object(lighting.level_contents, 'basics', return_value=(len(data), basics)):
            records = lighting.native_data(bytes(16) + data)
            self.assertEqual(records[0]['palette'], ((0, 1, 2, 128), (8, 9, 10, 128)))
            self.assertEqual(records[0]['streams'][2], bytes(range(32, 48)))
            self.assertIsNone(records[1])
            self.assertEqual(lighting.native_summary(records[:1])[3]['raw_index_bytes'], 16)
            struct.pack_into('<I', data, palette, 7)
            with self.assertRaisesRegex(ValueError, 'dimensions'):
                lighting.native_data(bytes(16) + data)

    def test_cli_refuses_output_outside_build_without_reading_inputs(self):
        args = ['--levels-root', 'synthetic', '--dgo-dir', 'synthetic', '--json', 'forbidden.json']
        with patch.object(lighting, 'analyse_level') as read, redirect_stderr(StringIO()):
            with self.assertRaises(SystemExit) as raised:
                lighting.main(args)
        self.assertEqual(raised.exception.code, 2)
        read.assert_not_called()

    def test_cli_json_and_default_levels_without_creating_files(self):
        result = {'level': 'synthetic', 'matched_corners': 3, 'measured_placements': 1, 'options': {}}
        args = ['--levels-root', 'synthetic', '--dgo-dir', 'synthetic']
        output = StringIO()
        # Patch all output operations: synthetic tests create no extra files.
        with patch.object(Path, 'exists', return_value=False), patch.object(Path, 'mkdir'), \
                patch.object(Path, 'open') as opened, patch.object(lighting, 'analyse_level', return_value=result) as run, \
                redirect_stdout(output), redirect_stderr(StringIO()):
            lighting.main(args)
        self.assertEqual(run.call_count, 4)
        self.assertEqual([c.args[0] for c in run.call_args_list], list(lighting.LEVEL_DGOS))
        self.assertEqual(opened.call_args.args, ('x',))
        self.assertIn('matched_corners', output.getvalue())


if __name__ == '__main__':
    unittest.main()
