"""Build the original typed GOAL frontend with MinGW64 (AI-assisted).

Requires MSYS2 MinGW64 GCC, CMake, Ninja. Downloads portable NASM with a pinned
checksum; installs nothing globally. Short paths avoid Windows archive limits.
"""
import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

NASM_URL='https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip'
NASM_SHA='3ee4782247bcb874378d02f7eab4e294a84d3d15f3f6ee2de2f47a46aa7226e6'

def short_path(path):
    buffer=ctypes.create_unicode_buffer(4096)
    if not ctypes.windll.kernel32.GetShortPathNameW(str(path),buffer,len(buffer)):
        raise OSError('Windows short path unavailable: '+str(path))
    if ' ' in buffer.value: raise ValueError('Space-free Windows short paths are required')
    return Path(buffer.value)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace',type=Path,default=Path(__file__).resolve().parents[3])
    parser.add_argument('--msys',type=Path,default=Path('C:/msys64'))
    parser.add_argument('--jobs',type=int,default=6)
    parser.add_argument('--configure-only',action='store_true')
    args=parser.parse_args()
    if not 1<=args.jobs<=32: parser.error('jobs must be in 1..32')
    work=args.workspace.resolve(); work.mkdir(parents=True,exist_ok=True)
    port=Path(__file__).resolve().parent
    short=short_path(work)
    repository=port.parents[1]
    source=(short/repository.relative_to(work) if repository.is_relative_to(work)
            else short_path(repository))
    # Preserve the configured junction spelling so CMake does not unnecessarily
    # invalidate an existing toolchain cache (C:/msys64 may point to another disk).
    host=args.msys.absolute()/'mingw64/bin'
    for name in ('gcc.exe','g++.exe','cmake.exe','ninja.exe'):
        if not (host/name).is_file(): parser.error('Required MinGW64 tool missing: '+name)
    archive=work/'nasm.zip'
    if not archive.exists():
        temporary=archive.with_suffix('.zip.partial')
        with urllib.request.urlopen(NASM_URL) as response,temporary.open('wb') as stream:
            shutil.copyfileobj(response,stream)
        temporary.replace(archive)
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream,'sha256').hexdigest()!=NASM_SHA:
            raise ValueError('NASM download checksum mismatch')
    nasm=work/'nasm/nasm-2.16.03/nasm.exe'
    if not nasm.is_file():
        with zipfile.ZipFile(archive) as z: z.extractall(nasm.parent.parent)
    env=os.environ.copy()
    env['PATH']=str(host)+os.pathsep+str(args.msys.absolute()/'usr/bin')+os.pathsep+env['PATH']
    env['ASM_NASM']=str(nasm)
    build=short/'og-host'
    command=[str(host/'cmake.exe'),'-S',str(source),'-B',str(build),'-G','Ninja',
             '-DCMAKE_C_COMPILER='+str(host/'gcc.exe'),'-DCMAKE_CXX_COMPILER='+str(host/'g++.exe'),
             '-DCMAKE_MAKE_PROGRAM='+str(host/'ninja.exe'),'-DCMAKE_BUILD_TYPE=Release',
             '-DSTATICALLY_LINK=ON','-DBUILD_TESTING=OFF','-DBUILD_TESTS=OFF','-DBUILD_TOOLS=OFF',
             '-DUSE_SANITIZERS=OFF','-DCMAKE_POLICY_VERSION_MINIMUM=3.14',
             '-DBUILD_DREAMCAST_HOST_TOOLS=ON','-DCMAKE_EXE_LINKER_FLAGS=-Wl,--stack,16777216',
             '-DCMAKE_ASM_NASM_COMPILER='+str(short/'nasm/nasm-2.16.03/nasm.exe')]
    # MinGW's optional JACK headers do not imply a POSIX dlopen implementation.
    command+=['-DUSE_'+name+'=OFF' for name in ('JACK','PULSE','ALSA','SNDIO','AAUDIO')]
    subprocess.run(command,env=env,check=True)
    # CMake clears command-line options on its automatic toolchain-change
    # reconfigure. Restore the explicit options before selecting our target.
    if 'BUILD_DREAMCAST_HOST_TOOLS:BOOL=ON' not in (build/'CMakeCache.txt').read_text():
        subprocess.run(command,env=env,check=True)
        if 'BUILD_DREAMCAST_HOST_TOOLS:BOOL=ON' not in (build/'CMakeCache.txt').read_text():
            raise RuntimeError('CMake did not retain the Dreamcast frontend option')
    if not args.configure_only:
        subprocess.run([str(host/'cmake.exe'),'--build',str(build),'--target','goalc-dreamcast-ir',
                        '-j',str(args.jobs)],env=env,check=True)

if __name__=='__main__': main()
