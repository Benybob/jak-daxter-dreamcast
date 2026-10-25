"""Build the standalone original-LOD FR3 exporter on Windows (AI-assisted), ISC."""
import argparse,ctypes,os,subprocess
from pathlib import Path
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--msys',type=Path,default=Path('C:/msys64'));a=parser.parse_args()
    if os.name!='nt':parser.error('Windows build helper; use the same source/dependency list with a native C++20 compiler elsewhere')
    port=Path(__file__).resolve().parent;repo=port.parents[1];sdk=a.msys.resolve()/'mingw64'
    build=port/'build';build.mkdir(exist_ok=True)
    def short(path):
        value=ctypes.create_unicode_buffer(4096)
        if not ctypes.windll.kernel32.GetShortPathNameW(str(path),value,4096):raise OSError('Windows short path unavailable: '+str(path))
        return value.value
    env=os.environ.copy();env['PATH']=short(sdk/'bin')+';'+env['PATH'];cc=short(sdk/'bin/g++.exe')
    version=subprocess.check_output([cc,'-dumpfullversion'],env=env,text=True).strip()
    target=subprocess.check_output([cc,'-dumpmachine'],env=env,text=True).strip()
    args=[cc,'-std=c++20','-O2','-DFMT_HEADER_ONLY','-ffunction-sections','-fdata-sections']
    target_headers=sdk/'include/c++'/version/target
    if target_headers.exists():args+=['-isystem',short(target_headers)]
    args+=['-I'+short(p) for p in (repo,repo/'third-party/fmt/include',repo/'third-party',repo/'third-party/stb_image')]
    syntax=subprocess.call(args+['-fsyntax-only',short(port/'export_background_fr3.cpp')],env=env)
    if syntax:return syntax
    sources=['ports/dreamcast/export_background_fr3.cpp','common/custom_data/TFrag3Data.cpp','decompiler/level_extractor/fr3_to_gltf.cpp','decompiler/level_extractor/tfrag_tie_fixup.cpp','third-party/tiny_gltf/tiny_gltf.cpp','third-party/stb_image/stb_image.cpp','common/math/geometry.cpp']
    args += [short(repo/s) for s in sources]+['-Wl,--gc-sections','-static','-o',str(build/'export-background.exe')]
    return subprocess.call(args,env=env)
if __name__=='__main__':raise SystemExit(main())
