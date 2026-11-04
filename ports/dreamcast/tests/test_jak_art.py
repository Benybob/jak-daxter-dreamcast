"""Retail data relocation and memory admission, no emulator (AI-assisted), ISC."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT))
import stage_jak_art as stage  # noqa: E402

OUT = PORT / 'build/jak-art-validation'
STAGED = PORT / 'build/jak-art-disc'


class JakArt(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        manifest = STAGED / 'manifest.json'
        if not manifest.exists():
            raise unittest.SkipTest('stage the owner\'s GAME.CGO with stage_jak_art.py first')
        cls.manifest = json.loads(manifest.read_text())
        cls.objects = list(stage.archive_objects(STAGED / 'JAKART.DGO'))
        cls.env = os.environ.copy()
        cls.env['PATH'] += os.pathsep + 'C:/msys64/mingw64/bin' + os.pathsep + str(
            Path('C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk/sh-elf/bin'))
        cls.cc = shutil.which('gcc', path=cls.env['PATH'])
        if not cls.cc:
            raise RuntimeError('host gcc unavailable')
        OUT.mkdir(exist_ok=True, parents=True)
        bindings = [b for _, _, obj in cls.objects for b in stage.decode_v4(obj)[2]]
        types = sorted({n for n, m, _ in bindings if m is not None})
        symbols = sorted({n for n, m, _ in bindings if m is None})
        (OUT / 'art_names.h').write_text(
            '/* Generated test names (AI-assisted), ISC; not committed. */\n' +
            'static const char *const art_types[]={' + ','.join(json.dumps(n) for n in types) + '};\n' +
            'static const char *const art_symbols[]={' + ','.join(json.dumps(n) for n in symbols) + '};\n')
        cls.exe = OUT / 'jak-art-check.exe'
        subprocess.run([cls.cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(PORT),
                        str(PORT / 'tests/jak_art_check.c'), str(PORT / 'goal_art.c'),
                        str(PORT / 'goal_runtime.c'), '-o', str(cls.exe)], env=cls.env, check=True)

    def test_staged_objects_are_complete_retail_bytes(self):
        source = {name: obj for _, name, obj in stage.archive_objects(Path(self.manifest['source']))
                  if name in stage.WANTED and stage.u32(obj, 8) == 4}
        self.assertEqual([n for _, n, _ in self.objects], list(stage.WANTED))
        for (_, name, obj), row in zip(self.objects, self.manifest['objects']):
            self.assertEqual(obj, source[name])
            self.assertEqual(hashlib.sha256(obj).hexdigest(), row['sha256'])
        self.assertEqual(self.manifest['resident_bytes'], 1979744)

    def test_all_linked_words_and_memory_admission(self):
        linked = OUT / 'linked.bin'
        run = subprocess.run([str(self.exe), str(STAGED / 'JAKART.DGO'), str(linked)],
                             env=self.env, capture_output=True, text=True, check=True)
        (OUT / 'host-result.txt').write_text(run.stdout)
        addresses = {}
        for line in run.stdout.splitlines():
            parts = line.split()
            if parts[0] == 'base':
                base = int(parts[1])
            elif parts[0] in ('type', 'symbol'):
                addresses[tuple(parts[:2])] = int(parts[2])
        expected = bytearray()
        total_pointers = total_bindings = 0
        for _, _, obj in self.objects:
            data, pointers, bindings = stage.decode_v4(obj)
            data = bytearray(data)
            for at in pointers:
                struct.pack_into('<I', data, at, base + stage.u32(data, at))
            for name, methods, offsets in bindings:
                value = addresses['type' if methods is not None else 'symbol', name]
                for at in offsets:
                    struct.pack_into('<I', data, at, value)
                total_bindings += len(offsets)
            total_pointers += len(pointers)
            expected += data
            base += len(data)
        self.assertEqual(linked.read_bytes(), expected)
        print(f'{len(expected)} linked bytes match independent Python oracle; '
              f'{total_pointers} pointers, {total_bindings} bindings; real OOM/exact-fit checks passed')

    def test_corrupt_link_table_and_container_refuse_before_login(self):
        original = bytearray((STAGED / 'JAKART.DGO').read_bytes())
        cases = {}
        bad = original.copy()
        struct.pack_into('<I', bad, 128 + 8, 3)  # retail code rejected
        cases['code'] = bad
        cases['tail'] = original + b'\0'
        bad = original.copy()
        struct.pack_into('<I', bad, 128 + 16 + stage.u32(bad, 128 + 12) + 8, 3)
        cases['link-version'] = bad
        bad = original.copy()
        data, pointers, _ = stage.decode_v4(self.objects[0][2])
        struct.pack_into('<I', bad, 128 + 16 + pointers[0], len(data))
        cases['pointer-outside'] = bad
        for name, data in cases.items():
            path = OUT / (name + '.dgo')
            path.write_bytes(data)
            subprocess.run([str(self.exe), str(path), str(OUT / 'unused.bin'), 'bad'],
                           env=self.env, check=True, capture_output=True, text=True)

    def test_no_retail_output_outside_build(self):
        with self.assertRaisesRegex(ValueError, 'must be under'):
            stage.stage(Path('unused'), PORT / 'romdisk')

    def test_isolated_budget_wrapper(self):
        exe = OUT / 'jak-art-budget-check.exe'
        subprocess.run([self.cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-DDC_ARENA_JAK_ART', '-I', str(PORT),
                        *[str(PORT / p) for p in ('tests/jak_art_budget_check.c', 'arena_jak_art.c',
                                                  'goal_art.c', 'goal_runtime.c', 'goal_module.c')],
                        '-o', str(exe)], env=self.env, check=True)
        dump = OUT / 'host-budget.bin'
        run = subprocess.run([str(exe), str(dump)], env=self.env, check=True, capture_output=True, text=True)
        w = struct.unpack('<25I', dump.read_bytes())
        self.assertEqual(w[0:4], (1, 1318288, 661456, 981600))
        self.assertEqual(w[4:10], (10673360, 11010048, 0, 1979744, 336688, 1643056))
        self.assertEqual(w[10:17], (0,) * 7)
        self.assertEqual(w[17:22], (10673360, 2, 1979744, 0, 0))
        self.assertEqual(w[22:25], (2621440, 1255392, 1255392))
        print(run.stdout.strip())


if __name__ == '__main__':
    unittest.main()
