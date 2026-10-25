"""Prepare a new synthetic/private GPU catalog test directory (AI-assisted), ISC."""
from pathlib import Path
import argparse,json,sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'tests'))
from test_render_sectors import _solo,_package
import prepare_render_sectors as r
def make(out,manifest=None):
    out=Path(out)
    if out.exists():raise ValueError('output must be a new directory')
    if manifest:blob=Path(manifest).read_bytes()
    else:
        b=_solo({'doubleSided':True,'pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicRoughnessTexture':{'index':1}},
                 'extensions':{'KHR_materials_specular':{'specularColorTexture':{'index':2}}}},
                images=[(255,0,0,255),(255,0,123,255),(0,255,0,255)],textures=[{'source':0},{'source':1},{'source':2}])
        blob=_package(b.to_bytes()).manifest
    h=r.parse_manifest(blob);colors=[t for t in h['textures'] if t['format']==1]
    out.mkdir(parents=True);(out/'manifest.drm').write_bytes(blob)
    report=dict(ai_assisted=True,native_tests_expected=5*len(colors)+20,colors=len(colors),color_bytes=sum(t['pixel_bytes'] for t in colors),strength_slots=h['texture_count']-len(colors),gpu_geometry=False)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('out',type=Path);ap.add_argument('--manifest',type=Path)
    a=ap.parse_args();print(json.dumps(make(a.out,a.manifest),indent=2))
