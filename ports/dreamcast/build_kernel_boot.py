"""Build and verify the native boot of the original jak1 kernel (AI-assisted).

Compiles the four unchanged original kernel files as whole modules, each with its
original top-level startup as an initializer, under the explicit Dreamcast
target contexts (scalar memory bounds, no desktop profiler, original console
memory sizes). gkernel's eight x86/ARM64 assembly primitives bind to their
native counterparts. Then links jak-dc-kernel-boot.elf and, with --verify, runs it.
"""
import argparse
from pathlib import Path
import subprocess
import sys

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
EXPECTED_CHECKS = 50  # verified in Flycast; never weakened afterwards


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
    args = parser.parse_args()
    compiler = [sys.executable, str(port / 'compile_goal_subset.py'), '--frontend', str(args.frontend),
                '--runtime', '--msys', str(args.msys), '--all-functions']
    def preloads(*sources):
        return [arg for source in sources for arg in ('--preload', str(source))]
    contexts = (kernel / 'gcommon.gc', port / 'scalar_validation_context.gc', port / 'dreamcast_profiler_context.gc',
                port / 'dreamcast_memory_context.gc')
    target = preloads(*contexts, kernel / 'gkernel-h.gc', kernel / 'gstring-h.gc')
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
    for name, extra in modules:
        stem = name.replace('-', '_')
        subprocess.run(compiler + extra + ['--source', str(kernel / f'{name}.gc'),
                                           '--initializer', f'dc-boot-{name}',
                                           '--header', str(port / f'build/boot_{stem}.h')], check=True)
    # The whole-file gcommon keeps its desktop END_OF_MEMORY; replace valid? with
    # the explicitly scalar-configured original (as the runtime suite does).
    subprocess.run([sys.executable, str(port / 'compile_goal_subset.py'), '--frontend', str(args.frontend),
                    '--runtime', '--msys', str(args.msys),
                    '--preload', str(kernel / 'gcommon.gc'), '--preload', str(port / 'scalar_validation_context.gc'),
                    '--source', str(kernel / 'gcommon.gc'), '--select-defuns', '--function', 'valid?',
                    '--header', str(port / 'build/boot_valid.h')], check=True)
    subprocess.run([sys.executable, str(port / 'build_dreamcast.py'), '--msys', str(args.msys),
                    '--target', 'jak-dc-kernel-boot.elf'], check=True)
    if args.verify:
        if args.tests <= 0: parser.error('pass the verified --tests count')
        subprocess.run([sys.executable, str(port / 'verify_backend.py'), str(args.flycast),
                        str(port / 'jak-dc-kernel-boot.elf'), '--nm', str(args.nm), '--output', str(args.output),
                        '--tests', str(args.tests), '--suite', 'runtime-goal', '--timeout', '300'], check=True)


if __name__ == '__main__':
    main()
