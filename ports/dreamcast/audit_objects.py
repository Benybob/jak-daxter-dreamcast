"""Audit local retail V4 data with the native shared parser (AI-assisted)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
def main():
    port=Path(__file__).resolve().parent
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso_root',type=Path)
    parser.add_argument('--checker',type=Path,default=port/'build'/('object-check.exe' if sys.platform=='win32' else 'object-check'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    records=[]; archives=skipped=0
    def header(stream):
        data=stream.read(64)
        if len(data)!=64 or b'\0' not in data[4:]: raise ValueError('invalid archive/object header')
        return struct.unpack_from('<I',data)[0],data[4:].split(b'\0',1)[0].decode('ascii')
    with tempfile.TemporaryDirectory() as directory:
        sample=Path(directory)/'asset.bin'
        paths=sorted([*args.iso_root.glob('DGO/*'),*args.iso_root.glob('CGO/*')])
        for path in paths:
            if not path.is_file(): continue
            archives+=1; size=path.stat().st_size
            with path.open('rb') as stream:
                count,_=header(stream)
                if count>65536: raise ValueError('compressed archive or object count limit')
                for _ in range(count):
                    length,name=header(stream); start=stream.tell()
                    if length<12 or length>size-start: raise ValueError('object outside archive')
                    prefix=stream.read(12); version=struct.unpack_from('<I',prefix,8)[0]
                    if version==4:
                        if length>8*1024*1024: raise ValueError('retail object exceeds scan budget')
                        data=prefix+stream.read(length-12); sample.write_bytes(data)
                        result=subprocess.run([str(args.checker),str(sample)],capture_output=True,text=True)
                        record={'archive':path.relative_to(args.iso_root).as_posix(),'name':name,'size':length,
                                'sha256':hashlib.sha256(data).hexdigest(),'passed':result.returncode==0}
                        if result.returncode: record['error']=result.stderr.strip()
                        else: record['layout']=json.loads(result.stdout)
                        records.append(record)
                    else: skipped+=1
                    end=start+length+((-length)&15)
                    if end>size: raise ValueError('object padding outside archive')
                    stream.seek(end)
                if stream.tell()!=size: raise ValueError('archive trailing data')
    provenance={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in
                (args.checker,port/'goal_object.c',port/'goal_object.h',port/'host_object.c')}
    report={'ai_assisted':True,'scope':'Read-only retail V4 data validation; no code execution',
            'checker_and_source_sha256':provenance,
            'archives':archives,'skipped_non_v4':skipped,'objects':records,
            'passed':sum(o['passed'] for o in records),'total':len(records)}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(f"{report['passed']}/{len(records)} V4 objects pass in {archives} archives; {skipped} non-V4 objects skipped")
    if report['passed']!=len(records): raise SystemExit(1)
if __name__=='__main__': main()
