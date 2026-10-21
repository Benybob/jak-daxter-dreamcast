"""Reproduce current compiler gaps without executing target code (AI-assisted)."""
import argparse,json,os
from pathlib import Path
import subprocess,sys
port=Path(__file__).resolve().parents[1]; repo=port.parents[1]
sys.path.insert(0,str(port))
from lower_sh4_ir import compile_module
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--frontend',type=Path,default=repo.parent/'og-host/bin/goalc-dreamcast-ir.exe')
parser.add_argument('--output',type=Path,default=port/'build/repros')
args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
env=os.environ.copy()
if os.name=='nt': env['PATH']='C:/msys64/mingw64/bin'+os.pathsep+env['PATH']
for stem,name in [('local-vars','dc-readiness-local'),('early-return','dc-readiness-early')]:
    source=Path(__file__).parent/(stem+'.gc'); ir=args.output/(stem+'.ir.json')
    result=subprocess.run([str(args.frontend.resolve()),str(repo),str(source),str(ir)],env=env,capture_output=True,text=True)
    (args.output/(stem+'.log')).write_text(result.stdout+result.stderr)
    if result.returncode: print(stem+': frontend rejected; see log'); continue
    try:
        compile_module(json.loads(ir.read_text()),[name],True)
        print(stem+': lowered successfully; native behavior still requires testing')
    except ValueError as error: print(stem+': '+str(error))
