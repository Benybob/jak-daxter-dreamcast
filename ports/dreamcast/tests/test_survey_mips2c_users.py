"""Test mips2c usage survey (AI-assisted).

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

SCRIPT = Path(__file__).resolve().parents[1] / 'survey_mips2c_users.py'
spec = importlib.util.spec_from_file_location('mipsusers', SCRIPT)
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)

class MipsUsersTests(unittest.TestCase):
    def test_fake_tree_and_cli(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def put(path, text):
                target = root/path; target.parent.mkdir(parents=True, exist_ok=True); target.write_text(text)
            put('game/mips2c/jak1_functions/a.cpp', 'gLinkedFunctionTable.reg("draw", execute, 128); gLinkedFunctionTable.reg("(method 12 mesh)", execute, 128); // gLinkedFunctionTable.reg("ignored", f, 1);')
            put('game/mips2c/jak1_functions/b.cpp', 'gLinkedFunctionTable.reg("outside", execute, 128);')
            put('goal_src/jak1/dgos/game.gd', '("GAME.CGO" ("inside.o")) ; "outside.o"')
            put('goal_src/jak1/engine/inside.gc', """(def-mips2c draw (function none))
(defmethod-mips2c "(method 12 mesh)" 12 mesh)
(__pc-get-mips2c "draw")
; (def-mips2c ignored x)
#| (def-mips2c ignored x) #| nested |# |#
(__pc-get-mips2c "unregistered")""")
            put('goal_src/jak1/engine/outside.gc', '(def-mips2c outside (function none))')
            result = module.survey(root)
            self.assertEqual(result['modules'][0]['game_cgo_callers'], 3)
            self.assertEqual(len(result['modules'][0]['callers']), 3)
            self.assertFalse(result['modules'][1]['callers'][0]['in_game_cgo'])
            self.assertEqual(result['unregistered_callers'][0]['name'], 'unregistered')
            run = subprocess.run([sys.executable, str(SCRIPT), '--root', str(root)], capture_output=True, text=True, check=True)
            self.assertEqual(json.loads(run.stdout), result)
            self.assertIn('GAME callers', run.stderr)

if __name__ == '__main__': unittest.main()
