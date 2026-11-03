"""Read-only Git/preprocessor audit of existing modes (AI-assisted), ISC.

Compares the four guarded TUs to HEAD with the actual SH-4 preprocessor and KOS
headers, including diagnostic __LINE__ constants. Does not build/link an ELF.
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
configs = {'kernel': [], 'arena': ['-DDC_ARENA_COEXIST'],
           'camera': ['-DDC_ARENA_COEXIST', '-DDC_ARENA_CAMERA'],
           'fly': ['-DDC_ARENA_COEXIST', '-DDC_ARENA_FLY'],
           'game': ['-DDC_ARENA_COEXIST', '-DDC_ARENA_FLY', '-DDC_ARENA_GAME']}
report = {'ai_assisted': True, 'preprocessed': {}, 'existing_elf_sha256': {}}
for file in ('goal_bridge.c', 'goal_pad.c', 'arena_coexist.c', 'kernel_boot_cases.c'):
    original = subprocess.check_output(['git', 'show', 'HEAD:ports/dreamcast/' + file], cwd=PORT, text=True)
    for mode, defines in configs.items():
        cmd = [str(SDK / 'sh-elf/bin/sh-elf-gcc.exe'), *flags, *defines,
               *(['-DDC_KERNEL_DGO'] if file == 'kernel_boot_cases.c' else [])]
        outputs = [subprocess.run(cmd, cwd=PORT, input=source, text=True, capture_output=True,
                                  env=env, check=True).stdout for source in (original, (PORT / file).read_text())]
        if outputs[0] != outputs[1]:
            raise RuntimeError(f'{file} {mode}: existing-mode preprocessing changed')
        report['preprocessed'][file + ':' + mode] = hashlib.sha256(outputs[0].encode()).hexdigest()
        print(file, mode, 'identical')
for elf in sorted(PORT.glob('*.elf')):
    report['existing_elf_sha256'][elf.name] = hashlib.sha256(elf.read_bytes()).hexdigest()
out = PORT / 'build/arena-live-validation/isolation.json'
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=2) + '\n')
print('20 old-mode preprocess comparisons passed; existing ELF hashes recorded')
