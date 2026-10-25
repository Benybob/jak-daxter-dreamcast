"""Asset-free malformed reader cases; optional private original ledger (AI-assisted), ISC."""
import argparse,json,sys,struct,zlib,shutil
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent/'tests'))
from test_render_sectors import _package,_interior,_solo
from test_render_package import RenderPackageNative
import prepare_render_sectors as r

def make(root,header,real=None):
    root=Path(root);header=Path(header)
    if root.exists():raise ValueError('fixture output must be new')
    root.mkdir();cases=[];p=_package(_interior().to_bytes());sector=next(iter(p.sectors.values()));helper=RenderPackageNative()
    def add(manifest,blob,global_=False,good=False,af=0,rf=0):
        n=len(cases);rel=f'synthetic/case{n:03}';folder=root/rel;folder.mkdir(parents=True)
        (folder/'m').write_bytes(manifest);(folder/'s').write_bytes(blob)
        cases.append((f'/rd/{rel}/m',f'/rd/{rel}/s',int(global_),int(good),af,rf))
    add(p.manifest,sector,good=True);add(p.manifest,p.global_sector,True,True)
    for n in range(1,4):add(p.manifest,sector,af=n)
    for n in range(1,8):add(p.manifest,sector,rf=n)
    for n in (0,255,256,len(p.manifest)-1):add(p.manifest[:n],sector)
    add(p.manifest+b'X',sector)
    for name,value in (('source_count',0),('global_count',2),('face_stride',80),('material_stride',16),
                       ('texture_count',257),('tables_bytes',0xffffffff),('package_bytes',8388609),('reserved_2',1)):
        add(helper.mutate_field(p,name,value),sector)
    for n in (0,31,len(sector)-1):add(p.manifest,sector[:n])
    h=r.parse_manifest(p.manifest);raw=bytearray(p.manifest)
    offset=256+h['texture_count']*16+h['texture_blob_bytes']+h['material_count']*64
    for pos,value in ((24,2),(204,2),(offset+8,2),(offset+12,232)):
        struct.pack_into('<I',raw,pos,value)
    add(helper.resign(raw),sector)
    add(p.manifest,sector+b'X')
    original=r.parse_sector(sector)['records'][0]['raw']
    for offset,value,kind in ((0,1,'I'),(4,1,'I'),(8,float('nan'),'f'),(20,65537,'f'),(28,float('nan'),'f')):
        raw=bytearray(original);struct.pack_into('<'+kind,raw,offset,value);m,s=helper.rewritten(p,raw);add(m,s)
    for normal in ((2,0,0),(1e-40,0,0),(float('inf'),0,0)):
        raw=bytearray(original);struct.pack_into('<3f',raw,28,*normal);m,s=helper.rewritten(p,raw);add(m,s)
    comp=zlib.compress(original,9)
    for data in (comp[:-1],comp+b'X',comp+comp,zlib.compress(bytes(500000),9)):
        m,s=helper.rewritten(p,original,compressed=data);add(m,s)
    m,s=helper.rewritten(p,b'',True,zlib.compress(bytes(8192),9));add(m,s,True)
    b=_solo({'doubleSided':True,'pbrMetallicRoughness':{'metallicRoughnessTexture':{'index':0}},
             'extensions':{'KHR_materials_specular':{'specularColorTexture':{'index':1}}}},
            images=[(255,0,128,255),(255,0,0,255)],textures=[{'source':0,'sampler':0},{'source':1}],
            samplers=[{'magFilter':9728,'minFilter':9728}])
    reflection=_package(b.to_bytes());add(reflection.manifest,next(iter(reflection.sectors.values())),good=True)
    source_count=0
    if real:
        real=Path(real);manifest=(real/'manifest.drm').read_bytes();h=r.parse_manifest(manifest)
        shutil.copytree(real,root/'real',ignore=shutil.ignore_patterns('*.json'))
        seen={}
        for file in sorted((real/'sectors').glob('*.drs'))+[real/'global.drs']:
            for face in r.parse_sector(file.read_bytes())['records']:
                sid=face['source_id'];value=r.crc32(face['raw'])
                assert sid not in seen or seen[sid]==value;seen[sid]=value
        assert set(seen)==set(range(h['source_count']))
        xor=0;total=0
        for value in seen.values():xor^=value;total=(total+value)&0xffffffff
        words=[0x3244524f,2,h['source_count'],h['local_sector_count'],h['global_count'],h['material_count'],h['texture_count'],
               h['texture_bytes'],h['package_bytes'],r.crc32(manifest),xor,total]
        (root/'oracle.bin').write_bytes(struct.pack('<12I',*words));source_count=h['source_count']
    header.parent.mkdir(parents=True,exist_ok=True)
    header.write_text('/* Generated original-free cases (AI-assisted). */\n#define DC_RENDER_HAS_REAL '+str(int(real is not None))+'\n'
                      +'static const struct render_case render_cases[] = {\n'
                      +''.join('{'+','.join(json.dumps(v) if isinstance(v,str) else str(v) for v in case)+'},\n' for case in cases)+'};\n')
    print(json.dumps(dict(ai_assisted=True,synthetic_cases=len(cases),real_source_count=source_count)))
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('output',type=Path);ap.add_argument('header',type=Path);ap.add_argument('--real-package',type=Path)
    a=ap.parse_args();make(a.output,a.header,a.real_package)
