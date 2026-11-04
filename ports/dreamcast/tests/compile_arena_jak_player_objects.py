"""Compile individual SH-4 objects, never link/launch (AI-assisted), ISC."""
from pathlib import Path
import os
import subprocess

PORT = Path(__file__).resolve().parents[1]
SDK = Path('C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk')
KOS = (PORT/'../../../kos-source').resolve()
OUT = PORT/'build/arena-jak-player-validation'
OUT.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] += ';C:/msys64/mingw64/bin;' + str(SDK/'sh-elf/bin')
flags = ['-O2', '-fno-builtin', '-m4-single', '-ml', '-mfsrra', '-mfsca',
         '-ffunction-sections', '-fdata-sections', '-matomic-model=soft-imask', '-ftls-model=local-exec',
         '-D__DREAMCAST__', '-D_arch_dreamcast', '-D_arch_sub_pristine',
         '-std=c99', '-Wall', '-Wextra', '-Werror', '-I.',
         '-I'+str(KOS/'include'), '-I'+str(KOS/'kernel/arch/dreamcast/include'),
         '-I'+str(KOS/'addons/include'), '-I'+str(SDK/'kos-ports/include')]
mode = ['-DDC_ARENA_COEXIST', '-DDC_ARENA_FLY', '-DDC_ARENA_LIVE', '-DDC_ARENA_JAK', '-DDC_ARENA_JAK_PLAYER']
sources = ['arena_jak_main.c', 'arena_coexist.c', 'kernel_boot_cases.c', 'goal_pad.c', 'arena_live.c',
           'arena_live_render.c', 'arena_jak.c', 'arena_jak_art.c', 'goal_art.c', 'arena_jak_exports.c',
           'goal_module.c', 'arena_jak_player.c']
for source in sources:
    extra = []
    if source == 'arena_jak_main.c':
        extra = ['-DDC_KERNEL_BOOT']
    if source == 'kernel_boot_cases.c':
        extra = ['-DDC_KERNEL_DGO']
    if source == 'arena_live_render.c':
        extra = ['-DDC_ARENA_SLAB_REDIRECT', '-include', 'arena_slab.h']
    subprocess.run([str(SDK/'sh-elf/bin/sh-elf-gcc.exe'), *flags, *mode, *extra,
                    '-c', source, '-o', str(OUT/(Path(source).stem+'.o'))], cwd=PORT, env=env, check=True)
    print(source+': SH-4 object compiled')
