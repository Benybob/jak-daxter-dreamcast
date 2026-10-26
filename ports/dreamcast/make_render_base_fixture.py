"""Original-free base-pass raster fixture or private original window (AI-assisted)."""
from pathlib import Path
from types import SimpleNamespace
import argparse,base64,json,math,shutil,struct,sys
from io import BytesIO
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parent/'tests'))
from test_render_sectors import GlbBuilder,_package,_png_uri
import prepare_render_sectors as r

def make(out,header,real=None,query=None,camera=None):
    out=Path(out)
    if out.exists():raise ValueError('fixture output must be new')
    query=query or (-18.4,18.4,-18.4,18.4);camera=camera or (0,0,0,0,0,4)
    try:camera=tuple(struct.unpack('<f',struct.pack('<f',v))[0] for v in camera)
    except (OverflowError,struct.error) as error:raise ValueError('camera must fit binary32') from error
    if len(camera)!=6 or not all(math.isfinite(v) for v in camera) or any(abs(v)>1000000 for v in camera[:3]) or not 0<=camera[5]<=1000000:
        raise ValueError('camera outside native world-view contract')
    if real:
        real=Path(real);manifest=(real/'manifest.drm').read_bytes();h=r.parse_manifest(manifest)
        sectors={(e['ix'],e['iz']):(real/'sectors'/f"{e['ix']}_{e['iz']}.drs").read_bytes() for e in h['entries']}
        packet=SimpleNamespace(manifest=manifest,sectors=sectors,global_sector=(real/'global.drs').read_bytes())
        shutil.copytree(real,out,ignore=shutil.ignore_patterns('*.json'))
    else:
        b=GlbBuilder();b.samplers=[dict(magFilter=9728,minFilter=9728,wrapS=33071,wrapT=33071)]
        mask=Image.new('RGBA',(8,8),(0,255,0,255))
        for y in range(8):
            for x in range(4,8):mask.putpixel((x,y),(0,255,0,0))
        stream=BytesIO();mask.save(stream,format='PNG')
        uris=[_png_uri((255,0,0,0)), 'data:image/png;base64,'+base64.b64encode(stream.getvalue()).decode(),
              _png_uri((0,0,255,255)),_png_uri((255,255,0,255)),_png_uri((255,0,127,255)),_png_uri((0,255,255,255))]
        b.images=[dict(uri=v) for v in uris];b.textures=[dict(source=i,sampler=0) for i in range(6)]
        b.materials=[dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=0),metallicRoughnessTexture=dict(index=4)),extensions={'KHR_materials_specular':dict(specularColorTexture=dict(index=5))}),
                     dict(alphaMode='MASK',alphaCutoff=.5,pbrMetallicRoughness=dict(baseColorTexture=dict(index=1))),
                     dict(pbrMetallicRoughness=dict(baseColorTexture=dict(index=2))),
                     dict(doubleSided=True,pbrMetallicRoughness=dict(baseColorTexture=dict(index=2))),
                     dict(doubleSided=True,pbrMetallicRoughness=dict(baseColorTexture=dict(index=3)))]
        triangles=[([(40,100),(280,100),(160,400)],1), ([(360,100),(600,100),(480,400)],1),
                   ([(20,420),(60,470),(100,420)],1), ([(520,420),(560,470),(600,420)],1),
                   ([(-1000,-1000),(1640,-1000),(320,2000)],2)]
        primitives=[]
        for material,(points,depth) in enumerate(triangles):
            pos=b.add_vec3([((x-320)*depth*4/390,(260-y)*depth*4/390,(depth-1)*4) for x,y in points])
            uv=b.add_vec2([(0,0),(1,0),(.5,1)])
            primitives.append(dict(attributes=dict(POSITION=pos,TEXCOORD_0=uv),indices=b.add_indices([0,1,2]),material=material))
        b.scenes[0]['nodes']=[b.add_node(mesh=b.add_mesh(primitives))];packet=_package(b.to_bytes(),cell_size=16)
        r.publish_package(out,packet);manifest=packet.manifest;h=r.parse_manifest(manifest)
    faces=r.assemble_window(packet,query);raw=b''.join(f['raw'] for f in faces)
    q=(query[0],query[2],query[1],query[3]);f=lambda v:repr(float(v))+'f'
    text='/* Generated raster fixture, private if real is supplied (AI-assisted). */\n'
    text+='static const dc_render_query base_query={'+','.join(repr(v) for v in q)+'};\n'
    text+='static const dc_render_camera base_camera={{'+','.join(f(v) for v in camera[:3])+'},'+','.join(f(v) for v in camera[3:])+'};\n'
    text+=f'#define BASE_EXPECTED_FACES {len(faces)}u\n#define BASE_EXPECTED_CRC {r.crc32(raw)}u\n#define BASE_SYNTHETIC {int(real is None)}\n'
    text+='static const char base_source_sha[]='+json.dumps(h['source_sha256'].hex())+';\n'
    header=Path(header);header.parent.mkdir(parents=True,exist_ok=True);header.write_text(text)
    report=dict(ai_assisted=True,original_assets=bool(real),faces=len(faces),crc=r.crc32(raw),camera=camera,query=query,materials=h['material_count'],scope='Base pass only; secondary maps preserved but not shaded',raster_samples=({} if real else {'red':[160,200],'green':[430,200],'mask_hole':[530,200],'single_back':[60,440],'double_back':[560,440],'clipped_background':[5,5]}))
    (out/'BASE_CASE.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('output',type=Path);ap.add_argument('--header',type=Path,required=True)
    ap.add_argument('--real',type=Path);ap.add_argument('--query',type=float,nargs=4);ap.add_argument('--camera',type=float,nargs=6)
    a=ap.parse_args();print(json.dumps(make(a.output,a.header,a.real,a.query,a.camera),indent=2))
