"""Prepare private exact-payload sector oracles from local DCR1 (AI-assisted), ISC.

This host tool validates every sector payload against the source interchange,
then generates one bounded query at each occupied cell center. It is a payload
membership/rebase oracle, not an independent proof of spatial partition coverage.
Output contains game geometry and must remain private.
"""
import argparse,json,math,struct,tempfile,zlib
from pathlib import Path
import partition_collision as pc
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def prepare(source,package,output,origin_y=0):
    source,package,output=source.resolve(),package.resolve(),output.resolve()
    if output.exists():raise ValueError('output must be new')
    if source.stat().st_size>pc.MAX_DCR_BYTES:raise ValueError('source too large')
    if (package/'manifest.dcs').stat().st_size>108+4096*20+4:raise ValueError('manifest too large')
    data=source.read_bytes();faces=pc.parse_dcr(data);manifest=(package/'manifest.dcs').read_bytes();index=pc.parse_manifest(manifest)
    if index['source_count']!=len(faces) or index['source_crc']!=zlib.crc32(data):raise ValueError('source/package mismatch')
    origin_y=f32(origin_y)
    if not math.isfinite(origin_y) or abs(origin_y)>1e6:raise ValueError('invalid origin Y')
    ids={};payloads={};all_ids=set()
    for entry in index['entries']:
        key=(entry['ix'],entry['iz']);path=package/f'sectors/{key[0]}_{key[1]}.dcs'
        if path.stat().st_size!=entry['bytes']:raise ValueError('sector size mismatch')
        b=path.read_bytes();parsed=pc.parse_sector(b)
        if (parsed['ix'],parsed['iz'])!=key or parsed['face_count']!=entry['face_count'] or zlib.crc32(b[:-4])!=entry['crc']:raise ValueError('sector/index mismatch')
        rows=parsed['raw_records']
        for i,row in rows:
            if i>=len(faces) or row!=faces[i].raw:raise ValueError('sector/source payload mismatch')
            all_ids.add(i)
        ids[key]=[i for i,_ in rows];payloads[key]=b
    if len(all_ids)!=len(faces):raise ValueError('source faces missing from package')
    cell=index['cell_size'];body=bytearray(struct.pack('<4sIIII',b'SCO1',1,len(ids),len(faces),zlib.crc32(manifest[:-4])));maximum=0
    for ix,iz in sorted(ids):
        x,z=f32((ix+.5)*cell),f32((iz+.5)*cell)
        q=tuple(f32(v) for v in (x-16.4,x+16.4,z-16.4,z+16.4));origin=(x,origin_y,z)
        b=(f32(q[0]-2),f32(q[1]+2),f32(q[2]-2),f32(q[3]+2))
        selected={i for xx in range(math.ceil(b[0]/cell)-1,math.floor(b[1]/cell)+1) for zz in range(math.ceil(b[2]/cell)-1,math.floor(b[3]/cell)+1) for i in ids.get((xx,zz),())}
        kept=[i for i in sorted(selected) if max(faces[i].xs)>=b[0] and min(faces[i].xs)<=b[1] and max(faces[i].zs)>=b[2] and min(faces[i].zs)<=b[3]]
        if len(kept)>8192:raise ValueError('oracle window exceeds cache cap')
        crc=0
        for i in kept:
            values=struct.unpack('<9fI',faces[i].raw);local=[f32(v-origin[k%3]) for k,v in enumerate(values[:9])]
            if any(not math.isfinite(v) or abs(v)>4096 for v in local):raise ValueError('oracle local range')
            crc=zlib.crc32(struct.pack('<9fI',*local,values[9]),crc)
        maximum=max(maximum,len(kept));body+=struct.pack('<7fII',*q,*origin,len(kept),crc)
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as tmp:
        assert Path(tmp).resolve().parent==output.parent
        stage=Path(tmp)/'package';(stage/'sectors').mkdir(parents=True)
        for (ix,iz),b in payloads.items():(stage/f'sectors/{ix}_{iz}.dcs').write_bytes(b)
        (stage/'oracle.bin').write_bytes(body);(stage/'manifest.dcs').write_bytes(manifest)
        assert stage.resolve().is_relative_to(output.parent) and output.parent==output.resolve().parent
        stage.rename(output)
    return dict(ai_assisted=True,windows=len(ids),native_checks=7*len(ids)+16,max_faces=maximum,private_game_assets=True)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('package',type=Path);p.add_argument('output',type=Path);p.add_argument('--origin-y',type=float,default=0);a=p.parse_args()
    print(json.dumps(prepare(a.source,a.package,a.output,a.origin_y),indent=2))
