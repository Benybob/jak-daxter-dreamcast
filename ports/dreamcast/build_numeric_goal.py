"""Reproduce numeric calls and original lerp on SH-4 (AI-assisted)."""
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
    parser.add_argument('--output',type=Path,default=work/'flycast-numeric-goal')
    args=parser.parse_args()
    compiler=[sys.executable,str(port/'compile_goal_subset.py'),'--frontend',str(args.frontend)]
    command=compiler+['--header',str(port/'build/goal_fixture.h')]
    for name in ('add','sub','choose','factorial','float','call','float-call'):
        command+=['--function','dc-goal-'+name]
    subprocess.run(command,check=True)
    subprocess.run(compiler+['--source',str(port.parents[1]/'goal_src/jak1/engine/math/math.gc'),
                            '--select-defuns','--function','lerp','--header',str(port/'build/game_leaf.h')],check=True)
    subprocess.run([sys.executable,str(port/'build_dreamcast.py'),'--target','jak-dc-goal-leaf.elf'],check=True)
    if args.verify:
        subprocess.run([sys.executable,str(port/'verify_backend.py'),str(args.flycast),
                        str(port/'jak-dc-goal-leaf.elf'),'--nm',str(args.nm),'--output',str(args.output),
                        '--tests','39'],check=True)

if __name__=='__main__': main()
