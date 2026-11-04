"""Actual built export receipts and fail-closed registry tests (AI-assisted), ISC."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
from dcsh_tool import read_module, function
from verify_arena_jak_exports import built_exports, decode, NAMES, MODULES


class Exports(unittest.TestCase):
    def test_actual_built_exports_and_first_refusal(self):
        reports = built_exports()
        out = PORT/'build/arena-jak-player-validation'
        out.mkdir(parents=True, exist_ok=True)
        arrays, entries = [], []
        for i, (name, module) in enumerate(zip(NAMES, MODULES)):
            path = PORT/('build/kernel-dcsh' if module == 'gkernel' else 'build/game-dcsh')/(module+'.dcsh')
            code = function(read_module(path), name)
            self.assertEqual(reports[i]['bytes'], len(code))
            arrays.append(f'static const unsigned char code{i}[]={{'+','.join(map(str, code))+'};')
            # Relocations are not executed on the host. Provenance is installed
            # by the actual loader, with the actual code spans from this build.
            entries.append(f'{{"{name}",code{i},sizeof code{i},NULL,0}}')
        (out/'exports_fixture.h').write_text('\n'.join(arrays)+
            '\nstatic const dc_goal_code_export fixture[]={'+','.join(entries)+'};\n')
        env = os.environ.copy()
        env['PATH'] += os.pathsep+'C:/msys64/mingw64/bin'+os.pathsep+str((PORT/'../../../../dc-sdk/sh-elf/bin').resolve())
        cc = shutil.which('gcc', path=env['PATH'])
        self.assertTrue(cc)
        exe = out/'exports-check.exe'
        subprocess.run([cc, '-O2', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I'+str(PORT), '-I'+str(out),
                        *[str(PORT/f) for f in ('tests/arena_jak_exports_check.c', 'arena_jak_exports.c',
                                               'goal_module.c', 'goal_runtime.c')], '-o', str(exe)], check=True, env=env)
        print(subprocess.check_output([str(exe)], text=True, env=env).strip())

    def test_decoder_identifies_each_export_and_bad_record(self):
        blob = bytearray(520)
        struct.pack_into('<4I', blob, 0, 1, 3, 2, 0)
        struct.pack_into('<3I', blob, 268, 1234, 5678, 3)
        for i in range(4):
            struct.pack_into('<15I', blob, 280+i*60, 4, 100+i*16, 0, 0, 9, 5, 7, 800,
                             900, 1000, 3, 400, 700, 800, 22)
        decoded = decode(blob)
        row = decoded['stages'][1]['exports'][2]
        self.assertEqual(row['name'], 'read-pad')
        self.assertEqual(row['at'], 800)
        self.assertEqual(row['record_bytes'], 3)
        self.assertEqual(row['refusal'], 'registry record fields/order')
        self.assertIn('odd code bytes', row['violations'])
        self.assertEqual(decoded['stages'][1]['first_failed'], 3)
        with self.assertRaises(ValueError):
            decode(blob[:-1])


if __name__ == '__main__':
    unittest.main()
