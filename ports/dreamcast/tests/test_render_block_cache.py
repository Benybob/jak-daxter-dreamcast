"""Independent whole-block residency/geometry/failure oracles (AI-assisted), ISC."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import sys
PORT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(PORT),str(PORT/'tests')]
import prepare_render_sectors as r
import indexed_sectors as ix
from test_render_sectors import _one,_package
from make_render_cache_fixture import make as make_cases


class BlockCacheTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path=PORT/'build/render-block-cache-check.dll'
        if not path.exists():path=path.with_suffix('.so')
        cls.lib=ctypes.CDLL(str(path))
        for name,args,ret in [
            ('dc_rbc_arm',[ctypes.c_uint32]*3,None),('dc_rbc_new',[ctypes.c_char_p,ctypes.c_uint32],ctypes.c_void_p),
            ('dc_rbc_new_indexed',[ctypes.c_char_p,ctypes.c_uint32],ctypes.c_void_p),
            ('dc_rbc_close',[ctypes.c_void_p],None),('dc_rbc_error',[],ctypes.c_char_p),
            ('dc_rbc_window',[ctypes.c_void_p,ctypes.POINTER(ctypes.c_double)],ctypes.c_char_p),
            ('dc_rbc_stats',[ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint32)],None),
            ('dc_rbc_copy',[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint32],ctypes.c_uint32),
            ('dc_rbc_guards',[ctypes.c_void_p],ctypes.c_int)]:
            f=getattr(cls.lib,name);f.argtypes=args;f.restype=ret
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);self.owners=[]
        self.lib.dc_rbc_arm(0,0,0)
    def tearDown(self):
        for owner in self.owners:self.lib.dc_rbc_close(owner)
        self.assertEqual(self.stats(None)[13:15],[0,0]);self.temp.cleanup()
    def stats(self,c):
        words=(ctypes.c_uint32*20)();self.lib.dc_rbc_stats(c,words);return list(words)
    def packet(self,xyz,name='packet'):
        p=_package(_one(xyz).to_bytes(),cell_size=16);root=self.root/name;r.publish_package(root,p);return p,root
    make='dc_rbc_new'
    def owner(self,root,budget=2*1024*1024):
        c=getattr(self.lib,self.make)(str(root).encode(),budget);self.assertTrue(c,self.lib.dc_rbc_error());self.owners.append(c);return c
    def load(self,c,q):
        args=(ctypes.c_double*4)(q[0],q[2],q[1],q[3]);error=self.lib.dc_rbc_window(c,args)
        self.assertNotEqual(error,b'BLOCK SOURCE ROLLBACK MUTATED');self.assertEqual(self.stats(c)[14],0);return error
    def payload(self,c):
        n=self.stats(c)[15]*116;out=(ctypes.c_ubyte*n)();self.assertEqual(self.lib.dc_rbc_copy(c,out,n),n);return bytes(out)
    def test_two_passes_and_adjacent_window_reuse_exact_blocks(self):
        p,root=self.packet([(0.2,0,.2),(1,0,.2),(.2,1,1)]);c=self.owner(root)
        for q in ((0,2,0,2),(-1,3,-1,3)):
            self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),b''.join(f['raw'] for f in r.assemble_window(p,q)))
        s=self.stats(c);self.assertEqual(s[2],2);self.assertGreaterEqual(s[0],6);self.assertEqual(s[3],0);self.assertEqual(s[6],116)
    def test_trailing_separator_matches_the_original_root_contract(self):
        p,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)])
        for suffix in ('/','\\') if sys.platform=='win32' else ('/',):
            with self.subTest(suffix=suffix):
                c=self.owner(str(root)+suffix);self.assertIsNone(self.load(c,(0,2,0,2)))
                self.assertEqual(self.payload(c),b''.join(f['raw'] for f in r.assemble_window(p,(0,2,0,2))))
    def test_bypass_and_eviction_preserve_source_payload(self):
        xyz=[(0,0,0),(1000,0,0),(0,1,1),(.2,0,.2),(1,0,.2),(.2,1,1)]
        p,root=self.packet(xyz)
        for budget in (1,116,232):
            c=self.owner(root,budget)
            for q in ((0,2,0,2),(-1,3,-1,3)):
                self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),b''.join(f['raw'] for f in r.assemble_window(p,q)))
            s=self.stats(c);self.assertLessEqual(s[6],budget);self.assertLessEqual(s[7],budget)
            if budget==1:self.assertGreater(s[4],0)
            if budget==116:self.assertGreater(s[3],0)
    def test_slot_eviction_is_bounded_across_a_larger_map(self):
        xyz=[p for i in range(140) for p in ((i*16+.2,0,.2),(i*16+1,0,.2),(i*16+.2,1,1))]
        packet,root=self.packet(xyz);c=self.owner(root)
        for i in (*range(140),0):
            q=(i*16+.1,i*16+1.1,.1,1.1);self.assertIsNone(self.load(c,q))
            self.assertEqual(self.payload(c),b''.join(f['raw'] for f in r.assemble_window(packet,q)))
            self.assertLessEqual(self.stats(c)[8],128)
        self.assertGreater(self.stats(c)[3],0)
    def test_all_replica_role_filter_multibatch_cases_still_apply(self):
        root=self.root/'cases';cases=make_cases(root)['cases']
        for case in cases:
            with self.subTest(name=case['name']):
                c=self.owner(root/case['name']);error=self.load(c,case['bounds'])
                if case['expected']['result']=='REFUSE':self.assertIsNotNone(error)
                else:
                    self.assertIsNone(error);self.assertEqual(len(self.payload(c)),case['expected']['count']*116)
                    self.assertEqual(r.crc32(self.payload(c)),case['expected']['crc'])
    def test_metadata_allocation_and_budget_rejection_leave_no_owner(self):
        _,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)])
        self.lib.dc_rbc_arm(1,0,0);self.assertFalse(self.lib.dc_rbc_new(str(root).encode(),1024));self.assertEqual(self.stats(None)[13],0)
        self.lib.dc_rbc_arm(0,0,0)
        for budget in (0,2*1024*1024+1):self.assertFalse(self.lib.dc_rbc_new(str(root).encode(),budget))
    def test_every_cold_open_close_failure_preserves_window_and_no_cached_failure(self):
        p,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)])
        for kind in (1,2):
            for n in (1,2):
                c=self.owner(root);self.lib.dc_rbc_arm(0,n if kind==1 else 0,n if kind==2 else 0)
                self.assertIsNotNone(self.load(c,(0,2,0,2)));self.assertEqual(self.payload(c),b'')
                self.lib.dc_rbc_arm(0,0,0);self.assertIsNone(self.load(c,(0,2,0,2)))
                self.assertEqual(self.payload(c),b''.join(f['raw'] for f in r.assemble_window(p,(0,2,0,2))))
    def test_missing_and_trailing_files_are_refused_without_empty_commit(self):
        for trailing in (False,True):
            _,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)],str(trailing));file=next((root/'sectors').glob('*.drs'))
            if trailing:file.write_bytes(file.read_bytes()+b'X')
            else:file.unlink()
            c=self.owner(root);self.assertIsNotNone(self.load(c,(0,2,0,2)));self.assertEqual(self.payload(c),b'')
            self.assertEqual(self.stats(c)[5],1)
    def test_catalog_path_and_nested_borrow_guards(self):
        _,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)]);c=self.owner(root)
        self.assertEqual(self.lib.dc_rbc_guards(c),1);self.assertIsNone(self.load(c,(0,2,0,2)))


class IndexedBlockCacheTest(BlockCacheTest):
    """Every test above against the DRX1 owner, plus its own accounting."""
    make='dc_rbc_new_indexed'
    def drx1(self,root):
        """Each published block's DRX1 size and face payload, by file name."""
        out={}
        for f in [root/'global.drs',*(root/'sectors').glob('*.drs')]:
            raw=[x['raw'] for x in r.parse_sector(f.read_bytes())['records']]
            out[f.name]=(len(ix.encode_sector(raw)),len(raw)*116)
        return out
    def grid(self,n=6,name='grid'):
        """An n x n grid of quads in one cell: most vertices shared by six faces."""
        xyz=[]
        for i in range(n):
            for j in range(n):
                a,b,c,d=((i+.5,0,j+.5),(i+1.5,0,j+.5),(i+1.5,0,j+1.5),(i+.5,0,j+1.5))
                xyz+= [a,b,c,a,c,d]
        return self.packet(xyz,name)
    def test_two_passes_and_adjacent_window_reuse_exact_blocks(self):
        p,root=self.packet([(0.2,0,.2),(1,0,.2),(.2,1,1)]);c=self.owner(root);sizes=self.drx1(root)
        for q in ((0,2,0,2),(-1,3,-1,3)):
            self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),b''.join(f['raw'] for f in r.assemble_window(p,q)))
        s=self.stats(c);self.assertEqual(s[2],2);self.assertGreaterEqual(s[0],6);self.assertEqual(s[3],0)
        self.assertEqual(s[6],sum(v[0] for v in sizes.values()));self.assertEqual(s[16],116)
        self.assertEqual(s[17],s[0])
    def test_bypass_and_eviction_preserve_source_payload(self):
        xyz=[(0,0,0),(1000,0,0),(0,1,1),(.2,0,.2),(1,0,.2),(.2,1,1)]
        p,root=self.packet(xyz);sizes=sorted(v[0] for v in self.drx1(root).values())
        for budget in (1,sizes[-1],sum(sizes)-1,sum(sizes)):
            c=self.owner(root,budget)
            for q in ((0,2,0,2),(-1,3,-1,3)):
                self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),b''.join(f['raw'] for f in r.assemble_window(p,q)))
            s=self.stats(c);self.assertLessEqual(s[6],budget);self.assertLessEqual(s[7],budget)
            if budget<sizes[0]:self.assertEqual(s[8],0);self.assertGreater(s[4],0)
            if sizes[-1]<=budget<sum(sizes):self.assertGreater(s[3],0)
            if budget==sum(sizes):self.assertEqual(s[3],0);self.assertEqual(s[4],0)
    def test_shared_vertices_retain_less_than_faces(self):
        p,root=self.grid();c=self.owner(root);q=(0,8,0,8)
        for _ in range(2):
            self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),b''.join(f['raw'] for f in r.assemble_window(p,q)))
        s=self.stats(c);sizes=self.drx1(root)
        self.assertEqual((s[6],s[16]),(sum(v[0] for v in sizes.values()),sum(v[1] for v in sizes.values())))
        self.assertLess(s[6]*2,s[16])
        raw=self.owner(root);self.assertIsNone(self.load(raw,q))
    def test_a_budget_between_indexed_and_raw_sizes_still_hits(self):
        p,root=self.grid(name='between');sizes=self.drx1(root);q=(0,8,0,8)
        budget=sum(v[0] for v in sizes.values())
        self.assertLess(budget,sum(v[1] for v in sizes.values()))
        misses={}
        for make in ('dc_rbc_new','dc_rbc_new_indexed'):
            c=getattr(self.lib,make)(str(root).encode(),budget);self.owners.append(c)
            for _ in range(2):self.assertIsNone(self.load(c,q))
            self.assertEqual(self.payload(c),b''.join(f['raw'] for f in r.assemble_window(p,q)))
            misses[make]=self.stats(c)[1]
        # indexed: each block read once; raw: the non-empty ones read on both passes
        self.assertEqual(misses['dc_rbc_new_indexed'],len(sizes))
        self.assertEqual(misses['dc_rbc_new'],len(sizes)+sum(1 for v in sizes.values() if v[1]))
    def test_every_allocation_failure_bypasses_or_rolls_back(self):
        p,root=self.grid(3,'faults');q=(0,8,0,8);want=b''.join(f['raw'] for f in r.assemble_window(p,q))
        seen=set()
        for warm in (False,True):
            for n in range(1,12):
                with self.subTest(warm=warm,n=n):
                    c=self.owner(root)
                    if warm:self.assertIsNone(self.load(c,q));self.load(c,(20,21,20,21))
                    before=self.payload(c);self.lib.dc_rbc_arm(n,0,0);error=self.load(c,q)
                    self.assertEqual(self.payload(c),before if error else want)
                    seen.add('rollback' if error else 'bypass' if self.stats(c)[4] else 'clean')
                    self.lib.dc_rbc_arm(0,0,0);self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),want)
                    s=self.stats(c);self.assertLessEqual(s[6],2*1024*1024)
        self.assertEqual(seen,{'rollback','bypass','clean'})
