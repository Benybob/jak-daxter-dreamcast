"""Test C-kernel service survey (AI-assisted).

ISC License
Copyright (c) 2026
Permission to use, copy, modify, and/or distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright notice
and this permission notice appear in all copies.
THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
"""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'survey_c_kernel_services.py'
spec = importlib.util.spec_from_file_location('services', SCRIPT)
services = importlib.util.module_from_spec(spec); spec.loader.exec_module(services)

class ServiceSurveyTests(unittest.TestCase):
    def test_fake_tree_and_cli(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def put(path, text):
                target = root/path; target.parent.mkdir(parents=True, exist_ok=True); target.write_text(text)
            put('game/kernel/jak1/a.cpp', """make_function_symbol_from_c("missing", f);
make_stack_arg_function_symbol_from_c("provided", f);
// make_function_symbol_from_c("comment", f);
make_function_symbol_from_c("unused", f);""")
            put('game/kernel/common/b.cpp', 'make_func_symbol_func(\n"missing", f); make_func_symbol_func("other", f);')
            put('ports/dreamcast/goal_bridge.c', 'char *x = "provided"; /* "missing" */')
            put('ports/dreamcast/build/game_a.ir.json', json.dumps({'functions': [{'instructions': [
                {'kind':'symbol_value','symbol':'missing'},
                {'kind':'function_address','function':'missing'},
                {'kind':'function_address','symbol':'other'},
                {'kind':'symbol_pointer','symbol':'unused'}]}]}))
            result = services.survey(root)
            rows = result['missing']
            self.assertEqual([r['name'] for r in rows], ['missing', 'other', 'unused'])
            self.assertEqual(rows[0]['reference_count'], 2)
            self.assertEqual(len(rows[0]['desktop_sources']), 2)
            self.assertEqual(rows[0]['game_files'], {'ports/dreamcast/build/game_a.ir.json':2})
            output = subprocess.check_output([sys.executable, str(SCRIPT), '--root', str(root)], text=True)
            self.assertEqual(json.loads(output), result)
            (root/'ports/dreamcast/build/game_a.ir.json').unlink()
            self.assertEqual(services.survey(root)['ir_files_scanned'], 0)

if __name__ == '__main__': unittest.main()
