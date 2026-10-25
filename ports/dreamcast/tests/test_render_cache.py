"""Independent binary/cache/failure oracles (AI-assisted), no game files."""
import ctypes,json,math,struct,sys,tempfile,unittest
from pathlib import Path
PORT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(PORT),str(PORT/'tests')]
import prepare_render_sectors as r
from make_render_cache_fixture import make as make_cache_fixture
from test_render_sectors import _package,_one,_interior

class CacheReview(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        dll=PORT/'build/render-cache-check.dll'
        if not dll.exists():dll=dll.with_suffix('.so')
        cls.lib=ctypes.CDLL(str(dll))
        signatures={'dc_rc_new':([ctypes.c_char_p],ctypes.c_void_p),
                    'dc_rc_error':([],ctypes.c_char_p),
                    'dc_rc_arm':([ctypes.c_uint32]*4,None),
                    'dc_rc_load':([ctypes.c_void_p,ctypes.c_char_p,ctypes.POINTER(ctypes.c_double),ctypes.c_char_p,ctypes.c_char_p],ctypes.c_char_p),
                    'dc_rc_stats':([ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint32)],None),
                    'dc_rc_copy':([ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint32],ctypes.c_uint32),
                    'dc_rc_close':([ctypes.c_void_p],None)}
        for name,(args,result) in signatures.items():
            fn=getattr(cls.lib,name);fn.argtypes=args;fn.restype=result
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.parent=Path(self.temp.name);self.contexts=[]
    def tearDown(self):
        for ctx in self.contexts:self.lib.dc_rc_close(ctx)
        words=self.stats(None);self.assertEqual(words[4],0);self.assertEqual(words[5],0);self.assertEqual(words[7],0)
        self.temp.cleanup()
    def stats(self,ctx):
        words=(ctypes.c_uint32*12)();self.lib.dc_rc_stats(ctx,words);return list(words)
    def packet(self,p,name='packet'):
        root=self.parent/name;r.publish_package(root,p);return root
    def context(self,root):
        ctx=self.lib.dc_rc_new(str(root/'manifest.drm').encode())
        self.assertTrue(ctx,self.lib.dc_rc_error());self.contexts.append(ctx);return ctx
    def load(self,ctx,root,q,af=0,of=0,cf=0,rf=0,new=None,sha=None):
        self.lib.dc_rc_arm(af,of,cf,rf)
        query=(ctypes.c_double*4)(q[0],q[2],q[1],q[3]) if q is not None else None
        error=self.lib.dc_rc_load(ctx,str(root).encode() if root is not None else None,query,
                                  str(new).encode() if new else None,sha.encode() if sha is not None else None)
        self.assertNotEqual(error,b'CACHE ROLLBACK MUTATED')
        words=self.stats(ctx);self.assertEqual(words[4],0);self.assertEqual(words[7],0)
        return error,words
    def payload(self,ctx):
        n=self.stats(ctx)[8]*116
        if not n:return b''
        output=(ctypes.c_ubyte*n)();self.assertEqual(self.lib.dc_rc_copy(ctx,output,n),n);return bytes(output)
    def test_whole_global_long_face_and_committed_air(self):
        p=_package(_one([(0,0,0),(1897,0,0),(0,0,10)]).to_bytes(),cell_size=4)
        root=self.packet(p);ctx=self.context(root)
        for q,count in (((0,8,0,8),1),((-20,-10,-20,-10),0)):
            error,words=self.load(ctx,root,q);self.assertIsNone(error);self.assertEqual(words[8:11],[count,1,1])
            expected=b''.join(f['raw'] for f in r.assemble_window(p,q));self.assertEqual(self.payload(ctx),expected)
    def test_independent_multibatch_and_both_filtered_fixtures(self):
        fixtures=self.parent/'synthetic';cases=make_cache_fixture(fixtures)['cases']
        for case in cases:
            root=fixtures/case['name'];ctx=self.context(root)
            error,words=self.load(ctx,root,case['bounds'])
            if case['expected']['result']=='REFUSE':self.assertIsNotNone(error,case['name'])
            else:
                self.assertIsNone(error,case['name']);self.assertEqual(words[8],case['expected']['count'])
                self.assertEqual(words[11],case['expected']['crc'])
                payload=self.payload(ctx)
                self.assertEqual([struct.unpack_from('<I',payload,i)[0] for i in range(0,len(payload),116)],case['expected']['source_ids'])
                if case['name']=='three-batches-one-face':self.assertGreaterEqual(words[1],6*7)
    def test_negative_closed_corner_and_exact_payload_order(self):
        positions=[(-4,0,-4)]*3+[(0,1,0)]*3+[(4,2,4)]*3
        p=_package(_one(positions).to_bytes(),cell_size=4);root=self.packet(p);ctx=self.context(root)
        for q in ((-4,-4,-4,-4),(0,0,0,0),(4,4,4,4),(-5,5,-5,5)):
            error,_=self.load(ctx,root,q);self.assertIsNone(error)
            self.assertEqual(self.payload(ctx),b''.join(f['raw'] for f in r.assemble_window(p,q)))
    def test_invalid_arguments_before_reuse_and_source_binding(self):
        p=_package(_interior().to_bytes(),cell_size=4);root=self.packet(p);ctx=self.context(root)
        self.assertIsNone(self.load(ctx,root,(0,3,0,3))[0]);before=self.payload(ctx);live=self.stats(ctx)[5]
        probes=[((0,float('nan'),0,1),root,None),((0,37,0,1),root,None),((2,1,0,1),root,None),
                ((0,1,0,1),None,None),((0,1,0,1),'',None),(None,root,None),
                ((0,1,0,1),root,'aa'),((0,1,0,1),root,'g'*64),((0,1,0,1),root,'0'*64)]
        for q,which,sha in probes:
            with self.subTest(q=q,root=which,sha=sha):
                err,s=self.load(ctx,which,q,sha=sha);self.assertIsNotNone(err)
                self.assertEqual(s[:4],[0,0,0,0]);self.assertEqual(s[5],live);self.assertEqual(self.payload(ctx),before)
    def test_reuse_keeps_larger_coverage_and_requires_selected_files_for_reload(self):
        p=_package(_interior().to_bytes(),cell_size=4);root=self.packet(p);ctx=self.context(root)
        self.assertIsNone(self.load(ctx,root,(0,3,0,3))[0]);before=self.payload(ctx)
        (root/'global.drs').rename(root/'global.kept')
        error,s=self.load(ctx,root,(1,2,1,2));self.assertIsNone(error);self.assertEqual(s[:4],[0,0,0,0])
        self.assertEqual(self.payload(ctx),before)
        error,_=self.load(ctx,root,(0,3.1,0,3));self.assertIsNotNone(error);self.assertEqual(self.payload(ctx),before)
    def test_every_reader_cache_allocation_and_io_close_open_failure_rolls_back(self):
        p=_package(_interior().to_bytes(),cell_size=4);root=self.packet(p)
        probe=self.context(root);self.assertIsNone(self.load(probe,root,(0,2,0,2))[0])
        err,counts=self.load(probe,root,(0,3,0,3));self.assertIsNone(err)
        ctx=self.context(root);self.assertIsNone(self.load(ctx,root,(0,2,0,2))[0]);before=self.payload(ctx);live=self.stats(ctx)[5]
        for kind,index in (('af',0),('of',1),('cf',2),('rf',3)):
            for n in range(1,counts[index]+1):
                with self.subTest(kind=kind,n=n):
                    error,s=self.load(ctx,root,(0,3,0,3),**{kind:n});self.assertIsNotNone(error)
                    self.assertEqual(s[5],live);self.assertEqual(self.payload(ctx),before)
        self.assertIsNone(self.load(ctx,root,(0,3,0,3))[0])
    def test_width_rounding_slack_absolute_not_coordinate_scaled(self):
        p=_package(_interior().to_bytes(),cell_size=4);root=self.packet(p);ctx=self.context(root)
        for center in (1000,999900,-999900):
            q=(center-18.4,center+18.4,0,1)
            self.assertIsNone(self.load(ctx,root,q)[0])
            self.assertIsNotNone(self.load(ctx,root,(center,center+36.802,0,1))[0])
    def test_new_catalog_identity_and_payload_commits_together(self):
        p1=_package(_one([(1,0,1),(2,0,1),(1,1,1)]).to_bytes(),cell_size=4)
        p2=_package(_one([(1,2,1),(2,2,1),(1,3,1)]).to_bytes(),cell_size=4)
        first=self.packet(p1,'a');second=self.packet(p2,'b');ctx=self.context(first)
        self.assertIsNone(self.load(ctx,first,(0,3,0,3))[0]);old=self.payload(ctx)
        self.assertIsNone(self.load(ctx,second,(0,3,0,3),new=second/'manifest.drm')[0])
        self.assertNotEqual(self.payload(ctx),old)
        self.assertEqual(self.payload(ctx),b''.join(f['raw'] for f in r.assemble_window(p2,(0,3,0,3))))

if __name__=='__main__':unittest.main()
