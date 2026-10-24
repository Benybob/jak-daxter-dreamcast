"""Install pinned upstream SH4ZAM into the local KOS ports SDK (AI-assisted).

Keeps the complete upstream library outside the project, with its MIT license.
Standard KOS users may instead install the official kos-ports/sh4zam port.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

REVISION='11aad5f9c7da0d8d5038633d418477d682d915f5'
UPSTREAM='https://github.com/gyrovorbis/sh4zam.git'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--workspace',type=Path,default=Path(__file__).resolve().parents[3])
p.add_argument('--msys',type=Path,default=Path('C:/msys64'))
a=p.parse_args();work=a.workspace.resolve();source=work/'sh4zam';out=source/'build-dc'
prefix=work/'dc-sdk/kos-ports'
if not source.exists():
 subprocess.run(['git','clone','--no-checkout',UPSTREAM,str(source)],check=True)
 subprocess.run(['git','-C',str(source),'checkout','--detach',REVISION],check=True)
def git(*args):return subprocess.check_output(['git','-C',str(source),*args],text=True).strip()
if git('rev-parse','HEAD')!=REVISION or git('status','--porcelain','--untracked-files=no'):
 p.error('SH4ZAM checkout differs from the pinned unmodified source; refusing to reset it')
marker=prefix/'share/sh4zam-jak-port.json'
if (prefix/'include/sh4zam').exists() and not marker.exists():
 p.error('Existing SH4ZAM installation has no project provenance; use it through standard KOS or a separate SDK')
def pos(path):
 if os.name!='nt':return str(path.resolve())
 buf=ctypes.create_unicode_buffer(4096)
 if not ctypes.windll.kernel32.GetShortPathNameW(str(path.resolve()),buf,len(buf)):
  raise OSError('GetShortPathName failed')
 text=buf.value.replace('\\','/');return '/'+text[0].lower()+text[2:]
out.mkdir(exist_ok=True);lines=['set -eo pipefail','source '+shlex.quote(pos(work/'kos-source')+'/environ.sh')];objects=[]
files=['source/shz_matrix.c','source/shz_quat.c','source/shz_version.c','source/shz_xmtrx.c',
 'source/sh4/shz_complex_sh4.c','source/sw/shz_complex_sw.c','source/sw/shz_xmtrx_sw.c',
 'source/sh4/shz_xmtrx_sh4.s','source/sh4/shz_mem_sh4.s']
for name in files:
 obj=pos(out)+'/'+Path(name).stem+'.o';objects.append(obj)
 lang='assembler-with-cpp' if name.endswith('.s') else 'c'
 lines.append('$KOS_CC $KOS_CFLAGS -x '+lang+' -std=gnu17 -I'+shlex.quote(pos(source)+'/include')+
              ' -c '+shlex.quote(pos(source/name))+' -o '+shlex.quote(obj))
lines.append('$KOS_AR rcs '+shlex.quote(pos(out)+'/libsh4zam.a')+' '+' '.join(map(shlex.quote,objects)))
script=out/'build-jak-port.sh';script.write_text('\n'.join(lines)+'\n',newline='\n')
bash=str(a.msys.resolve()/'usr/bin/bash.exe') if os.name=='nt' else 'bash'
subprocess.run([bash,'--noprofile','--norc',str(script)],check=True)
(prefix/'lib').mkdir(parents=True,exist_ok=True)
shutil.copy2(out/'libsh4zam.a',prefix/'lib/libsh4zam.a')
shutil.copytree(source/'include/sh4zam',prefix/'include/sh4zam',dirs_exist_ok=True)
license_dir=prefix/'share/licenses/sh4zam';license_dir.mkdir(parents=True,exist_ok=True)
shutil.copy2(source/'LICENSE',license_dir/'LICENSE')
marker.parent.mkdir(parents=True,exist_ok=True)
marker.write_text(json.dumps({'ai_assisted':True,'upstream':UPSTREAM,'revision':REVISION,
 'library_sha256':hashlib.sha256((prefix/'lib/libsh4zam.a').read_bytes()).hexdigest(),
 'license':'MIT','sources_unmodified':True},indent=2)+'\n')
print('Installed unmodified SH4ZAM '+REVISION+' into local KOS ports SDK')
