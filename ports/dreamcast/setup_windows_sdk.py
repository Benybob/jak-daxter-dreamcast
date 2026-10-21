"""Set up an isolated native Windows SH-4/KOS build (AI-assisted).

Requires an existing MSYS2 MinGW64 host toolchain, Git, 7-Zip and Python. No
installer is executed and no global environment or registry values are changed.
The large official SDK download is cached and SHA-256 checked before extraction.
"""
import argparse
import ctypes
import hashlib
from pathlib import Path
import shutil
import subprocess
import urllib.request
import zipfile

SDK_URL='https://github.com/dreamsdk/dreamsdk/releases/download/r4-4.0.11.2508/DreamSDK-R4-Setup.zip'
SDK_SHA='a5ae11aa1bec6ebf6b920731ca0f22af7de748d28f02d0cb6ddfdcc5fe644857'
INNO_URL='https://github.com/dscharrer/innoextract/releases/download/1.9/innoextract-1.9-windows.zip'
KOS_COMMIT='b125c930da1a7969c5b9760da98254c666b3eeba'

def run(command,**kwargs):
    print('Running:',subprocess.list2cmdline([str(x) for x in command]),flush=True)
    return subprocess.run([str(x) for x in command],check=True,**kwargs)

def fetch(url,path,sha=None):
    if not path.exists():
        print('Downloading:',url,flush=True)
        temporary=path.with_suffix(path.suffix+'.partial')
        with urllib.request.urlopen(url) as response,temporary.open('wb') as output:
            shutil.copyfileobj(response,output)
        temporary.replace(path)
    if sha:
        actual=hashlib.file_digest(path.open('rb'),'sha256').hexdigest()
        if actual!=sha: raise ValueError(f'Checksum mismatch: {path}')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace',type=Path,default=Path(__file__).resolve().parents[3])
    parser.add_argument('--msys',type=Path,default=Path('C:/msys64'))
    parser.add_argument('--sevenzip',type=Path,default=Path('C:/Program Files/7-Zip/7z.exe'))
    parser.add_argument('--configure-only',action='store_true')
    args=parser.parse_args()
    work=args.workspace.resolve()
    work.mkdir(parents=True,exist_ok=True)
    msys=args.msys.resolve()
    if not (msys/'usr/bin/bash.exe').is_file() or not (msys/'mingw64/bin/gcc.exe').is_file():
        parser.error('MSYS2 MinGW64 bash and host GCC are required')
    archive=work/'DreamSDK-R4-Setup.zip'
    fetch(SDK_URL,archive,SDK_SHA)
    installer=work/'dreamsdk-installer/DreamSDK-R4-Setup.exe'
    if not installer.is_file():
        with zipfile.ZipFile(archive) as z: z.extractall(installer.parent)
    extractor=work/'innoextract/innoextract.exe'
    if not extractor.is_file():
        fetch(INNO_URL,work/'innoextract.zip')
        with zipfile.ZipFile(work/'innoextract.zip') as z: z.extractall(extractor.parent)
    packages=work/'dreamsdk-extracted/code$GetApplicationPackagesPath'
    sh_package=packages/'sh-elf-toolchain-1510-bin.7z'
    arm_package=packages/'arm-eabi-toolchain-1510-bin.7z'
    if not sh_package.is_file() or not arm_package.is_file():
        run([extractor,'-q','-I',sh_package.name,'-I',arm_package.name,'-d',packages.parent,installer])
    sh=work/'dc-sdk/sh-elf'
    arm=work/'dc-sdk/arm-eabi/arm-eabi'
    if not (sh/'bin/sh-elf-gcc.exe').is_file():
        run([args.sevenzip,'x',sh_package,'-o'+str(sh.parent),'-y'])
    if not (arm/'bin/arm-eabi-gcc.exe').is_file():
        run([args.sevenzip,'x',arm_package,'-o'+str(arm.parent),'-y'])
    kos=work/'kos-source'
    if not kos.exists():
        run(['git','-c','core.autocrlf=false','clone','--depth','1','--branch','v2.2.1',
             'https://github.com/KallistiOS/KallistiOS.git',kos])
    commit=subprocess.check_output(['git','-C',str(kos),'rev-parse','HEAD'],text=True).strip()
    if commit!=KOS_COMMIT: parser.error('Existing KOS checkout differs from the pinned revision')
    buffer=ctypes.create_unicode_buffer(4096)
    if not ctypes.windll.kernel32.GetShortPathNameW(str(work),buffer,len(buffer)):
        raise OSError('Unable to get SDK short path')
    short=buffer.value.replace('\\','/')
    posix='/'+short[0].lower()+short[2:]
    if ' ' in posix: parser.error('KOS requires a space-free SDK path; Windows short paths are unavailable')
    msys_posix=msys.as_posix()
    msys_posix='/'+msys_posix[0].lower()+msys_posix[2:]
    config=f'''# Workspace-local KOS 2.2.1 / DreamSDK R4 configuration (AI-assisted).
export KOS_ARCH=dreamcast
export KOS_SUBARCH=pristine
export KOS_BASE={posix}/kos-source
export KOS_PORTS={posix}/dc-sdk/kos-ports
export KOS_CC_BASE={posix}/dc-sdk/sh-elf
export KOS_CC_PREFIX=sh-elf
export DC_ARM_BASE={posix}/dc-sdk/arm-eabi/arm-eabi
export DC_ARM_PREFIX=arm-eabi
export DC_TOOLS_BASE={posix}/dc-sdk/bin
export KOS_GENROMFS=$KOS_BASE/utils/genromfs/genromfs
export KOS_MAKE=make
export KOS_CFLAGS="-O2 -fno-builtin"
export KOS_CPPFLAGS=""
export KOS_LDFLAGS=""
export KOS_AFLAGS=""
export KOS_INC_PATHS=""
export KOS_SH4_PRECISION=-m4-single
export DC_ARM_LDFLAGS=""
export PATH={msys_posix}/mingw64/bin:{msys_posix}/usr/bin:$KOS_CC_BASE/bin:$DC_ARM_BASE/bin:$PATH
. $KOS_BASE/environ_base.sh
'''
    (kos/'environ.sh').write_text(config,newline='\n')
    script=f'''#!/usr/bin/bash
set -eo pipefail
source {posix}/kos-source/environ.sh
echo "SH4: $(sh-elf-gcc -dumpversion) KOS: $KOS_VERSION"
make -C "$KOS_BASE/utils" -j4 SUBDIRS='bin2c genromfs'
make -C "$KOS_BASE/kernel" -j8
# GCC returns a canonical long Windows path; quote the startup-object install.
crt1=$("$KOS_CC" $KOS_CFLAGS -print-file-name=crt1.o)
cp "$KOS_BASE/kernel/arch/dreamcast/kernel/startup.o" "$crt1"
make -C "$KOS_BASE/addons" -j8
'''
    (work/'build_sdk.sh').write_text(script,newline='\n')
    if not args.configure_only:
        run([msys/'usr/bin/bash.exe','--noprofile','--norc',work/'build_sdk.sh'])
    print('KOS environment:',kos/'environ.sh')

if __name__=='__main__': main()
