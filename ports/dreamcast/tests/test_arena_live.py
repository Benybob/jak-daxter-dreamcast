"""Host fixtures and strict refusal paths, no original execution (AI-assisted), ISC."""
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]


class ArenaLiveTest(unittest.TestCase):
    def compile_run(self, source, observer=False):
        env = os.environ.copy()
        if os.name == 'nt':
            env['PATH'] += ';C:/msys64/mingw64/bin'
        cc = os.environ.get('HOST_CC') or shutil.which('gcc', path=env['PATH'])
        self.assertTrue(cc, 'set HOST_CC to a host C compiler')
        with tempfile.TemporaryDirectory(prefix='arena-live-') as tmp:
            for fast in (False, True):
                exe = Path(tmp) / ('check.exe' if os.name == 'nt' else 'check')
                low = (['-Wl,--image-base,0x10000000', '-Wl,--disable-dynamicbase']
                       if os.name == 'nt' else ['-no-pie']) if observer else []
                sources = [PORT / source, PORT / 'arena_live_math.c', PORT / 'arena_live_input.c',
                           PORT / 'goal_runtime.c', PORT / 'render_draw_window.c', PORT / 'instance_pass.c']
                sources += [PORT / 'goal_pad.c']
                if observer:
                    sources += [PORT / 'goal_module.c']
                else:
                    sources += [PORT / 'arena_live_assets.c', PORT / 'sector_inflate_stored.c']
                command = [cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-DDC_ARENA_LIVE',
                           *(['-ffast-math'] if fast else []), *low, '-I', str(PORT),
                           *map(str, sources), '-lm', '-o', str(exe)]
                result = subprocess.run(command, env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                asset_root = Path(tmp) / 'assets'
                if not observer and not asset_root.exists():
                    (asset_root / 'cells').mkdir(parents=True)
                    (asset_root / 'cells/-2_1.dci').write_bytes(bytes(52))
                result = subprocess.run([str(exe), str(Path(tmp) / 'evidence.bin') if observer else str(asset_root)], env=env,
                                        timeout=20, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(('fast-math ' if fast else 'O2 ') + result.stdout.strip())
                if observer:
                    sys.path.insert(0, str(PORT))
                    from verify_arena_live import audit, TRACE_BYTES
                    from analyze_arena_live_path import decode
                    blob = (Path(tmp) / 'evidence.bin').read_bytes()
                    audit(blob, host_fixture=True)
                    # The diagnostic decoder must expose a failed prefix as
                    # partial evidence, never a complete set of successful original updates.
                    partial = bytearray(blob)
                    struct.pack_into('<I', partial, 7*4, 26)
                    struct.pack_into('<I', partial, 380+25*TRACE_BYTES+176, 0)
                    partial[380+26*TRACE_BYTES:] = bytes(155*TRACE_BYTES)
                    rows, summary = decode(partial, dict(start=[-281.192993,31.275928,-280.458771],
                                                         crop=[-332,-235,-330,-234]))
                    self.assertEqual((summary['witnessed'],summary['drawn'],summary['unrecorded']), (26,25,155))
                    self.assertEqual(rows[25]['status'], 'witnessed_not_drawn')
                    self.assertTrue(all(r['status']=='unrecorded' for r in rows[26:]))
                    self.assertFalse(summary['acceptance'])
                    # A short capture is diagnostic only, including v3 and historical v2.
                    rows_short, summary_short = decode(partial[:380+61*TRACE_BYTES],
                        dict(start=[-281.192993,31.275928,-280.458771],
                             crop=[-332,-235,-330,-234]))
                    self.assertEqual(summary_short['uncaptured_slots'], 120)
                    self.assertEqual(len(rows_short), 61)
                    self.assertFalse(summary_short['acceptance'])
                    self.assertFalse(summary_short['complete_capture'])
                    # Convert the unchanged prefix fields to the historical ABI.
                    legacy = bytearray(partial[:380])
                    struct.pack_into('<I', legacy, 0, 2)
                    for i in range(61):
                        legacy += partial[380+i*TRACE_BYTES:380+i*TRACE_BYTES+212]
                    self.assertEqual(len(legacy), 13312)
                    self.assertEqual(decode(legacy, dict(start=[-281.192993,31.275928,-280.458771],
                                                        crop=[-332,-235,-330,-234]))[1]['uncaptured_slots'], 120)
                    self.assertFalse(decode(legacy, dict(start=[0,0,0],crop=[-999,999,-999,999]))[1]['complete_capture'])
                    # Even an apparently successful summary cannot make a prefix complete.
                    legacy = bytearray(blob[:380])
                    struct.pack_into('<I', legacy, 0, 2)
                    for i in range(61):
                        legacy += blob[380+i*TRACE_BYTES:380+i*TRACE_BYTES+212]
                    self.assertEqual(decode(legacy, dict(start=[0,0,0],crop=[-999,999,-999,999]))[1]['reported_witnessed'], 181)
                    with self.assertRaises(ValueError):
                        audit(partial[:380+61*TRACE_BYTES], host_fixture=True)
                    with self.assertRaises(ValueError):
                        audit(partial, host_fixture=True)
                    with self.assertRaises(ValueError):
                        decode(partial[:-1], dict(start=[0,0,0], crop=[-1,1,-1,1]))
                    with self.assertRaisesRegex(ValueError, 'cached stock main RAM'):
                        audit(blob)
                    # Raw outputs, frame identities, render values and summary
                    # counters must all be audited, not just a sticky success bit.
                    changes = [(380+32+4*i, '<I', 0x7fc12345) for i in range(36)]
                    changes += [(380+TRACE_BYTES*i, '<I', 0) for i in range(181)]
                    changes += [(380+TRACE_BYTES*i+176, '<I', 0) for i in range(181)]
                    changes += [(4*i, '<I', 60) for i in (4, 5, 6, 7, 43, 44, 45)]
                    changes += [(380+180, '<f', 0), (380+8, '<I', 4)]
                    changes += [(380+TRACE_BYTES*i+212, '<f', value) for i in (0,42,43,180)
                                for value in (0, 5, float('nan'))]
                    changes += [(380+TRACE_BYTES*42+212, '<f', 4)] # actual PAL angle, false NTSC ratio
                    for offset, fmt, value in changes:
                        mutant = bytearray(blob)
                        struct.pack_into(fmt, mutant, offset, value)
                        with self.assertRaises(ValueError):
                            audit(mutant, host_fixture=True)
                    with self.assertRaises(ValueError):
                        audit(blob[:-1], host_fixture=True)

    def test_affine_conversion_validation_memory_and_pad_services(self):
        self.compile_run('tests/arena_live_math_check.c')

    def test_observer_per_frame_evidence_and_closed_failures(self):
        self.compile_run('tests/arena_live_observer_check.c', observer=True)

    def test_capture_contract_refuses_short_dump_before_launch(self):
        sys.path.insert(0, str(PORT))
        from verify_arena_live import capture_contract, EVIDENCE_BYTES, EXPECTED_CHECKS
        self.assertEqual(capture_contract('arena-live', None, None),
                         (['dc_arena_live_evidence', str(EVIDENCE_BYTES)], EXPECTED_CHECKS))
        self.assertEqual(EVIDENCE_BYTES, 39476)
        for dump in (['dc_arena_live_evidence','13312'], ['dc_arena_live_evidence','38752'],
                     ['dc_arena_live_render_metrics','80']):
            with self.assertRaises(ValueError):
                capture_contract('arena-live', dump, None)
        with self.assertRaises(ValueError):
            capture_contract('arena-live', None, 6982)
        self.assertEqual(capture_contract('arena-camera', ['x','80'], 42), (['x','80'],42))
        self.assertEqual(capture_contract('codegen', None, None), (None,17))

    def test_asset_ledger_bounds_counts_and_refusals(self):
        import json
        sys.path.insert(0, str(PORT))
        from make_arena_live_assets import header
        with tempfile.TemporaryDirectory(prefix='live-assets-') as tmp:
            root = Path(tmp)
            (root / 'cells').mkdir()
            report = dict(frames=16, length_m=6, crop=[-30, 30, -40, 40])
            (root / 'FLY.json').write_text(json.dumps(report))
            (root / 'package.dci').write_bytes(b'DCI3')
            cell = root / 'cells/-2_1.dci'
            cell.write_bytes(bytes(52))
            result = header(root)
            self.assertIn('{-2,1,1u,', result)
            self.assertEqual(result, header(root))
            for bad in ([], [1, 0, -1, 1], [0, 1, float('nan'), 1], [0, float('inf'), 0, 1]):
                report['crop'] = bad
                (root / 'FLY.json').write_text(json.dumps(report))
                with self.assertRaises(ValueError):
                    header(root)
            report['crop'] = [-30, 30, -40, 40]
            (root / 'FLY.json').write_text(json.dumps(report))
            for blob in (b'', bytes(51), bytes(53)):
                cell.write_bytes(blob)
                with self.assertRaises(ValueError):
                    header(root)
            cell.write_bytes(bytes(52))
            (root / 'package.dci').write_bytes(b'DCI4')
            with self.assertRaises(ValueError):
                header(root)


if __name__ == '__main__':
    unittest.main()
