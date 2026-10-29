"""Build and verify the native boot of the original jak1 kernel (AI-assisted).

Compiles the four unchanged original kernel files as whole modules, each with its
original top-level startup as an initializer, under the explicit Dreamcast
target contexts (scalar memory bounds, no desktop profiler, original console
memory sizes). gkernel's eight x86/ARM64 assembly primitives bind to their
native counterparts. Links jak-dc-kernel-boot.elf (modules compiled in) and
jak-dc-kernel-dgo.elf (the same modules loaded from a KERNEL.CGO DGO on the romdisk)
and, with --verify, runs both.
"""
import argparse
from pathlib import Path
import subprocess
import json
import sys
from pack_native_dgo import write_dgo

BINDINGS = {
    'return-from-thread': 'dc-c-return-from-thread',
    'return-from-thread-dead': 'dc-c-return-from-thread-dead',
    'reset-and-call': 'dc-c-reset-and-call',
    '(method thread-suspend cpu-thread)': 'dc-c-thread-suspend',
    '(method thread-resume cpu-thread)': 'dc-c-thread-resume',
    '(method new catch-frame)': 'dc-c-catch-frame-new',
    'throw-dispatch': 'throw-dispatch',
    'set-to-run-bootstrap': 'set-to-run-bootstrap',
}
EXPECTED_CHECKS = 90  # verified in Flycast; never weakened afterwards
EXPECTED_DGO_CHECKS = 532  # KERNEL.CGO + 326 GAME.CGO files + 141 vf cases, verified in Flycast
GAME_PREFIX = 326  # all of GAME.CGO


def main():
    port = Path(__file__).resolve().parent
    work = port.parents[2]
    kernel = port.parents[1] / 'goal_src/jak1/kernel'
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', type=Path, default=work / 'og-host/bin/goalc-dreamcast-ir.exe')
    parser.add_argument('--msys', type=Path, default=Path('C:/msys64'))
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--tests', type=int, default=EXPECTED_CHECKS)
    parser.add_argument('--flycast', type=Path, default=work / 'flycast/flycast.exe')
    parser.add_argument('--nm', type=Path, default=work / 'dc-sdk/sh-elf/bin/sh-elf-nm.exe')
    parser.add_argument('--output', type=Path, default=work / 'flycast-kernel-boot')
    parser.add_argument('--dgo-tests', type=int,
                        help=f'default {EXPECTED_DGO_CHECKS}, 2 more with the animation case (anim_case_data.py)')
    parser.add_argument('--dgo-output', type=Path, default=work / 'flycast-kernel-dgo')
    args = parser.parse_args()
    compiler = [sys.executable, str(port / 'compile_goal_subset.py'), '--frontend', str(args.frontend),
                '--runtime', '--msys', str(args.msys), '--all-functions']
    def preloads(*sources):
        return [arg for source in sources for arg in ('--preload', str(source))]
    contexts = (kernel / 'gcommon.gc', port / 'scalar_validation_context.gc', port / 'dreamcast_profiler_context.gc',
                port / 'dreamcast_memory_context.gc')
    target = preloads(*contexts, kernel / 'gkernel-h.gc', kernel / 'gstring-h.gc',
                      port / 'dreamcast_thread_context.gc')
    # The original KERNEL.CGO load order.
    modules = [
        ('gcommon', []),
        ('gstring-h', preloads(kernel / 'gcommon.gc')),
        ('gkernel-h', preloads(*contexts, kernel / 'gstring-h.gc')),
        ('gkernel', target + [arg for original, native in BINDINGS.items()
                               for arg in ('--native-binding', f'{original}={native}')]),
        ('pskernel', target),  # game.gp: gcommon + gkernel-h (which redefines break)
        ('gstring', preloads(kernel / 'gcommon.gc', kernel / 'gstring-h.gc')),
        ('dgo-h', preloads(kernel / 'gcommon.gc')),
        ('gstate', target + preloads(kernel / 'gkernel.gc')),
    ]
    dcsh = port / 'build/kernel-dcsh'
    for name, extra in modules:
        stem = name.replace('-', '_')
        subprocess.run(compiler + extra + ['--source', str(kernel / f'{name}.gc'),
                                           '--initializer', f'dc-boot-{name}',
                                           '--header', str(port / f'build/boot_{stem}.h'),
                                           '--native-module', str(dcsh / f'{name}.dcsh')], check=True)
    # The whole-file gcommon keeps its desktop END_OF_MEMORY; replace valid? with
    # the explicitly scalar-configured original (as the runtime suite does).
    subprocess.run([sys.executable, str(port / 'compile_goal_subset.py'), '--frontend', str(args.frontend),
                    '--runtime', '--msys', str(args.msys),
                    '--preload', str(kernel / 'gcommon.gc'), '--preload', str(port / 'scalar_validation_context.gc'),
                    '--source', str(kernel / 'gcommon.gc'), '--select-defuns', '--function', 'valid?',
                    '--header', str(port / 'build/boot_valid.h'),
                    '--native-module', str(dcsh / 'dc-scalar-valid.dcsh')], check=True)
    # KERNEL.CGO for the DGO boot: the original order, with the Dreamcast valid?
    # override as an extra object straight after gcommon.
    order = ['gcommon', 'dc-scalar-valid'] + [name for name, _ in modules[1:]]
    write_dgo(port / 'build/kernel-dgo/KERNEL.CGO', 'KERNEL.CGO', [(n, dcsh / f'{n}.dcsh') for n in order])
    # The PC port's mips2c functions, built natively for SH-4 into the DGO ELF.
    subprocess.run([sys.executable, str(port / 'build_mips2c.py')], check=True, stdout=subprocess.DEVNULL)
    if args.dgo_tests is None:
        args.dgo_tests = EXPECTED_DGO_CHECKS + (2 if (port / 'build/mips2c/anim_case.h').is_file() else 0)
    # The compiled prefix of the original GAME.CGO.
    subprocess.run([sys.executable, str(port / 'build_game_cgo.py'), '--frontend', str(args.frontend),
                    '--msys', str(args.msys), '--count', str(GAME_PREFIX)], check=True)
    compiled = json.loads((port / 'build/game-cgo-report.json').read_text())['compiled']
    if len(compiled) != GAME_PREFIX: sys.exit(f'GAME.CGO prefix compiled only {compiled}')
    # The romdisk carries GAME.CGO as zstd with a 128 KiB window (goal_dgo_zst.c);
    # the boot streams it, so the resident copy is about 1/8 the size.
    romdisk = port / 'build/kernel-dgo-rd'
    romdisk.mkdir(exist_ok=True)
    (romdisk / 'GAME.CGO.gz').unlink(missing_ok=True)
    (romdisk / 'KERNEL.CGO').write_bytes((port / 'build/kernel-dgo/KERNEL.CGO').read_bytes())
    subprocess.run([str(args.msys / 'usr/bin/zstd.exe'), '-19', '--zstd=wlog=17', '-q', '-f',
                    str(port / 'build/kernel-dgo/GAME.CGO'), '-o', str(romdisk / 'GAME.CGO.zst')], check=True)
    # VU0 vf lowering fixture, compiled after the kernel and GAME.CGO through
    # vector-h, and its independently computed expected lanes.
    engine = port.parents[1] / 'goal_src/jak1/engine'
    fixture_preloads = [*contexts, kernel / 'gstring-h.gc', kernel / 'gkernel-h.gc', port / 'dreamcast_thread_context.gc',
                        *(kernel / f'{n}.gc' for n in ('gkernel', 'pskernel', 'gstring', 'dgo-h', 'gstate')),
                        engine / 'util/types-h.gc', engine / 'ps2/vu1-macros.gc', engine / 'math/math.gc',
                        engine / 'math/vector-h.gc']
    subprocess.run(compiler + preloads(*fixture_preloads) + ['--source', str(port / 'vf_fixture.gc'),
                                                            '--header', str(port / 'build/vf_fixture.h')], check=True)
    subprocess.run([sys.executable, str(port / 'gen_vf_cases.py')], check=True)
    elves = (('jak-dc-kernel-boot.elf', args.tests, args.output),
             ('jak-dc-kernel-dgo.elf', args.dgo_tests, args.dgo_output))
    for elf, _, _ in elves:
        subprocess.run([sys.executable, str(port / 'build_dreamcast.py'), '--msys', str(args.msys),
                        '--target', elf], check=True)
    if args.verify:
        for elf, tests, output in elves:
            if tests <= 0: parser.error('pass the verified --tests/--dgo-tests counts')
            subprocess.run([sys.executable, str(port / 'verify_backend.py'), str(args.flycast),
                            str(port / elf), '--nm', str(args.nm), '--output', str(output),
                            '--tests', str(tests), '--suite', 'runtime-goal', '--timeout', '300'], check=True)

if __name__ == '__main__':
    main()
