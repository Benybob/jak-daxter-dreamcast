"""Compile single objects only, never link/run the target (AI-assisted), ISC."""
from pathlib import Path
import os
import subprocess

PORT = Path(__file__).resolve().parents[1]
SDK = Path('C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk')
KOS = (PORT / '../../../kos-source').resolve()
OUT = PORT / 'build/arena-live-validation'
OUT.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] += ';C:/msys64/mingw64/bin;' + str(SDK / 'sh-elf/bin')
flags = ['-O2', '-fno-builtin', '-m4-single', '-ml', '-mfsrra', '-mfsca',
         '-ffunction-sections', '-fdata-sections', '-matomic-model=soft-imask',
         '-ftls-model=local-exec', '-D__DREAMCAST__', '-D_arch_dreamcast',
         '-D_arch_sub_pristine', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I.',
         '-I' + str(KOS / 'include'), '-I' + str(KOS / 'kernel/arch/dreamcast/include'),
         '-I' + str(KOS / 'addons/include'), '-I' + str(SDK / 'kos-ports/include')]
mode = ['-DDC_ARENA_COEXIST', '-DDC_ARENA_FLY', '-DDC_ARENA_LIVE']
for source in ('arena_live.c', 'arena_live_math.c', 'arena_live_input.c', 'arena_live_assets.c', 'arena_live_render.c',
               'goal_bridge.c', 'goal_pad.c', 'arena_coexist.c', 'kernel_boot_cases.c'):
    extra = ['-DDC_KERNEL_DGO'] if source == 'kernel_boot_cases.c' else []
    if source in ('arena_live_render.c', 'arena_live_assets.c'):
        extra += ['-DDC_ARENA_SLAB_REDIRECT', '-include', 'arena_slab.h']
    subprocess.run([str(SDK / 'sh-elf/bin/sh-elf-gcc.exe'), *flags, *mode, *extra,
                    '-c', source, '-o', str(OUT / (Path(source).stem + '.o'))],
                   cwd=PORT, env=env, check=True)
    print(source + ': compiled')
