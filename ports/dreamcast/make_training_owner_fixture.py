"""Original-free immutable missing/trailing selected-file cases (AI-assisted)."""
from pathlib import Path
import argparse,json,sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'tests'))
from test_render_sectors import GlbBuilder,_package
import prepare_render_sectors as r
def make(out,header):
    out=Path(out)
    if out.exists():raise ValueError('output must be new')
    b=GlbBuilder();xyz=[(0,0,0),(1,0,0),(0,1,1),(40,0,0),(41,0,0),(40,1,1),(-1000,0,0),(1000,0,0),(0,1,1)]
    pos=b.add_vec3(xyz);idx=b.add_indices(list(range(9)))
    b.scenes[0]['nodes']=[b.add_node(mesh=b.add_mesh([dict(attributes=dict(POSITION=pos),indices=idx)]))]
    packet=_package(b.to_bytes(),cell_size=16);h=r.parse_manifest(packet.manifest);out.mkdir(parents=True)
    far=[key for key,data in packet.sectors.items() if any(f['source_id']==1 for f in r.parse_sector(data)['records'])]
    assert far
    for name in ('missing','trailing'):
        r.publish_package(out/name,packet)
        for key in far:
            path=out/name/'sectors'/f'{key[0]}_{key[1]}.drs'
            if name=='missing':path.unlink()
            else:path.write_bytes(path.read_bytes()+b'\x00')
    initial=r.assemble_window(packet,(-18.4,18.4,-18.4,18.4));assert len(initial)==2
    text='/* Original-free source and initial geometry oracle (AI-assisted). */\n'
    text+='static const char owner_source_sha[]='+json.dumps(h['source_sha256'].hex())+';\n'
    text+=f'#define OWNER_INITIAL_CRC {r.crc32(b"".join(f["raw"] for f in initial))}u\n'
    header=Path(header);header.parent.mkdir(parents=True,exist_ok=True);header.write_text(text)
    report=dict(ai_assisted=True,source_assets='procedural original-free',far_files=far,initial_faces=2,global_faces=h['global_count'])
    (out/'OWNER_CASES.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('output',type=Path);ap.add_argument('--header',type=Path,required=True);a=ap.parse_args();print(json.dumps(make(a.output,a.header)))
