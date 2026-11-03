"""Host contracts and original typed-IR replay, no target run (AI-assisted), ISC."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_arena_live_original_input import replay  # noqa: E402
sys.path.insert(0, str(PORT))
from dcsh_tool import read_module, function  # noqa: E402


def original_art_lookup(length, match=False):
    """Host replay of original loader IR, with fixture memory/string equality.

    Only the no-debug path is supported. A disk-loading call or a memory store
    is an error, not a fabricated answer. This does not execute SH-4 code.
    """
    false, true = 0x100004, 0x10000c
    doc = json.loads((PORT / 'build/game_loader.ir.json').read_text())
    functions = {f['name']: f for f in doc['functions']}
    # Directory -> string array / art array. These values are HOST fixtures.
    memory = {0x200004: 0x300000, 0x200008: 0x400000, 0x300000: length,
              0x30000c: 0x500000 if match else 0x500100, 0x40000c: 0x600004}
    symbols = {'*debug-segment*': false, 'art-group-load-check': 'art-group-load-check', 'string=': 'string='}
    calls = []

    def run(name, argv):
        f = functions[name]
        regs = {p['register']: a for p, a in zip(f['parameters'], argv)}
        pc = steps = 0
        while pc < len(f['instructions']):
            op = f['instructions'][pc]
            pc += 1
            steps += 1
            assert steps < 1000
            kind = op['kind']
            if kind == 'nop':
                continue
            args = [regs[r] for r in op['reads']]
            result = None
            if kind == 'return':
                return args[0]
            if kind == 'move':
                result = args[0]
            elif kind == 'constant':
                result = op['value']
            elif kind == 'symbol_value':
                result = symbols[op['symbol']]
            elif kind == 'symbol_pointer':
                result = {'#f': false, '#t': true}[op['symbol']]
            elif kind == 'load_memory':
                assert op['size'] == 4
                result = memory[args[0] + op['offset']]
            elif kind == 'add64':
                result = args[0] + args[1]
            elif kind == 'shl64':
                result = args[0] << op['shift']
            elif kind == 'goto':
                pc = op['target']
            elif kind == 'branch':
                condition = {'eq': args[0] == args[1], 'ne': args[0] != args[1], 'lt': args[0] < args[1]}
                if condition[op['condition']]:
                    pc = op['target']
            elif kind == 'call':
                calls.append(args[0])
                result = (true if args[1] == args[2] else false) if args[0] == 'string=' else run(args[0], args[1:])
            else:
                raise AssertionError(op)
            if result is not None:
                regs[op['writes'][0]] = result
        raise AssertionError('original loader did not return')

    result = run('(method load-to-heap-by-name load-dir-art-group)', [0x200000, 0x500000, false, 0x700000, 1])
    return result, calls


def original_debug_initializer(enabled, layout=0):
    """Interpret this original top-level IR's branches and stores on the host.

    malloc is a bounded allocation double. Discarded function addresses are
    zero, matching --no-debug-segment in the production module, not invented
    functions. This is an IR semantic check, not original SH-4 execution.
    """
    false, true = 0x100004, 0x10000c
    doc = json.loads((PORT / 'build/game_cam_debug_h.ir.json').read_text())
    code = next(f['instructions'] for f in doc['functions'] if f['top_level'])
    symbols = {'*debug-segment*': true if enabled else false, '*cam-layout*': layout, 'malloc': 123}
    pointers = {'#f': false, '#t': true, 'debug': 456}
    regs, allocations = {}, []
    pc = steps = 0
    while pc < len(code):
        op = code[pc]
        pc += 1
        steps += 1
        assert steps < 1000
        kind = op['kind']
        if kind in ('nop', 'return'):
            if kind == 'return':
                break
            continue
        args = [regs[r] for r in op['reads']]
        result = None
        if kind == 'symbol_value':
            result = symbols.get(op['symbol'], 0)
        elif kind == 'symbol_pointer':
            result = pointers[op['symbol']]
        elif kind == 'constant':
            result = op['value']
        elif kind == 'function_address':
            result = 0
        elif kind == 'move':
            result = args[0]
        elif kind == 'set_symbol':
            symbols[op['symbol']] = args[0]
        elif kind == 'branch':
            assert op['condition'] in ('eq', 'ne')
            if (args[0] == args[1]) == (op['condition'] == 'eq'):
                pc = op['target']
        elif kind == 'goto':
            pc = op['target']
        elif kind == 'call':
            assert args == [123, 456, 1600]
            result = 0x200000 + 1600 * len(allocations)
            allocations.append(result)
        else:
            raise AssertionError(op)
        if result is not None:
            regs[op['writes'][0]] = result
    return symbols, allocations


class ArenaJakTest(unittest.TestCase):
    def test_original_art_lookup_missing_and_resident_paths(self):
        self.assertEqual(original_art_lookup(0), (0x100004, ['art-group-load-check']))
        self.assertEqual(original_art_lookup(1), (0x100004, ['string=', 'art-group-load-check']))
        self.assertEqual(original_art_lookup(1, match=True), (0x600004, ['string=']))

    def test_native_exports_are_present_and_debug_functions_are_discarded(self):
        modules = PORT / 'build/game-dcsh'
        entries = read_module(modules / 'cam-debug-h.dcsh')
        self.assertEqual([(e.kind, e.name) for e in entries], [('function', 'dc-game-cam-debug-h')])
        for module, names in (
                ('logic-target', ('start', 'init-target')),
                ('game-info', ('(method get-or-create-continue! game-info)',)),
                ('loader', ('(method load-to-heap-by-name load-dir-art-group)', 'art-group-load-check'))):
            for name in names:
                self.assertGreater(len(function(read_module(modules / (module + '.dcsh')), name)), 0)

    def test_probe_modules_match_the_packed_game_dgo(self):
        required = {'cam-debug-h', 'logic-target', 'game-info', 'loader', 'target-util', 'level'}
        blob = (PORT / 'build/kernel-dgo/GAME.CGO').read_bytes()
        count, = struct.unpack_from('<I', blob)
        self.assertEqual(count, 326)
        offset, found = 64, set()
        for _ in range(count):
            size, = struct.unpack_from('<I', blob, offset)
            name = blob[offset+4:offset+64].split(b'\0', 1)[0].decode()
            offset += 64
            if name in required:
                self.assertEqual(blob[offset:offset+size], (PORT / 'build/game-dcsh' / (name + '.dcsh')).read_bytes())
                found.add(name)
            offset += (size + 15) & ~15
        self.assertEqual(offset, len(blob))
        self.assertEqual(found, required)

    def test_original_debug_data_and_digital_input_need_initializer(self):
        false = 0x100004
        skipped, empty = original_debug_initializer(False)
        loaded, tables = original_debug_initializer(True)
        self.assertEqual(skipped['*cam-layout*'], 0)
        self.assertEqual(empty, [])
        self.assertEqual(loaded['*cam-layout*'], false)
        self.assertEqual(len(set(tables)), 5)
        for color in ('redline', 'blueline', 'greenline', 'yellowline', 'timeplot'):
            self.assertIn(loaded['*' + color + '-table*'], tables)
            self.assertEqual(loaded['*' + color + '-index*'], 0)
        # Same digital samples, analog sticks centered: no response without init.
        for ratio in (1, 1.2, 4, 4.8):
            before = replay(skipped['*cam-layout*'], circle=True, l2=True, ratio=ratio)
            after = replay(loaded['*cam-layout*'], circle=True, l2=True, ratio=ratio)
            self.assertEqual(before, ((0, 0, 0), (0, 0, 0)))
            self.assertAlmostEqual(after[0][1], -65536 * .6 / 360 * ratio, delta=.0001)
            self.assertEqual(after[1], (0, 0, 0))
        # Original define-perm respects active layout state; never force it false.
        active, _ = original_debug_initializer(True, 0x123456)
        self.assertEqual(active['*cam-layout*'], 0x123456)

    def test_host_refusal_contracts(self):
        env = os.environ.copy()
        env['PATH'] += os.pathsep + 'C:/msys64/mingw64/bin' + os.pathsep + str(
            (PORT / '../../../../dc-sdk/sh-elf/bin').resolve())
        cc = os.environ.get('HOST_CC') or shutil.which('gcc', path=env['PATH'])
        self.assertTrue(cc)
        with tempfile.TemporaryDirectory(prefix='arena-jak-') as tmp:
            for fast in (False, True):
                out = Path(tmp) / ('jak.exe' if os.name == 'nt' else 'jak')
                low = ['-Wl,--image-base,0x10000000', '-Wl,--disable-dynamicbase'] if os.name == 'nt' else ['-no-pie']
                cmd = [cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-DDC_ARENA_JAK',
                       *(['-ffast-math'] if fast else []), *low, '-I', str(PORT),
                       *[str(PORT / f) for f in ('tests/arena_jak_check.c', 'arena_jak.c', 'arena_jak_debug.c',
                                                  'goal_runtime.c', 'tests/arena_jak_module_fixture.c')], '-o', str(out)]
                result = subprocess.run(cmd, env=env, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(out)], env=env, text=True, capture_output=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(('fast-math ' if fast else 'O2 ') + result.stdout.strip())

    def test_recorded_hardware_and_no_c_layout_fallback(self):
        env = os.environ.copy()
        env['PATH'] += os.pathsep + 'C:/msys64/mingw64/bin'
        cc = os.environ.get('HOST_CC') or shutil.which('gcc', path=env['PATH'])
        self.assertTrue(cc)
        with tempfile.TemporaryDirectory(prefix='jak-observer-') as tmp:
            out = Path(tmp) / ('check.exe' if os.name == 'nt' else 'check')
            low = ['-Wl,--image-base,0x10000000', '-Wl,--disable-dynamicbase'] if os.name == 'nt' else ['-no-pie']
            sources = ('tests/arena_jak_observer_check.c', 'arena_live_math.c', 'arena_live_input.c',
                       'goal_runtime.c', 'goal_module.c', 'goal_pad.c', 'instance_pass.c')
            result = subprocess.run([cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                                     '-DDC_ARENA_LIVE', '-DDC_ARENA_JAK', *low, '-I', str(PORT),
                                     *[str(PORT / f) for f in sources], '-lm', '-o', str(out)],
                                    env=env, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(out)], env=env, text=True, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
