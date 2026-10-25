"""Resident catalog staging/rollback with original-free inputs (AI-assisted), ISC."""
from pathlib import Path
import json,subprocess,sys,tempfile,unittest,ctypes
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from test_render_sectors import _package,_interior,_solo
EXE=Path(__file__).resolve().parents[1]/'build/render-gpu-check.exe'
if not EXE.exists():EXE=EXE.with_suffix('')
@unittest.skipUnless(EXE.exists(),'build resident-catalog host oracle')
class RenderGpuOwner(unittest.TestCase):
    def run_case(self,p,colors,color_bytes,slots):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'manifest.drm';path.write_bytes(p.manifest)
            run=subprocess.run([str(EXE),str(path)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            result=json.loads(run.stdout)
            self.assertEqual(result['total'],5*colors+20);self.assertEqual(result['passed'],result['total'])
            self.assertEqual(result['colors'],colors);self.assertEqual(result['bytes'],color_bytes)
            self.assertEqual(result['peak_vram'],2*color_bytes)
            self.assertEqual(result['peak_cpu'],2*slots*ctypes.sizeof(ctypes.c_void_p))
    def test_color_and_strength_ownership_with_each_allocation_failure(self):
        b=_solo({'pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicRoughnessTexture':{'index':1}},
                 'extensions':{'KHR_materials_specular':{'specularColorTexture':{'index':2}}}},
                images=[(255,0,0,255),(255,0,123,255),(0,255,0,255)],textures=[{'source':0},{'source':1},{'source':2}])
        self.run_case(_package(b.to_bytes()),2,256,3)
    def test_untextured_base_owner_survives_staged_replacement(self):
        self.run_case(_package(_interior().to_bytes()),1,128,1)
