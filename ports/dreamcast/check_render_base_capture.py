"""Check completed native base-pass framebuffer/ownership evidence (AI-assisted)."""
from pathlib import Path
import argparse,hashlib,json,struct

def check(out,fixture):
    out=Path(out);fixture=json.loads(Path(fixture).read_text());native=json.loads((out/'result.json').read_text())
    raw=(out/'frame.ppm').read_bytes();metrics=struct.unpack('<10I',(out/'metrics.bin').read_bytes())
    keys=('frames','faces','triangles','clipped','culled','secondary','cpu_live','cpu_peak','vram_bytes','frame_us')
    metrics=dict(zip(keys,metrics));checks=dict(native_pass=native['result']=='PASS',ram_16mib=native['system_ram']==16777216,
        capture_hash=hashlib.sha256(raw).hexdigest()==native.get('framebuffer_sha256'),
        capture_format=len(raw)==921615 and raw.startswith(b'P6\n640 480\n255\n'),
        all_faces=metrics['faces']==fixture['faces'],completed_frames=metrics['frames']==12,
        triangles_submitted=metrics['triangles']>0,CPU_owners_released=metrics['cpu_live']==0)
    def rgb(x,y):return tuple(raw[15+(y*640+x)*3:15+(y*640+x)*3+3])
    samples={name:rgb(*point) for name,point in fixture['raster_samples'].items()}
    if not fixture['original_assets']:
        expected={'red':(255,0,0),'green':(0,255,0),'mask_hole':(255,255,0),
                  'single_back':(255,255,0),'double_back':(0,0,255),'clipped_background':(255,255,0)}
        checks.update({name:samples.get(name)==color for name,color in expected.items()})
        checks.update(clipping_exercised=metrics['clipped']>0,sidedness_exercised=metrics['culled']>0,
                      secondary_maps_retained=metrics['secondary']==1)
    report=dict(ai_assisted=True,result='PASS' if all(checks.values()) else 'FAIL',checks=checks,metrics=metrics,
                sampled_rgb=samples,elf_sha256=native['elf_sha256'],framebuffer_sha256=hashlib.sha256(raw).hexdigest(),
                original_assets=fixture['original_assets'],visual_review_required=fixture['original_assets'],
                scope='Base-pass geometry/texture/clip/sidedness and ownership only; no reflection/TOD shader, gameplay integration or hardware validation')
    (out/'capture-verification.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('output',type=Path);ap.add_argument('--fixture',type=Path,required=True)
    a=ap.parse_args();report=check(a.output,a.fixture);print(json.dumps(report,indent=2));raise SystemExit(report['result']!='PASS')
