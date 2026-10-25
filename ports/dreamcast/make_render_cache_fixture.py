"""Original-free cache oracles; optional private training windows (AI-assisted), ISC."""
from pathlib import Path
import argparse,json,sys,shutil
sys.path.insert(0,str(Path(__file__).resolve().parent/'tests'))
import prepare_render_sectors as r
from test_render_sectors import GlbBuilder,_package,_one
def make(root,header=None,real=None,windows=None,first=0,count=None,reuse=False):
    root=Path(root)
    if reuse:
        report=json.loads((root/'CASES.json').read_text());rows=json.loads(Path(windows).read_text()) if windows else []
        chosen=select_rows(rows,first,count)
        if header:write_header(report['cases'],chosen,header)
        return dict(report,real_windows=len(chosen),first_window=first)
    if root.exists():raise ValueError('fixture output must be new')
    root.mkdir(parents=True);cases=[]
    def add(name,p,bounds,expected):
        try:
            faces=r.assemble_window(p,bounds)
            answer=dict(result='PASS',count=len(faces),crc=r.crc32(b''.join(f['raw'] for f in faces)),source_ids=[f['source_id'] for f in faces])
        except r.RenderSectorError as error:answer=dict(result='REFUSE',error=str(error))
        assert answer['result']==expected,(name,answer)
        r.publish_package(root/name,p);cases.append(dict(name=name,bounds=bounds,expected=answer))
    b=GlbBuilder();positions=[]
    for ix in range(5):
        for _ in range(4095 if ix==0 else 4096):
            x=ix*4+1;positions.extend([(x,0,1),(x+.01,0,1),(x,.2,1.2)])
    positions.extend([(2,0,3.9),(2.1,0,3.9),(2,.2,4.1)])
    a=b.add_vec3(positions);idx=b.add_indices(list(range(len(positions))))
    b.scenes[0]['nodes']=[b.add_node(mesh=b.add_mesh([{'attributes':{'POSITION':a},'indices':idx}]))]
    p=_package(b.to_bytes(),cell_size=4)
    add('three-batches-one-face',p,(0,20,4,4.2),'PASS')
    add('three-batches-too-many-included',p,(0,20,0,4.2),'REFUSE')
    p=_package(_one([(3.9,0,1),(4.1,0,1),(4,.2,1.2)]).to_bytes(),cell_size=4)
    add('both-filtered-valid',p,(0,8,4,4.2),'PASS')
    key=sorted(p.sectors)[0];raw=bytearray(r.parse_sector(p.sectors[key])['records'][0]['raw']);raw[8]^=1
    sectors=dict(p.sectors);sectors[key]=r.encode_sector([bytes(raw)],*key);p=r.relink_package(p,sectors=sectors)
    add('both-filtered-mismatch',p,(0,8,4,4.2),'REFUSE')
    p=_package(_one([(0,0,0),(1897,0,0),(0,0,10)]).to_bytes(),cell_size=4)
    add('global-long-intersect',p,(0,8,0,8),'PASS');add('global-long-known-air',p,(-20,-10,-20,-10),'PASS')
    rows=[]
    if real or windows:
        if not real or not windows:raise ValueError('real package and private window oracles required together')
        real=Path(real);r.parse_manifest((real/'manifest.drm').read_bytes())
        shutil.copytree(real,root/'real',ignore=shutil.ignore_patterns('*.json'))
        rows=json.loads(Path(windows).read_text())
    report=dict(ai_assisted=True,cases=cases,real_windows=len(rows),native_execution=False)
    (root/'CASES.json').write_text(json.dumps(report,indent=2)+'\n')
    chosen=select_rows(rows,first,count)
    if header:write_header(cases,chosen,header)
    return dict(report,real_windows=len(chosen),first_window=first)
def select_rows(rows,first,count):
    if first<0 or first>len(rows) or (count is not None and (count<1 or first+count>len(rows))):raise ValueError('invalid oracle window range')
    return rows[first:] if count is None else rows[first:first+count]
def write_header(cases,rows,header):
        lines=['/* Generated independent synthetic/host oracles (AI-assisted). */',
               'static const struct cache_case cache_cases[]={']
        for c in cases:
            q=c['bounds'];e=c['expected'];vals=(q[0],q[2],q[1],q[3])
            lines.append('{'+json.dumps(c['name'])+',{'+','.join(repr(v) for v in vals)+'},'+str(int(e['result']=='PASS'))+','+str(e.get('count',0))+','+str(e.get('crc',0))+'u},')
        lines+=['};',f'#define CACHE_REAL_WINDOWS {len(rows)}','static const struct cache_real cache_real_rows[]={']
        for row in rows:
            x=row['ix']*16+8;z=row['iz']*16+8
            lines.append('{{'+','.join(repr(v) for v in (x-18.4,z-18.4,x+18.4,z+18.4))+'},'+str(row['count'])+','+str(row['crc'])+'u},')
        if not rows:lines.append('{{0,0,0,0},0,0},')
        lines+=['};'];Path(header).parent.mkdir(parents=True,exist_ok=True);Path(header).write_text('\n'.join(lines)+'\n')
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('output',type=Path);ap.add_argument('--header',type=Path)
    ap.add_argument('--real',type=Path);ap.add_argument('--windows',type=Path)
    ap.add_argument('--first-window',type=int,default=0);ap.add_argument('--window-count',type=int);ap.add_argument('--reuse',action='store_true');a=ap.parse_args()
    result=make(a.output,a.header,a.real,a.windows,a.first_window,a.window_count,a.reuse);print(json.dumps(dict(ai_assisted=True,synthetic_cases=len(result['cases']),real_windows=result['real_windows'],first_window=a.first_window)))
