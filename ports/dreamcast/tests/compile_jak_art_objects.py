"""Single SH-4 objects and existing-mode isolation only (AI-assisted), ISC."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

PORT = Path(__file__).resolve().parents[1]
SDK = Path('C:/Users/sullins server/Documents/Codex/2026-10-20/continue-our-jak-and-daxter-dreamcast/work/dc-sdk')
KOS = (PORT / '../../../kos-source').resolve()
OUT = PORT / 'build/jak-art-validation'
OUT.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] += ';C:/msys64/mingw64/bin;' + str(SDK / 'sh-elf/bin')
cc = str(SDK / 'sh-elf/bin/sh-elf-gcc.exe')
includes = ['-I.', '-I' + str(KOS / 'include'), '-I' + str(KOS / 'kernel/arch/dreamcast/include'),
            '-I' + str(KOS / 'addons/include'), '-I' + str(SDK / 'kos-ports/include')]
arch = ['-D__DREAMCAST__', '-D_arch_dreamcast', '-D_arch_sub_pristine']
flags = ['-O2', '-fno-builtin', '-m4-single', '-ml', '-mfsrra', '-mfsca',
         '-ffunction-sections', '-fdata-sections', '-matomic-model=soft-imask', '-ftls-model=local-exec',
         '-std=c99', '-Wall', '-Wextra', '-Werror', *arch, *includes]
mode = ['-DDC_ARENA_COEXIST', '-DDC_ARENA_FLY', '-DDC_ARENA_LIVE', '-DDC_ARENA_JAK']
for source, obj, defines in [('goal_art.c', 'goal_art', []), ('arena_jak_art.c', 'arena_jak_art',
                             [*mode, '-DDC_ARENA_JAK_ART']),
                            ('arena_jak.c', 'arena_jak_art_probe', [*mode, '-DDC_ARENA_JAK_ART'])]:
    subprocess.run([cc, *flags, *defines, '-c', source, '-o', str(OUT / (obj + '.o'))],
                   cwd=PORT, env=env, check=True)
    print(source + ': SH-4 object compiled')
# The only changed existing translation unit is arena_jak.c. Compare its old
# Jak mode too, not just the pre-Jak camera/live variants.
old = subprocess.check_output(['git', 'show', 'HEAD:ports/dreamcast/arena_jak.c'], cwd=PORT, text=True)
configs = [[], ['-DDC_ARENA_COEXIST'], ['-DDC_ARENA_COEXIST', '-DDC_ARENA_CAMERA'],
           ['-DDC_ARENA_COEXIST', '-DDC_ARENA_FLY'],
           ['-DDC_ARENA_COEXIST', '-DDC_ARENA_FLY', '-DDC_ARENA_GAME'], mode[:-1], mode]
for defines in configs:
    outputs = [subprocess.run([cc, '-E', '-P', '-x', 'c', '-', *arch, *includes, *defines],
                              input=s, cwd=PORT, env=env, check=True, capture_output=True, text=True).stdout
               for s in (old, (PORT / 'arena_jak.c').read_text())]
    assert outputs[0] == outputs[1], defines
old_make = subprocess.check_output(['git', 'show', 'HEAD:ports/dreamcast/Makefile'], cwd=PORT, text=True)
without_jak = lambda text: '\n'.join(line for line in text.splitlines() if not line.startswith('jak-dc-arena-jak.elf:'))
assert without_jak((PORT / 'Makefile').read_text()).startswith(without_jak(old_make))
report = dict(ai_assisted=True, existing_modes_identical=len(configs), makefile_prefix_except_jak_dependencies_unchanged=True,
              elf_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in PORT.glob('*.elf') if 'sound' not in p.name})
before = OUT / 'before.json'
if before.exists():
    for name, digest in json.loads(before.read_text()).items():
        if name.endswith('.elf'):
            assert report['elf_sha256'][name] == digest, name
(OUT / 'isolation.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'{len(configs)} existing modes identical; non-Jak Makefile prefix and existing ELF hashes unchanged')
