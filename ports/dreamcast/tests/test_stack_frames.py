"""Stack rankings on synthetic manifests only (AI-assisted)."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
from stack_frames import report


class StackFramesTest(unittest.TestCase):
    def test_ranking_limit_threshold_and_file_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, frames in [('a', [4096, 8192]), ('b', [8192, 32])]:
                (root / f'game_{name}.manifest.json').write_text(json.dumps(dict(functions=[
                    dict(name=f'f{i}', bytes=100, stack_bytes=n) for i, n in enumerate(frames)])))
            (root / 'other.manifest.json').write_text('not JSON')
            result = report(root, 2)
            self.assertEqual(result['total'], 4)
            self.assertEqual(result['exceeds_4096'], 2)
            self.assertEqual([(f['file'], f['name']) for f in result['largest']],
                             [('game_a.manifest.json', 'f1'), ('game_b.manifest.json', 'f0')])
            run = subprocess.run([sys.executable, str(PORT / 'stack_frames.py'), '--build', str(root),
                                  '-n', '2', '--json'], capture_output=True, text=True, check=True)
            self.assertEqual(json.loads(run.stdout), result)
            text = subprocess.check_output([sys.executable, str(PORT / 'stack_frames.py'),
                                            '--build', str(root), '-n', '0'], text=True)
            self.assertEqual(text.strip(), '2 of 4 functions exceed 4096 bytes')

    def test_empty_and_negative_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(report(directory), dict(total=0, exceeds_4096=0, largest=[]))
            with self.assertRaises(ValueError):
                report(directory, -1)
