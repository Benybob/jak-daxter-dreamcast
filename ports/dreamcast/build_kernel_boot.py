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
EXPECTED_CHECKS = 87  # verified in Flycast; never weakened afterwards
EXPECTED_DGO_CHECKS = 90  # verified KERNEL.CGO boot in Flycast


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
    parser.add_argument('--dgo-tests', type=int, default=EXPECTED_DGO_CHECKS)
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
