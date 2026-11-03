"""Single-object SH-4 validation only, never link or launch (AI-assisted), ISC."""
from pathlib import Path
import os
import subprocess

PORT = Path(__file__).resolve().parents[1]
SDK = Path('C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk')
KOS = (PORT / '../../../kos-source').resolve()
OUT = PORT / 'build/arena-jak-validation'
OUT.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] += ';C:/msys64/mingw64/bin;' + str(SDK / 'sh-elf/bin')
flags = ['-O2', '-fno-builtin', '-m4-single', '-ml', '-mfsrra', '-mfsca',
         '-ffunction-sections', '-fdata-sections', '-matomic-model=soft-imask', '-ftls-model=local-exec',
         '-D__DREAMCAST__', '-D_arch_dreamcast', '-D_arch_sub_pristine',
         '-std=c99', '-Wall', '-Wextra', '-Werror', '-I.',
         '-I' + str(KOS / 'include'), '-I' + str(KOS / 'kernel/arch/dreamcast/include'),
         '-I' + str(KOS / 'addons/include'), '-I' + str(SDK / 'kos-ports/include')]
mode = ['-DDC_ARENA_COEXIST', '-DDC_ARENA_FLY', '-DDC_ARENA_LIVE', '-DDC_ARENA_JAK']
sources = {'arena_jak': 'arena_jak.c', 'arena_jak_debug': 'arena_jak_debug.c',
           'arena_jak_dgo': 'arena_jak_dgo.c', 'arena_jak_live': 'arena_live.c',
           'arena_jak_input': 'arena_live_input.c', 'arena_jak_kernel_cases': 'kernel_boot_cases.c'}
for obj, source in sources.items():
    extra = ['-DDC_KERNEL_DGO'] if source == 'kernel_boot_cases.c' else []
    subprocess.run([str(SDK / 'sh-elf/bin/sh-elf-gcc.exe'), *flags, *mode, *extra,
                    '-c', source, '-o', str(OUT / (obj + '.o'))], cwd=PORT, env=env, check=True)
    print(source + ': compiled')
