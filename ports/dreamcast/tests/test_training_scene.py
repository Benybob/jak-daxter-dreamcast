"""Analytic transformed/selected terrain and bound native bundles (AI-assisted)."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
PORT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PORT))
import prepare_training_scene as training
from convert_scene import read_glb
from make_scene_fixture import fixture
CHECK=PORT/'build'/('world-manifest-check.exe' if sys.platform=='win32' else 'world-manifest-check')


class TrainingScene(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        fixture(self.root/'terrain.glb');self.doc,self.binary=read_glb(self.root/'terrain.glb')
        self.dcc=struct.pack('<4sII3f9fI',b'DCC1',1,1,0,0,0,-1,0,-1,-1,0,1,1,0,1,0)

    def tearDown(self):self.temp.cleanup()

    def native(self,dct,metadata,dcc=None):
        files=[self.root/name for name in ('world.xfm','world.dct','world.dcc')]
        for p,data in zip(files,(metadata,dct,self.dcc if dcc is None else dcc)):p.write_bytes(data)
        return subprocess.run([str(CHECK),*map(str,files)],capture_output=True,text=True)

    def test_native_bundle_and_all_metadata_truncations_and_event_values(self):
        r=subprocess.run([str(CHECK),'--self-test',str(self.root/'fixture')],capture_output=True,text=True,check=True)
        self.assertRegex(r.stdout,r'world manifest (\d+)/\1 checks')
        dct,report=training.convert_patch(self.doc,self.binary,(0,0,0))
        meta=training.manifest(dct,self.dcc,report,(-.5,3,-.5))
        self.assertEqual(len(meta),84);r=self.native(dct,meta);self.assertEqual(r.returncode,0,r.stderr)
        for index in (0,33,len(dct)-1):
            wrong=bytearray(dct);wrong[index]^=1
            self.assertNotEqual(self.native(wrong,meta).returncode,0)
        bad=bytearray(self.dcc);bad[-1]^=1
        self.assertNotEqual(self.native(dct,meta,bad).returncode,0)

    def test_hierarchy_rotation_scale_and_rebase_inverse(self):
        # Synthetic XZ terrain -> rotate Z 90 degrees after child scale2,
        # translate by collision origin. Verify all referenced points independently.
        self.doc['nodes']=[{'translation':[10,2,-4],'rotation':[0,0,2**-.5,2**-.5],'children':[1]},
                           {'mesh':0,'scale':[2,2,2]}]
        dct,report=training.convert_patch(self.doc,self.binary,(10,2,-4),(3,3,3))
        vertices=[struct.unpack_from('<5fI',dct,32+i*24)[:3] for i in range(report['vertices'])]
        restored=[tuple(v[k]*report['original_radius']+report['original_center'][k] for k in range(3)) for v in vertices]
        source=training.accessor(self.doc,self.binary,0)
        expected=[(-2*p[1],2*p[0],2*p[2]) for p in source]
        self.assertEqual(len(restored),len(expected))
        self.assertLess(max(min(sum((v[k]-q[k])**2 for k in range(3)) for q in expected) for v in restored),1e-12)
        self.assertEqual(report['triangles'],512)

    def test_true_triangle_box_intersection_and_crossing(self):
        low=(-1,-1,-1);high=(1,1,1)
        self.assertFalse(training.intersects_box([(3,0,0),(0,3,0),(3,3,0)],low,high))
        self.assertTrue(training.intersects_box([(-3,0,0),(3,0,0),(0,3,0)],low,high))
        self.assertFalse(training.intersects_box([(3,0,0),(3,1,0),(3,0,1)],low,high))

    def test_instances_and_decoding_cache_and_inactive_nodes(self):
        self.doc['nodes']=[{'mesh':0,'translation':[-2,0,0]},{'mesh':0,'translation':[2,0,0]},
                           {'mesh':0,'children':[2]}]
        self.doc['scenes'][0]['nodes']=[0,1]
        _,report=training.convert_patch(self.doc,self.binary,(0,0,0))
        self.assertEqual(report['triangles'],1024);self.assertEqual(report['decoded_source_accessors'],3)
        self.assertEqual(report['active_mesh_instances'],2)
        self.doc['scenes'][0]['nodes']=[2]
        with self.assertRaisesRegex(ValueError,'cycle'):training.convert_patch(self.doc,self.binary,(0,0,0))

    def test_selected_unsupported_content_fails(self):
        for kind in ('skin','morph','blend','ambiguous','mode','empty','badroot'):
            doc=copy.deepcopy(self.doc)
            if kind=='skin':doc['nodes'][0]['skin']=0
            elif kind=='morph':doc['meshes'][0]['primitives'][0]['targets']=[]
            elif kind=='blend':
                doc['materials']=[{'alphaMode':'BLEND'}];doc['meshes'][0]['primitives'][0]['material']=0
            elif kind=='ambiguous':doc['nodes'][0].update(matrix=training.IDENTITY,translation=[0,0,0])
            elif kind=='mode':doc['meshes'][0]['primitives'][0]['mode']=5
            elif kind=='empty':doc['nodes'][0]['translation']=[100,0,0]
            else:doc['scenes'][0]['nodes']=[-1]
            with self.subTest(kind=kind),self.assertRaises(ValueError):training.convert_patch(doc,self.binary,(0,0,0))

    def test_collision_rejects_unsupported_events_and_modes(self):
        for pat in (1<<14,32<<14,63<<14,3<<3):
            bad=self.dcc[:-4]+struct.pack('<I',pat)
            with self.assertRaises(ValueError):training.collision_info(bad)
        self.assertEqual(training.collision_info(self.dcc[:-4]+struct.pack('<I',(63<<14)|1))[1],1)

    def test_deterministic_transactional_preparation(self):
        (self.root/'source.dcc').write_bytes(self.dcc)
        paths=(self.root/'first',self.root/'second')
        for output in paths:training.prepare(self.root/'terrain.glb',self.root/'source.dcc',output,(0,3,0))
        for name in ('world.dct','world.dcc','world.xfm','report.json'):
            self.assertEqual((paths[0]/name).read_bytes(),(paths[1]/name).read_bytes())
        with self.assertRaisesRegex(ValueError,'new private directory'):
            training.prepare(self.root/'terrain.glb',self.root/'source.dcc',paths[0],(0,3,0))
        invalid=self.root/'invalid'
        with self.assertRaises(ValueError):
            training.prepare(self.root/'terrain.glb',self.root/'source.dcc',invalid,(float('nan'),0,0))
        self.assertFalse(invalid.exists())
        self.assertNotIn(str(self.root),json.dumps(json.loads((paths[0]/'report.json').read_text())))

    def test_numpy_and_scalar_selection_agree(self):
        fast=training.convert_patch(self.doc,self.binary,(0,0,0),(.7,2,.5))
        saved=training.np
        try:
            training.np=None;slow=training.convert_patch(self.doc,self.binary,(0,0,0),(.7,2,.5))
        finally:training.np=saved
        self.assertEqual(fast,slow)

    def test_wld3_coverage_roundtrip_and_bad_extents(self):
        dct,report=training.convert_patch(self.doc,self.binary,(0,0,0))
        metadata=training.manifest(dct,self.dcc,report,(0,3,0),(18,12,18))
        self.assertEqual(len(metadata),96)
        self.assertEqual(self.native(dct,metadata).returncode,0)
        for cut in range(84,96):self.assertNotEqual(self.native(dct,metadata[:cut]).returncode,0)
        for extent in ((0,12,18),(18,float('nan'),18),(65,12,18)):
            with self.assertRaises(ValueError):training.manifest(dct,self.dcc,report,(0,3,0),extent)

    def test_coverage_provenance_is_bound_to_exact_collision_payload(self):
        import hashlib
        path=self.root/'collision.json';dcc_path=self.root/'source.dcc';dcc_path.write_bytes(self.dcc)
        provenance=dict(payload_sha256=hashlib.sha256(self.dcc).hexdigest(),selected_triangles=1,payload_bytes=len(self.dcc),
            whole_intersecting_triangles=True,origin_meters=[0,0,0],coverage_format_version=1,source_units_per_meter=4096,
            selection_radius=18,coverage_half_extent_meters=[18,12,18])
        path.write_text(json.dumps(provenance))
        training.prepare(self.root/'terrain.glb',dcc_path,self.root/'covered',(0,3,0),collision_report=path)
        self.assertEqual((self.root/'covered/world.xfm').read_bytes()[:8],b'WLD3\x03\0\0\0')
        provenance['payload_sha256']='0'*64;path.write_text(json.dumps(provenance))
        with self.assertRaisesRegex(ValueError,'provenance mismatch'):
            training.prepare(self.root/'terrain.glb',dcc_path,self.root/'wrong',(0,3,0),collision_report=path)
        self.assertFalse((self.root/'wrong').exists())
