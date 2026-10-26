"""Private bounded playable bundle with complete render files (AI-assisted), ISC.

The full DRM2 catalog/index remains unchanged. Only complete sector files that
can be selected by the existing bounded controller are copied. Missing listed
files outside this deliberately partial ROM disk remain loader errors.
"""
from pathlib import Path
from collections.abc import Mapping
from types import SimpleNamespace
import argparse,hashlib,json,math,os,shutil,struct,subprocess,zlib
import prepare_render_sectors as r
PORT=Path(__file__).resolve().parent

def prepare(training,render,out,header,checker=None):
    training,render,out=map(Path,(training,render,out))
    if out.exists():raise ValueError('output must be a new directory')
    checker=Path(checker) if checker else PORT/'build'/('world-manifest-check.exe' if os.name=='nt' else 'world-manifest-check')
    if not checker.is_file():raise ValueError('build the host world-manifest checker first')
    subprocess.run([str(checker),str(training/'world.xfm'),str(training/'world.dct'),str(training/'world.dcc')],check=True,capture_output=True,text=True)
    raw=(training/'world.xfm').read_bytes()
    if len(raw)!=96 or raw[:8]!=b'WLD3\x03\0\0\0':raise ValueError('WLD3 collision coverage is required')
    origin=struct.unpack_from('<3f',raw,8);spawn=struct.unpack_from('<3f',raw,36);extent=struct.unpack_from('<3f',raw,84)
    if min(extent[0],extent[2])<18 or not all(math.isfinite(v) for v in (*origin,*spawn,*extent)):
        raise ValueError('the bounded controller requires the verified 18m collision coverage')
    if max(abs(origin[0]),abs(origin[2]))>4096:
        raise ValueError('prototype bundle requires XZ origins within 4096m for its f32 rounding bound')
    source_report=json.loads((training/'report.json').read_text());manifest=(render/'manifest.drm').read_bytes();h=r.parse_manifest(manifest)
    if source_report.get('source_glb_sha256')!=h['source_sha256'].hex():raise ValueError('collision/render source GLB mismatch')
    files={n:(training/n).read_bytes() for n in ('player.dct','player.dca','world.dct','world.dcc','world.xfm')}
    actor=files['player.dct']
    if len(actor)<32 or actor[:8]!=b'DCT1\x01\0\0\0':raise ValueError('invalid actor DCT header')
    actor_indices=struct.unpack_from('<I',actor,12)[0]
    if not actor_indices or actor_indices%3:raise ValueError('invalid actor index count')
    # The prototype enforces +/-6.8m in collision-local XZ. One absolute mm
    # covers f32 addition rounding for WLD origins bounded by4096m. It does not
    # alter cache inclusion; it only permits copying an extra complete file.
    def anchor(v):return math.floor(v/4.+.5)*4.
    bounds=(anchor(origin[0]-6.8-.001)-18.4,anchor(origin[0]+6.8+.001)+18.4,
            anchor(origin[2]-6.8-.001)-18.4,anchor(origin[2]+6.8+.001)+18.4)
    selected=[];cell=h['cell_size'];payloads={}
    for entry in h['entries']:
        ix,iz=entry['ix'],entry['iz'];x0,z0=ix*cell,iz*cell
        if x0>bounds[1] or x0+cell<bounds[0] or z0>bounds[3] or z0+cell<bounds[2]:continue
        name=f'{ix}_{iz}.drs';data=(render/'sectors'/name).read_bytes();parsed=r.parse_sector(data)
        if parsed['is_global'] or (parsed['ix'],parsed['iz'])!=(ix,iz) or len(data)!=entry['file_bytes'] or parsed['record_count']!=entry['record_count'] or parsed['payload_crc']!=entry['payload_crc'] or parsed['compressed_crc']!=entry['compressed_crc']:
            raise ValueError('selected render file does not match the original catalog')
        payloads[name]=data;selected.append((ix,iz))
    global_data=(render/'global.drs').read_bytes();g=r.parse_sector(global_data)
    if not g['is_global'] or g['record_count']!=h['global_count'] or len(global_data)!=h['global_file_bytes'] or g['payload_crc']!=h['global_payload_crc'] or g['compressed_crc']!=h['global_compressed_crc']:
        raise ValueError('global render file does not match the original catalog')
    keys={(e['ix'],e['iz']) for e in h['entries']}
    class SectorFiles(Mapping):
        def __init__(self,root):self.root=Path(root)
        def __len__(self):return len(keys)
        def __iter__(self):return iter(keys)
        def __getitem__(self,key):
            if key not in keys:raise KeyError(key)
            return (self.root/'sectors'/f'{key[0]}_{key[1]}.drs').read_bytes()
    packet=SimpleNamespace(manifest=manifest,global_sector=global_data,sectors=SectorFiles(render))
    anchors=[]
    for x in range(int(anchor(origin[0]-6.8-.001)),int(anchor(origin[0]+6.8+.001))+1,4):
        for z in range(int(anchor(origin[2]-6.8-.001)),int(anchor(origin[2]+6.8+.001))+1,4):
            q=(x-18.4,x+18.4,z-18.4,z+18.4);faces=r.assemble_window(packet,q)
            anchors.append(dict(x=x,z=z,count=len(faces),crc=r.crc32(b''.join(f['raw'] for f in faces))))
    out.mkdir(parents=True);(out/'render/sectors').mkdir(parents=True)
    for name,data in files.items():(out/name).write_bytes(data)
    (out/'render/manifest.drm').write_bytes(manifest);(out/'render/global.drs').write_bytes(global_data)
    for name,data in payloads.items():(out/'render/sectors'/name).write_bytes(data)
    # Complete index plus lazy reads makes every missing SELECTED file raise.
    # It does not silently represent omitted listed cells as empty.
    packet.sectors=SectorFiles(out/'render')
    for row in anchors:
        q=(row['x']-18.4,row['x']+18.4,row['z']-18.4,row['z']+18.4);faces=r.assemble_window(packet,q)
        if len(faces)!=row['count'] or r.crc32(b''.join(f['raw'] for f in faces))!=row['crc']:
            raise ValueError('partial bundle changed a reachable source window')
    header=Path(header);header.parent.mkdir(parents=True,exist_ok=True)
    text=('/* Private world/render file binding (AI-assisted). */\n'+
        'static const char training_render_source_sha[]='+json.dumps(h['source_sha256'].hex())+';\n'+
        f'#define TRAINING_RENDER_XFM_BYTES {len(raw)}u\n#define TRAINING_RENDER_XFM_CRC {zlib.crc32(raw)&0xffffffff}u\n')
    text+='struct training_render_oracle {int32_t x,z;uint32_t count,crc;};\nstatic const struct training_render_oracle training_render_oracles[]={\n'
    text+=''.join('{'+f"{row['x']},{row['z']},{row['count']}u,{row['crc']}u"+'},\n' for row in anchors)+'};\n'
    text+=f'#define TRAINING_RENDER_ORACLE_COUNT {len(anchors)}u\n';header.write_text(text)
    report=dict(ai_assisted=True,private_game_assets=True,source_glb_sha256=h['source_sha256'].hex(),
        origin=list(origin),spawn=list(spawn),collision_extent=list(extent),actor_triangles=actor_indices//3,partial_render_ROMdisk=True,
        full_catalog_bytes_unchanged=True,full_catalog_sha256=hashlib.sha256(manifest).hexdigest(),
        listed_sector_count=len(h['entries']),copied_sector_count=len(selected),copied_sectors=selected,
        resident_color_bytes=sum(t['pixel_bytes'] for t in h['textures'] if t['format']==1),
        resident_color_textures=sum(t['format']==1 for t in h['textures']),
        possible_query_union=bounds,source_window_oracles=anchors,partial_bundle_windows_match_full_source=True,
        storage_bytes=sum(len(d) for d in files.values())+len(manifest)+len(global_data)+sum(map(len,payloads.values())),
        sha256={n:hashlib.sha256(d).hexdigest() for n,d in files.items()},
        scope='Bounded native prototype only; complete selected source files, missing listed files fail closed; no full-game claim')
    (out/'STREAM_BUNDLE.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('training',type=Path);ap.add_argument('render',type=Path);ap.add_argument('output',type=Path)
    ap.add_argument('--header',type=Path,required=True);ap.add_argument('--checker',type=Path)
    a=ap.parse_args();print(json.dumps(prepare(a.training,a.render,a.output,a.header,a.checker),indent=2))
