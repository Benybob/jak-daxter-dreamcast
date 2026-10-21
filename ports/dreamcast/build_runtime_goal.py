"""Reproduce native runtime/compiled GOAL integration checks (AI-assisted)."""
import argparse
from pathlib import Path
import subprocess
import sys

def main():
    port=Path(__file__).resolve().parent
    work=port.parents[2]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend',type=Path,default=work/'og-host/bin/goalc-dreamcast-ir.exe')
    parser.add_argument('--verify',action='store_true')
    parser.add_argument('--flycast',type=Path,default=work/'flycast/flycast.exe')
    parser.add_argument('--nm',type=Path,default=work/'dc-sdk/sh-elf/bin/sh-elf-nm.exe')
    parser.add_argument('--output',type=Path,default=work/'flycast-runtime-goal')
    args=parser.parse_args()
    compiler=[sys.executable,str(port/'compile_goal_subset.py'),'--frontend',str(args.frontend),'--runtime']
    names=('read','write','false','true','not','empty','call-read','constant')
    command=compiler+['--source',str(port/'runtime_fixture.gc'),'--header',str(port/'build/runtime_fixture.h')]
    for name in names: command+=['--function','dc-runtime-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'initializer_fixture.gc'),
                      '--header',str(port/'build/initializer_fixture.h'),
                      '--initializer','dc-init-top-level']
    for name in ('read','bump','result','alias-value'): command+=['--function','dc-init-'+name]
    subprocess.run(command,check=True)
    identity=compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                       '--function','identity','--select-defuns']
    subprocess.run(identity+['--initializer','dc-identity-top-level',
                             '--header',str(port/'build/runtime_identity.h')],check=True)
    subprocess.run(identity+['--no-optimize','--header',str(port/'build/runtime_identity_baseline.h')],check=True)
    names=['car','cdr','set-car','set-cdr']+['load-'+kind for kind in
            ('i8','u8','i16','u16','i32','u32','i64','u64')]
    names+=['store-'+kind for kind in ('u8','u16','u32','u64')]+['load-float','store-float','type','offset']
    command=compiler+['--source',str(port/'memory_fixture.gc'),'--header',str(port/'build/memory_fixture.h')]
    for name in names: command+=['--function','dc-mem-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'static_fixture.gc'),'--header',str(port/'build/static_fixture.h'),
                      '--initializer','dc-static-top-level']
    for name in ('read-text','literal','read-float','number','float-add','string-length','first-char'):
        command+=['--function','dc-static-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                      '--select-defuns','--header',str(port/'build/original_lists.h')]
    for name in ('ref','last','member','assoc'): command+=['--function',name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'float_fixture.gc'),'--header',str(port/'build/float_fixture.h')]
    for name in ('eq','ne','lt','le','gt','ge','to-int','to-float','min','max','sqrt'):
        command+=['--function','dc-fp-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port.parents[1]/'goal_src/jak1/engine/math/math.gc'),
                      '--select-defuns','--header',str(port/'build/original_math.h')]
    for name in ('truncate','integral?','fractional-part','seek','lerp-clamp','lerp-scale'):
        command+=['--function',name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'bridge_fixture.gc'),'--header',str(port/'build/bridge_fixture.h')]
    for name in ('cons','type','third','eighth','stack-call','return-cdr'): command+=['--function','dc-bridge-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'quoted_fixture.gc'),'--header',str(port/'build/quoted_fixture.h'),
                      '--initializer','dc-quoted-top-level']
    for name in ('root','literal','head'): command+=['--function','dc-quoted-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'void_fixture.gc'),'--header',str(port/'build/void_fixture.h'),
                      '--initializer','dc-void-top-level']
    for name in ('write','clear','add','empty','read','call','call-read','dispatch'):
        command+=['--function','dc-void-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'type_fixture.gc'),'--header',str(port/'build/type_fixture.h'),
                      '--initializer','dc-type-top-level']
    for name in ('(method new dc-native-record)','(method score dc-native-record)'):
        command+=['--function',name]
    for name in ('create','child','read','write','score','of','alternate','install'):
        command+=['--function','dc-type-'+name]
    subprocess.run(command,check=True)
    subprocess.run(compiler+['--source',str(port/'void_empty.gc'),'--header',str(port/'build/void_empty.h'),
                             '--initializer','dc-void-empty-top-level'],check=True)
    command=compiler+['--source',str(port/'shift_fixture.gc'),'--header',str(port/'build/shift_fixture.h')]
    for name in ('left','logical','arithmetic','self','mix'):
        command+=['--function','dc-shift-'+name]
    for kind in ('left','logical','arithmetic'):
        for amount in (0,1,31,32,63): command+=['--function',f'dc-shift-{kind}-{amount}']
    subprocess.run(command,check=True)
    baseline=list(command); baseline[baseline.index('--header')+1]=str(port/'build/shift_baseline.h')
    subprocess.run(baseline+['--no-optimize'],check=True)
    subprocess.run(compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                             '--select-defuns','--function','ash','--header',str(port/'build/original_shift.h')],check=True)
    command=compiler+['--source',str(port/'division_fixture.gc'),'--header',str(port/'build/division_fixture.h')]
    for name in ('signed','signed-rem','unsigned','unsigned-rem','mix','call','same','seven'):
        command+=['--function','dc-div-'+name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port.parents[1]/'goal_src/jak1/kernel/gcommon.gc'),
                      '--select-defuns','--header',str(port/'build/original_division.h')]
    for name in ('/','mod','rem'): command+=['--function',name]
    subprocess.run(command,check=True)
    command=compiler+['--source',str(port/'native_file_fixture.gc'),'--header',str(port/'build/native_file_fixture.h'),
                      '--initializer','dc-file-top-level','--native-module',str(port/'build/native_fixture.dcn')]
    for name in ('read','add','string','list'): command+=['--function','dc-file-'+name]
    subprocess.run(command,check=True)
    subprocess.run([sys.executable,str(port/'embed_native_fixture.py'),str(port/'build/native_fixture.dcn'),
                    str(port/'build/native_file_blob.h')],check=True)
    command=compiler+['--source',str(port/'locals_fixture.gc'),'--header',str(port/'build/locals_fixture.h')]
    for name in ('zero','basic','mutate','eight','float','branch'): command+=['--function','dc-local-'+name]
    command+=['--function','(method score dc-local-record)']
    subprocess.run(command,check=True)
    subprocess.run([sys.executable,str(port/'build_dreamcast.py'),'--target','jak-dc-runtime-goal.elf'],check=True)
    if args.verify:
        subprocess.run([sys.executable,str(port/'verify_backend.py'),str(args.flycast),
                        str(port/'jak-dc-runtime-goal.elf'),'--nm',str(args.nm),'--output',str(args.output),
                        '--tests','8014','--suite','runtime-goal'],check=True)

if __name__=='__main__': main()
