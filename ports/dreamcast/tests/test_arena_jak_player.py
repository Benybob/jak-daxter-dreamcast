"""Private budget/data contracts and receipt refusals (AI-assisted), ISC.

Host fixtures only; no original SH-4 execution is claimed by these tests.
"""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(PORT), str(PORT/'tests')]
import stage_jak_art as stage
from verify_arena_jak_player import audit, WORDS


class JakPlayer(unittest.TestCase):
    def test_overlapping_boot_tail_is_protected_from_real_allocator(self):
        env=os.environ.copy();env['PATH']+=os.pathsep+'C:/msys64/mingw64/bin'
        cc=shutil.which('gcc',path=env['PATH']);self.assertTrue(cc)
        out=PORT/'build/arena-jak-player-validation';out.mkdir(parents=True,exist_ok=True)
        exe=out/'boot-tail-check.exe'
        subprocess.run([cc,'-O2','-std=c99','-Wall','-Wextra','-Werror','-I',str(PORT),
                        str(PORT/'tests/jak_boot_tail_check.c'),str(PORT/'goal_runtime.c'),'-o',str(exe)],env=env,check=True)
        print(subprocess.check_output([str(exe)],env=env,text=True).strip())

    def test_receipt_auditor_rejects_fake_or_stalled_player(self):
        w = dict.fromkeys(WORDS, 0)
        w.update(version=1, phase=3, frames=160, pair_available=1525008, pair_shortfall=454736,
                 target=1, thread=2, init_entry=3, read_entry=4, read_shim=5, control=6, cpad=7,
                 read_calls=160, read_returns=160, attempts=160, resumes=160,
                 pad_services=160, pad_valid=160, moving_reads=64, neutral_reads=96,
                 moving_changes=64, neutral_stable=16, state_changes=1, restored=1)
        w.update(sidekick_available=100000,sidekick_shortfall=561456)
        b = bytearray(struct.pack('<31I', *(w[k] for k in WORDS)))
        x = 0
        for i in range(160):
            joy = 96 if 32 <= i < 64 else -96 if 96 <= i < 128 else 0
            x += 32 if joy > 0 else -32 if joy < 0 else 0
            b += struct.pack('<6IiI4f', i+1, 100+(i>=16), 200, i+1, i+1, i+1, joy, 0,
                             x, 0, 0, .5 if joy else 0)
        self.assertEqual(audit(b, host_fixture=True)['moving_changes'], 64)
        for key, value in [('read_returns',0), ('resumes',0), ('phase',2), ('failure',20),
                           ('pair_shortfall',0), ('restored',0), ('read_shim',4)]:
            bad = b.copy();struct.pack_into('<I', bad, WORDS.index(key)*4, value)
            with self.subTest(key=key), self.assertRaises(ValueError):
                audit(bad, host_fixture=True)
        for offset, fmt, value in [(124+32*48+24,'i',0), (124+80*48+12,'I',1),
                                    (124+40*48+32,'f',float('nan'))]:
            bad=b.copy();struct.pack_into('<'+fmt,bad,offset,value)
            with self.assertRaises(ValueError):audit(bad, host_fixture=True)
        with self.assertRaises(ValueError):audit(b)
        with self.assertRaises(ValueError):audit(b[:-1], host_fixture=True)

    def test_eichar_complete_relocations_and_rebalanced_admission(self):
        manifest=PORT/'build/jak-art-disc/manifest.json'
        if not manifest.exists():self.skipTest('stage owner GAME.CGO first')
        info=json.loads(manifest.read_text())
        stage.stage(Path(info['source']), manifest.parent)
        one=list(stage.archive_objects(manifest.parent/'EICHAR.DGO'))
        self.assertEqual([name for _,name,_ in one], ['eichar'])
        pair=list(stage.archive_objects(manifest.parent/'JAKART.DGO'))
        self.assertEqual(one[0][2],pair[0][2])
        env=os.environ.copy();env['PATH']+=os.pathsep+'C:/msys64/mingw64/bin'
        cc=shutil.which('gcc',path=env['PATH']);self.assertTrue(cc)
        out=PORT/'build/arena-jak-player-validation';out.mkdir(parents=True,exist_ok=True)
        exe=out/'eichar-check.exe';linked=out/'eichar-linked.bin'
        subprocess.run([cc,'-O2','-std=c99','-Wall','-Wextra','-Werror','-DDC_ARENA_JAK_PLAYER',
                        '-I',str(PORT),str(PORT/'tests/jak_eichar_check.c'),str(PORT/'goal_art.c'),
                        str(PORT/'goal_runtime.c'),'-o',str(exe)],env=env,check=True)
        result=subprocess.check_output([str(exe),str(manifest.parent/'EICHAR.DGO'),str(linked)],env=env,text=True)
        addresses={};base=None
        for line in result.splitlines():
            parts=line.split()
            if parts[0]=='base':base=int(parts[1])
            else:addresses[tuple(parts[:2])]=int(parts[2])
        data,pointers,bindings=stage.decode_v4(one[0][2]);expected=bytearray(data)
        for at in pointers:struct.pack_into('<I',expected,at,base+stage.u32(data,at))
        for name,methods,offsets in bindings:
            for at in offsets:struct.pack_into('<I',expected,at,addresses['type' if methods is not None else 'symbol',name])
        self.assertEqual(linked.read_bytes(),expected)
        budget=json.loads((PORT/'build/arena-jak-budget/budget.json').read_text())
        available=budget['goal_bytes']-10673360
        print(f'1318288 complete eichar linked bytes; 18523 pointer fixes; pair short {1979744-available}, eichar tail {available-1318288} at old snapshot')

    def test_fixture_budget_has_margin_and_same_total(self):
        p=PORT/'build/arena-jak-budget/budget.json'
        if not p.exists():self.skipTest('measure fixture first')
        d=json.loads(p.read_text())
        self.assertEqual(d['slab_bytes']+d['goal_bytes'],13*1024*1024)
        self.assertGreaterEqual(d['slab_bytes']-d['measured_host_high_water'],d['margin'])
        self.assertEqual(d['returned_bytes'],2621440-d['slab_bytes'])
        self.assertEqual(d['frames'],181)

    def test_build_parameter_rejects_missing_margin_and_bad_alignment(self):
        p=PORT/'build/arena-jak-budget/budget.json'
        if not p.exists():self.skipTest('measure fixture first')
        d=json.loads(p.read_text());slab=d['slab_bytes']
        env=os.environ.copy();env['PATH']+=os.pathsep+'C:/msys64/mingw64/bin'
        cc=shutil.which('gcc',path=env['PATH']);self.assertTrue(cc)
        for value,valid in ((slab-32,False),(slab,True),(slab+32,True),(slab+1,False),(2621472,False)):
            with self.subTest(value=value):
                r=subprocess.run([cc,'-std=c99','-fsyntax-only','-x','c','-',
                                  '-I',str(PORT),f'-DDC_JAK_RENDER_SLAB_BYTES={value}'],
                                 input='#include "arena_jak_memory.h"\n',text=True,capture_output=True,env=env)
                self.assertEqual(r.returncode==0,valid,r.stderr)


if __name__ == '__main__':unittest.main()
