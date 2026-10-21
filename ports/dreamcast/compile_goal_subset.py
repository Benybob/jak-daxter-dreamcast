"""Original GOAL reader/typechecker -> typed IR -> native SH-4 (AI-assisted)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from lower_sh4_ir import module_data

def main():
    port=Path(__file__).resolve().parent
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend',type=Path,required=True)
    parser.add_argument('--source',type=Path,default=port/'goal_fixture.gc')
    parser.add_argument('--function',action='append',default=[])
    parser.add_argument('--select-defuns',action='store_true',help='Read selected original forms without compiling their enclosing file')
    parser.add_argument('--header',type=Path,default=port/'build/goal_fixture.h')
    parser.add_argument('--msys',type=Path,default=Path('C:/msys64'))
    parser.add_argument('--runtime',action='store_true')
    parser.add_argument('--no-optimize',action='store_true')
    parser.add_argument('--initializer',help='Also compile original top-level initialization with this unique export name')
    parser.add_argument('--native-module',type=Path,help='Also produce a native DCSH file for the runtime loader')
    args=parser.parse_args()
    if not args.function and args.initializer is None:
        parser.error('select at least one function or an initializer')
    if args.select_defuns and not args.function:
        parser.error('--select-defuns requires at least one original defun')
    header=args.header.resolve(); header.parent.mkdir(parents=True,exist_ok=True)
    intermediate=header.with_suffix('.ir.json')
    manifest=header.with_suffix('.manifest.json')
    env=os.environ.copy()
    if os.name=='nt':
        env['PATH']=str(args.msys.resolve()/'mingw64/bin')+os.pathsep+env['PATH']
    command=[str(args.frontend.resolve()),str(port.parents[1]),str(args.source.resolve()),str(intermediate)]
    if args.select_defuns: command+=args.function
    subprocess.run(command,env=env,check=True)
    command=[sys.executable,str(port/'lower_sh4_ir.py'),str(intermediate),'--header',str(header),'--manifest',str(manifest)]
    for name in args.function: command+=['--function',name]
    if args.runtime: command+=['--runtime']
    if args.no_optimize: command+=['--no-optimize']
    if args.initializer is not None: command+=['--initializer',args.initializer]
    if args.native_module: command+=['--native-module',str(args.native_module.resolve())]
    subprocess.run(command,check=True)
    report={'scope':('Scalar typed GOAL with native tagged values/symbol bindings; incomplete kernel' if args.runtime else 'Numeric typed GOAL subset; native SH-4, no complete game runtime'),
            'runtime_symbols':args.runtime,
            'optimizations':not args.no_optimize,
            'initializer':args.initializer,
            'source':str(args.source.resolve()),
            'source_sha256':hashlib.sha256(args.source.read_bytes()).hexdigest(),
            'frontend_sha256':hashlib.sha256(args.frontend.read_bytes()).hexdigest(),
            'ir_sha256':hashlib.sha256(intermediate.read_bytes()).hexdigest(),
            'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),
            'selected_original_defuns':args.select_defuns,
            'functions':json.loads(manifest.read_text())}
    document=json.loads(intermediate.read_text())
    data=module_data(document,{str(i):(b'',f) for i,f in enumerate(report['functions'])})
    referenced={d['name'] for d in data}
    report['static_data']=[s for s in document.get('statics',[]) if s['id'] in referenced]
    report['static_data_bytes']=sum(len(d['data']) for d in data)
    if args.native_module:
        report['native_module_sha256']=hashlib.sha256(args.native_module.read_bytes()).hexdigest()
    manifest.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__': main()
