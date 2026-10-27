"""memory_trace.py reads the Flycast arena dump correctly (AI-assisted), ISC."""
import json
import csv
import io
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]


class MemoryTraceTest(unittest.TestCase):
    def run_trace(self, failed, completed, after, *options, compare=None):
        names = ['first', 'second, "quoted"', 'third']
        raw = struct.pack('<7I', 0x4d454d54, len(after) + 1, completed, failed, 8388608, 1000, 4096)
        raw += struct.pack(f'<{len(after)}I', *after) + bytes(2076 - 28 - 4 * len(after))
        with tempfile.TemporaryDirectory() as tmp:
            script = Path(tmp) / 'memory_trace.py'
            shutil.copyfile(PORT / 'memory_trace.py', script)
            report = Path(tmp) / 'build/game-cgo-report.json'
            report.parent.mkdir()
            report.write_text(json.dumps({'compiled': names}))
            dump = Path(tmp) / 'dump.bin'
            dump.write_bytes(raw)
            if compare is not None:
                other = Path(tmp) / 'other.bin'
                other.write_bytes(struct.pack('<7I', 0x4d454d54, len(compare), len(compare),
                                              0xffffffff, 8388608, 2000, 0)
                                  + struct.pack(f'<{len(compare)}I', *compare))
                options += ('--compare', str(other))
            out = subprocess.run([sys.executable, str(script), str(dump), '--top', '2', *options],
                                 capture_output=True, text=True, check=True).stdout
        return names, out

    def test_deltas_failure_and_ranking(self):
        names, out = self.run_trace(2, 2, [1500, 9000])
        lines = out.splitlines()
        self.assertIn('before GAME.CGO 1000; after 2/3 objects 9000', lines[0])
        self.assertIn(f'object 2 ({names[2]}) failed; last refused request 4096 bytes', lines[1])
        # largest delta first: object 1 grew 7500, object 0 grew 500
        self.assertEqual(lines[2].split(maxsplit=1), ['7500', names[1]])
        self.assertEqual(lines[3].split(), ['500', names[0]])

    def test_csv_columns_trace_order_and_quoting(self):
        names, out = self.run_trace(0xffffffff, 3, [1500, 9000, 9000], '--csv', '--top', '1')
        rows = list(csv.reader(io.StringIO(out)))
        self.assertEqual(rows, [['index', 'name', 'bytes_before', 'bytes_after', 'delta'],
                                ['0', names[0], '1000', '1500', '500'],
                                ['1', names[1], '1500', '9000', '7500'],
                                ['2', names[2], '9000', '9000', '0']])

    def test_csv_partial_failure_has_only_completed_objects(self):
        names, out = self.run_trace(1, 1, [1500, 9000], '--csv')
        self.assertEqual(list(csv.reader(io.StringIO(out))),
                         [['index', 'name', 'bytes_before', 'bytes_after', 'delta'],
                          ['0', names[0], '1000', '1500', '500']])

    def test_csv_empty_trace_has_header_only(self):
        _, out = self.run_trace(0xffffffff, 0, [], '--csv')
        self.assertEqual(out.strip(), 'index,name,bytes_before,bytes_after,delta')

    def test_missing_magic_refuses(self):
        with tempfile.TemporaryDirectory() as tmp:
            dump = Path(tmp) / 'dump.bin'
            dump.write_bytes(bytes(2076))
            r = subprocess.run([sys.executable, str(PORT / 'memory_trace.py'), str(dump)], capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('no memory trace', r.stderr + r.stdout)

    def test_compare_uses_per_object_deltas_not_cumulative_totals(self):
        names, out = self.run_trace(0xffffffff, 2, [1500, 9000], compare=[2700, 9600])
        self.assertEqual(out.splitlines(), ['change = OTHER_DUMP delta - DUMP delta',
                                           f'0\t{names[0]}\t500\t700\t+200',
                                           f'1\t{names[1]}\t7500\t6900\t-600'])
        _, out = self.run_trace(0xffffffff, 2, [1500, 9000], '--csv', compare=[2700, 9600])
        self.assertEqual(list(csv.reader(io.StringIO(out)))[2], ['1', names[1], '7500', '6900', '-600'])

    def test_compare_mismatched_completed_prefix_refuses(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.run_trace(0xffffffff, 1, [1500], compare=[2700, 9600])


if __name__ == '__main__':
    unittest.main()
