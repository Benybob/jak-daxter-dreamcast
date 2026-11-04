"""Read-only old-mode SH-4 preprocessing/recipe audit (AI-assisted), ISC.

No links, emulator or Git mutations. Includes the existing live mode.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess

PORT = Path(__file__).resolve().parents[1]
SDK = Path('C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk')
KOS = (PORT / '../../../kos-source').resolve()
env = os.environ.copy()
env['PATH'] += ';C:/msys64/mingw64/bin;' + str(SDK / 'sh-elf/bin')
flags = ['-E', '-P', '-x', 'c', '-', '-I.', '-D__DREAMCAST__', '-D_arch_dreamcast',
         '-D_arch_sub_pristine', '-I' + str(KOS / 'include'),
         '-I' + str(KOS / 'kernel/arch/dreamcast/include'), '-I' + str(KOS / 'addons/include'),
         '-I' + str(SDK / 'kos-ports/include')]
configs = {'kernel': [], 'arena': ['DC_ARENA_COEXIST'],
           'camera': ['DC_ARENA_COEXIST', 'DC_ARENA_CAMERA'],
           'fly': ['DC_ARENA_COEXIST', 'DC_ARENA_FLY'],
           'game': ['DC_ARENA_COEXIST', 'DC_ARENA_FLY', 'DC_ARENA_GAME'],
           'live': ['DC_ARENA_COEXIST', 'DC_ARENA_FLY', 'DC_ARENA_LIVE'],
           'old-jak': ['DC_ARENA_COEXIST', 'DC_ARENA_FLY', 'DC_ARENA_LIVE', 'DC_ARENA_JAK']}
report = {'ai_assisted': True, 'preprocessed': {}, 'existing_elf_sha256': {}}
for file in ('arena_live.c', 'arena_live_input.c', 'kernel_boot_cases.c', 'arena_coexist.c',
             'goal_pad.c', 'arena_live_render.c', 'arena_jak.c'):
    original = subprocess.check_output(['git', 'show', 'HEAD:ports/dreamcast/' + file], cwd=PORT, text=True)
    for mode, defines in configs.items():
        cmd = [str(SDK / 'sh-elf/bin/sh-elf-gcc.exe'), *flags, *['-D' + d for d in defines],
               *(['-DDC_KERNEL_DGO'] if file == 'kernel_boot_cases.c' else [])]
        outputs = [subprocess.run(cmd, cwd=PORT, input=source, text=True, capture_output=True,
                                  env=env, check=True).stdout for source in (original, (PORT / file).read_text())]
        if outputs[0] != outputs[1]:
            raise RuntimeError(f'{file} {mode}: existing-mode preprocessing changed')
        report['preprocessed'][file + ':' + mode] = hashlib.sha256(outputs[0].encode()).hexdigest()
        print(file, mode, 'identical')
# Only Jak's target dependency line is replaced. Every other existing rule,
# recipe and variable remains byte-for-byte, including the old art probe.
old_make = subprocess.check_output(['git', 'show', 'HEAD:ports/dreamcast/Makefile'], cwd=PORT, text=True)
def without_jak(text):
    return '\n'.join(line for line in text.splitlines() if not line.startswith('jak-dc-arena-jak.elf:'))
if not without_jak((PORT / 'Makefile').read_text()).startswith(without_jak(old_make)):
    raise RuntimeError('existing Makefile content changed')
report['makefile_prefix_except_jak_dependencies_unchanged'] = True
for elf in sorted(PORT.glob('*.elf')):
    report['existing_elf_sha256'][elf.name] = hashlib.sha256(elf.read_bytes()).hexdigest()
out = PORT / 'build/arena-jak-validation/isolation.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=2) + '\n')
print('49 old-mode preprocessing comparisons; existing recipes except Jak dependencies identical; ELF hashes recorded')
