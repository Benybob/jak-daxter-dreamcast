"""Synthetic coverage, RGB565, diff and CLI threshold tests. (AI-assisted), ISC."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import frame_compare as compare
from dci1_reference import read_ppm, write_ppm


BACKGROUND = (115, 158, 204)


def square(xmin, ymin, size, w=8, h=6):
    rgb = bytearray(bytes(BACKGROUND) * (w * h))
    for y in range(ymin, ymin + size):
        for x in range(xmin, xmin + size):
            index = (y * w + x) * 3
            rgb[index:index + 3] = b'\xff\0\0'
    return rgb


class FrameTests(unittest.TestCase):
    def test_identical_and_empty_frames(self):
        for frame in (square(1, 1, 3), bytearray(bytes(BACKGROUND) * 48)):
            result = compare.compare(frame, frame, 8, 6, BACKGROUND)
            self.assertEqual(result, dict(coverage_agreement=1., mean_abs_error=[0., 0., 0.],
                             max_abs_error=[0, 0, 0], pixels_only_in_a=0,
                             pixels_only_in_b=0, largest_disagreement_bbox=None))
            self.assertEqual(compare.diff_image(frame, frame, BACKGROUND), bytearray([32] * 144))

    def test_shifted_square_and_diff(self):
        a, b = square(1, 1, 3), square(2, 1, 3)
        result = compare.compare(a, b, 8, 6, BACKGROUND)
        self.assertEqual(result['pixels_only_in_a'], 3)
        self.assertEqual(result['pixels_only_in_b'], 3)
        self.assertEqual(result['coverage_agreement'], 42 / 48)
        self.assertEqual(result['largest_disagreement_bbox'], [1, 1, 1, 3])
        diff = compare.diff_image(a, b, BACKGROUND)
        self.assertEqual(diff[(1 * 8 + 1) * 3:(1 * 8 + 1) * 3 + 3], b'\xff\0\0')
        self.assertEqual(diff[(1 * 8 + 4) * 3:(1 * 8 + 4) * 3 + 3], b'\0\xff\0')

    def test_error_is_per_channel_shared_foreground_only(self):
        a = bytearray([100, 120, 140, 50, 60, 70, *BACKGROUND])
        b = bytearray([110, 100, 150, 54, 60, 62, 255, 0, 0])
        result = compare.compare(a, b, 3, 1, BACKGROUND, 0)
        self.assertEqual(result['mean_abs_error'], [7., 10., 9.])
        self.assertEqual(result['max_abs_error'], [10, 20, 10])
        self.assertEqual(result['pixels_only_in_b'], 1)

    def test_tolerance_boundary(self):
        a = bytearray(BACKGROUND)
        b = bytearray([BACKGROUND[0] + 24, *BACKGROUND[1:]])
        self.assertEqual(compare.compare(a, b, 1, 1, BACKGROUND)['coverage_agreement'], 1.)
        b[0] += 1
        self.assertEqual(compare.compare(a, b, 1, 1, BACKGROUND)['coverage_agreement'], 0.)

    def test_four_connectivity_no_row_wrap_and_largest(self):
        a = bytearray([0] * (4 * 3 * 3))
        b = a.copy()
        # Index 3 and index 4 must not connect across the row boundary.
        for index in (3, 4, 8):
            a[index * 3] = 255
        result = compare.compare(a, b, 4, 3, (0, 0, 0), 0)
        self.assertEqual(result['largest_disagreement_bbox'], [0, 1, 0, 2])
        # Opposite coverage classifications still connect in the XOR mask.
        a = bytearray([255, 0, 0, 0, 0, 0])
        b = bytearray([0, 0, 0, 255, 0, 0])
        self.assertEqual(compare.compare(a, b, 2, 1, (0, 0, 0), 0)
                         ['largest_disagreement_bbox'], [0, 0, 1, 0])

    def test_quantize565_expanded_copy_and_idempotence(self):
        original = bytearray([123, 155, 201, 31, 90, 245])
        # Independent nearest-code and bit-replication calculation.
        expanded = bytearray()
        for i, value in enumerate(original):
            bits = 6 if i % 3 == 1 else 5
            level = int(value * ((1 << bits) - 1) / 255 + 0.5)
            expanded.append((level << (8 - bits)) | (level >> (2 * bits - 8)))
        self.assertEqual(compare.quantize565(original), expanded)
        self.assertEqual(compare.quantize565(expanded), expanded)
        result = compare.compare(compare.quantize565(original), compare.quantize565(expanded),
                                 2, 1, (0, 0, 0), 0)
        self.assertEqual(result['max_abs_error'], [0, 0, 0])
        # All 256 input codes, in all three channel positions, are idempotent.
        ramp = bytearray(c for c in range(256) for _ in range(3))
        quantized = compare.quantize565(ramp)
        self.assertEqual(compare.quantize565(quantized), quantized)

    def test_invalid_inputs(self):
        for args in ((b'', b'', 0, 0, BACKGROUND, 24),
                     (b'123', b'12', 1, 1, BACKGROUND, 24),
                     (b'123', b'123', 1, 1, (0, 0), 24),
                     (b'123', b'123', 1, 1, BACKGROUND, -1)):
            with self.subTest(args=args):
                with self.assertRaises(ValueError):
                    compare.compare(*args)

    def test_cli_threshold_diff_json_and_quantize(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            write_ppm(path / 'a.ppm', 8, 6, square(1, 1, 3))
            write_ppm(path / 'b.ppm', 8, 6, square(2, 1, 3))
            command = [sys.executable, compare.__file__, str(path / 'a.ppm'), str(path / 'b.ppm'),
                       '--diff', str(path / 'diff.ppm'), '--json', str(path / 'out.json')]
            for threshold, status in (('0.9', 1), ('0.875', 0), ('0.8', 0)):
                run = subprocess.run(command + ['--min-agreement', threshold],
                                     capture_output=True, text=True, timeout=30)
                self.assertEqual(run.returncode, status, run.stderr)
                self.assertEqual(json.loads(run.stdout)['coverage_agreement'], 0.875)
            self.assertEqual(read_ppm(path / 'diff.ppm')[:2], (8, 6))
            self.assertEqual(json.loads((path / 'out.json').read_text())['pixels_only_in_a'], 3)
            a = bytearray([123, 155, 201, 31, 90, 245])
            write_ppm(path / 'a.ppm', 2, 1, a)
            write_ppm(path / 'b.ppm', 2, 1, compare.quantize565(a))
            run = subprocess.run(command + ['--quantize565', '--tolerance', '0'],
                                 capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(json.loads(run.stdout)['max_abs_error'], [0, 0, 0])


if __name__ == '__main__':
    unittest.main()
