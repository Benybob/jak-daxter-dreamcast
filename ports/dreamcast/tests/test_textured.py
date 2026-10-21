"""Real-format validation, indexed geometry and native texture layout (AI-assisted)."""
import base64
import copy
from io import BytesIO
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from convert_scene import read_glb
from convert_textured import convert,twiddle
from make_scene_fixture import fixture
CHECK=ROOT/'build'/('textured-check.exe' if sys.platform=='win32' else 'textured-check')

class TexturedTest(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory()
        self.root=Path(self.directory.name)
        path=self.root/'terrain.glb'
        fixture(path)
        self.doc,self.binary=read_glb(path)

    def tearDown(self): self.directory.cleanup()

    def native(self,data):
        path=self.root/'scene.dct'
        path.write_bytes(data)
        return subprocess.run([str(CHECK),str(path)],capture_output=True,text=True)

    def add_texture(self):
        image=Image.new('RGBA',(8,8),(255,0,0,255))
        for y in range(4,8):
            for x in range(8): image.putpixel((x,y),(0,255,0,0))
        stream=BytesIO(); image.save(stream,format='PNG')
        self.doc['images']=[{'uri':'data:image/png;base64,'+base64.b64encode(stream.getvalue()).decode()}]
        self.doc['textures']=[{'source':0,'sampler':0}]
        self.doc['samplers']=[{'wrapS':33071,'wrapT':33648}]
        self.doc['materials']=[{'alphaMode':'MASK','alphaCutoff':0.5,
                               'pbrMetallicRoughness':{'baseColorTexture':{'index':0}}}]
        self.doc['meshes'][0]['primitives'][0]['material']=0

    def test_indexed_roundtrip_and_shared_native_loader(self):
        data,report=convert(self.doc,self.binary,0)
        self.assertEqual(report['triangles'],512)
        self.assertEqual(report['vertices'],289)
        result=self.native(data)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)['triangles'],512)

    def test_twiddle_rectangular_and_known_hardware_order(self):
        self.assertEqual([twiddle(x,y,8,8) for x,y in [(0,0),(0,1),(1,0),(1,1),(0,2),(2,0)]],[0,1,2,3,4,8])
        for width,height in [(8,8),(8,16),(32,8),(64,128)]:
            values={twiddle(x,y,width,height) for y in range(height) for x in range(width)}
            self.assertEqual(values,set(range(width*height)))

    def test_texture_alpha_and_sampler_flags(self):
        self.add_texture()
        data,report=convert(self.doc,self.binary,0)
        self.assertEqual(report['texture_bytes'],128)
        batch=32+report['vertices']*24+report['triangles']*6
        self.assertEqual(struct.unpack_from('<I',data,batch+12)[0],37)
        pixels=batch+16+12
        self.assertEqual(struct.unpack_from('<H',data,pixels+twiddle(0,0,8,8)*2)[0],0xfc00)
        self.assertEqual(struct.unpack_from('<H',data,pixels+twiddle(0,7,8,8)*2)[0],0x03e0)
        self.assertEqual(self.native(data).returncode,0)

    def test_native_rejects_corrupt_structural_fields(self):
        self.add_texture()
        data,report=convert(self.doc,self.binary,0)
        index=32+report['vertices']*24
        batch=index+report['triangles']*6
        texture=batch+16
        mutations=[(0,b'BAD!'),(4,struct.pack('<I',2)),(8,struct.pack('<I',0xffffffff)),
                   (12,struct.pack('<I',24579)),(16,struct.pack('<I',129)),
                   (20,struct.pack('<I',65)),(24,struct.pack('<I',2097153)),
                   (32,struct.pack('<f',float('nan'))),(44,struct.pack('<f',float('inf'))),
                   (index,b'\xff\xff'),(batch,struct.pack('<I',1)),
                   (batch+8,struct.pack('<I',1)),(batch+12,struct.pack('<I',2)),
                   (texture,struct.pack('<H',7)),(texture+4,struct.pack('<I',2)),
                   (texture+8,struct.pack('<I',127))]
        for offset,replacement in mutations:
            with self.subTest(offset=offset):
                bad=data[:offset]+replacement+data[offset+len(replacement):]
                self.assertNotEqual(self.native(bad).returncode,0)
        self.assertNotEqual(self.native(data+b'extra').returncode,0)
        for end in [0,1,12,31,32,index-1,index,batch-1,batch,texture-1,texture,texture+11,len(data)-1]:
            with self.subTest(end=end): self.assertNotEqual(self.native(data[:end]).returncode,0)

    def test_conversion_rejects_unsupported_content(self):
        self.add_texture()
        for mutation in ('blend','uri','wrap','texcoord','size','mesh'):
            doc=copy.deepcopy(self.doc)
            kwargs={}
            mesh=0
            if mutation=='blend': doc['materials'][0]['alphaMode']='BLEND'
            elif mutation=='uri': doc['images'][0]['uri']='https://example.com/image.png'
            elif mutation=='wrap': doc['samplers'][0]['wrapS']=123
            elif mutation=='texcoord': doc['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['texCoord']=1
            elif mutation=='size': kwargs['size']=127
            else: mesh=-1
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):
                convert(doc,self.binary,mesh,**kwargs)

    def test_instances_and_selected_branch_transforms(self):
        self.doc['meshes'].append(copy.deepcopy(self.doc['meshes'][0]))
        self.doc['nodes']=[{'mesh':0,'matrix':[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,53]},
                           {'translation':[5,0,0],'children':[2]}, {'mesh':1}, {'mesh':1,'translation':[-5,0,0]}]
        self.doc['scenes'][0]['nodes']=[0,1,3]
        data,report=convert(self.doc,self.binary,1)
        self.assertEqual(report['triangles'],1024)
        self.assertEqual(self.native(data).returncode,0)
        self.doc['nodes'][1]['children']=[1]
        with self.assertRaisesRegex(ValueError,'cycle'): convert(self.doc,self.binary,1)

if __name__=='__main__': unittest.main()
