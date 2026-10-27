"""Next-object inventory on a synthetic source tree and report (AI-assisted)."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
from next_objects import next_objects


class NextObjectsTest(unittest.TestCase):
    def test_prefix_source_lines_limit_and_cli(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'goal_src/jak1'
            (source / 'dgos').mkdir(parents=True)
            (source / 'dgos/game.gd').write_text('("GAME.CGO" ("a.o" "b.o" "c.o"))')
            (source / 'game.gp').write_text('(goal-src-sequence "" "a.gc" "b.gc" "c.gc")')
            for name, text in [('a', 'a'), ('b', 'one\ntwo\n'), ('c', '')]:
                (source / f'{name}.gc').write_text(text)
            status = dict(compiled=['a'])
            report = root / 'report.json'; report.write_text(json.dumps(status))
            self.assertEqual(next_objects(root, status, 1),
                             [dict(name='b', source='goal_src/jak1/b.gc', lines=2)])
            out = subprocess.check_output([sys.executable, str(PORT / 'next_objects.py'), '--root',
                                           str(root), '--report', str(report), '-n', '2'], text=True)
            self.assertEqual(out.splitlines(), ['b\tgoal_src/jak1/b.gc\t2', 'c\tgoal_src/jak1/c.gc\t0'])
            self.assertEqual(next_objects(root, dict(compiled=['a', 'b', 'c'])), [])
            with self.assertRaisesRegex(ValueError, 'prefix'):
                next_objects(root, dict(compiled=['b']))
            with self.assertRaises(ValueError):
                next_objects(root, status, -1)
