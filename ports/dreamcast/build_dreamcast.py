"""Build this port using the workspace-local Windows SDK (AI-assisted)."""
import argparse
import ctypes
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workspace',type=Path,default=Path(__file__).resolve().parents[3])
parser.add_argument('--msys',type=Path,default=Path('C:/msys64'))
parser.add_argument('--target',default='all')
args=parser.parse_args()
port=Path(__file__).resolve().parent
work=args.workspace.resolve()
def short_posix(path):
    value=ctypes.create_unicode_buffer(4096)
    if not ctypes.windll.kernel32.GetShortPathNameW(str(path),value,len(value)):
        raise OSError('GetShortPathName failed')
    text=value.value.replace('\\','/')
    text='/'+text[0].lower()+text[2:]
    if ' ' in text: raise ValueError('KOS make paths must not contain spaces')
    return text
if not args.target.replace('-','').replace('.','').isalnum():
    parser.error('invalid make target')
script=f'''#!/usr/bin/bash
set -eo pipefail
source {short_posix(work)}/kos-source/environ.sh
cd {short_posix(port)}
make PYTHON=python fixture
make -j4 CFLAGS='-std=c99 -Wall -Wextra -Werror' {args.target}
'''
script_path=work/'build-port-current.sh'
script_path.write_text(script,newline='\n')
raise SystemExit(subprocess.call([str(args.msys/'usr/bin/bash.exe'),'--noprofile','--norc',str(script_path)]))
