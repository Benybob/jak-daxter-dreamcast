"""Every argparse tool must offer bounded, dependency-light help (AI-assisted)."""
import ast
from pathlib import Path
import subprocess
import sys
import unittest

PORT = Path(__file__).resolve().parents[1]
# Import-time tool/frontend dependencies only, with a specific reason per name.
# Currently no argparse script requires these dependencies merely to show help.
SKIP = {}


def argparse_scripts():
    result = []
    for path in sorted(PORT.glob('*.py')):
        tree = ast.parse(path.read_text(encoding='utf-8'), filename=str(path))
        if any((isinstance(node, ast.Import) and any(a.name == 'argparse' for a in node.names))
               or (isinstance(node, ast.ImportFrom) and node.module == 'argparse')
               for node in ast.walk(tree)):
            result.append(path)
    return result


class ToolHelpTest(unittest.TestCase):
    def test_inventory_and_skip_reasons(self):
        names = {p.name for p in argparse_scripts()}
        self.assertTrue(names)
        self.assertLessEqual(set(SKIP), names, 'remove stale skip entries')
        self.assertTrue(all(isinstance(reason, str) and reason.strip() for reason in SKIP.values()))


def help_test(path):
    def test(self):
        run = subprocess.run([sys.executable, str(path), '--help'], cwd=PORT.parents[1],
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, f'{path.name}:\n{run.stdout}\n{run.stderr}')
    return unittest.skip(SKIP[path.name])(test) if path.name in SKIP else test


for script in argparse_scripts():
    setattr(ToolHelpTest, 'test_help_' + script.stem, help_test(script))
