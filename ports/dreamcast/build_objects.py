"""Build native original-data parser probes, optionally with a local asset (AI-assisted)."""
import argparse
from pathlib import Path
import subprocess
import sys
def main():
    port=Path(__file__).resolve().parent; work=port.parents[2]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--retail-archive',type=Path)
    parser.add_argument('--retail-art-archive',type=Path)
    parser.add_argument('--verify',action='store_true')
    parser.add_argument('--flycast',type=Path,default=work/'flycast/flycast.exe')
    parser.add_argument('--nm',type=Path,default=work/'dc-sdk/sh-elf/bin/sh-elf-nm.exe')
    parser.add_argument('--output',type=Path,default=work/'flycast-objects')
    args=parser.parse_args()
    command=[sys.executable,str(port/'make_object_fixture.py'),'--header',str(port/'build/object_fixture.h')]
    if args.retail_archive: command+=['--retail-archive',str(args.retail_archive)]
    if args.retail_art_archive: command+=['--retail-art-archive',str(args.retail_art_archive)]
    subprocess.run(command,check=True)
    subprocess.run([sys.executable,str(port/'build_dreamcast.py'),'--target','jak-dc-object.elf'],check=True)
    if args.verify:
        subprocess.run([sys.executable,str(port/'verify_backend.py'),str(args.flycast),str(port/'jak-dc-object.elf'),
                        '--nm',str(args.nm),'--output',str(args.output),'--suite','objects',
                        '--tests',str(98+(17 if args.retail_archive else 0)+(67 if args.retail_art_archive else 0))],check=True)
if __name__=='__main__': main()
