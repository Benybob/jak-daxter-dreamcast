"""memory_trace.py reads the Flycast arena dump correctly (AI-assisted), ISC."""
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]


class MemoryTraceTest(unittest.TestCase):
    def run_trace(self, failed, completed, after):
        names = json.loads((PORT / 'build/game-cgo-report.json').read_text())['compiled']
        raw = struct.pack('<7I', 0x4d454d54, len(after) + 1, completed, failed, 8388608, 1000, 4096)
        raw += struct.pack(f'<{len(after)}I', *after) + bytes(2076 - 28 - 4 * len(after))
        with tempfile.TemporaryDirectory() as tmp:
            dump = Path(tmp) / 'dump.bin'
            dump.write_bytes(raw)
            out = subprocess.run([sys.executable, str(PORT / 'memory_trace.py'), str(dump), '--top', '2'],
                                 capture_output=True, text=True, check=True).stdout
        return names, out

    @unittest.skipUnless((PORT / 'build/game-cgo-report.json').is_file(), 'build GAME.CGO first')
    def test_deltas_failure_and_ranking(self):
        names, out = self.run_trace(2, 2, [1500, 9000])
        lines = out.splitlines()
        self.assertIn('before GAME.CGO 1000; after 2/3 objects 9000', lines[0])
        self.assertIn(f'object 2 ({names[2]}) failed; last refused request 4096 bytes', lines[1])
        # largest delta first: object 1 grew 7500, object 0 grew 500
        self.assertEqual(lines[2].split(), ['7500', names[1]])
        self.assertEqual(lines[3].split(), ['500', names[0]])

    def test_missing_magic_refuses(self):
        with tempfile.TemporaryDirectory() as tmp:
            dump = Path(tmp) / 'dump.bin'
            dump.write_bytes(bytes(2076))
            r = subprocess.run([sys.executable, str(PORT / 'memory_trace.py'), str(dump)], capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('no memory trace', r.stderr + r.stdout)


if __name__ == '__main__':
    unittest.main()
