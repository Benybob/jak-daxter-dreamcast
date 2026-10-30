"""Independent whole-block residency/geometry/failure oracles (AI-assisted), ISC."""
import ctypes
from pathlib import Path
import tempfile
import unittest
import sys
PORT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(PORT),str(PORT/'tests')]
import indexed_sectors as indexed
import prepare_render_sectors as r
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
            ('dc_rbc_new_draw',[ctypes.c_char_p,ctypes.c_uint32],ctypes.c_void_p),
            ('dc_rbc_close',[ctypes.c_void_p],None),('dc_rbc_error',[],ctypes.c_char_p),
            ('dc_rbc_window',[ctypes.c_void_p,ctypes.POINTER(ctypes.c_double)],ctypes.c_char_p),
            ('dc_rbc_stats',[ctypes.c_void_p,ctypes.POINTER(ctypes.c_uint32)],None),
            ('dc_rbc_copy',[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint32],ctypes.c_uint32),
            ('dc_rbc_dw_make',[ctypes.c_void_p]+[ctypes.c_uint32]*3,ctypes.c_char_p),
            ('dc_rbc_dw_build',[ctypes.c_void_p,ctypes.POINTER(ctypes.c_double)],ctypes.c_char_p),
            ('dc_rbc_dw_count',[ctypes.c_void_p],ctypes.c_uint32),
            ('dc_rbc_dw_prepare',[ctypes.c_void_p,ctypes.c_int],ctypes.c_uint32),
            ('dc_rbc_dw_prepared',[ctypes.c_void_p],ctypes.c_uint32),
            ('dc_rbc_dw_vertices',[ctypes.c_void_p],ctypes.c_uint32),
            ('dc_rbc_dw_copy',[ctypes.c_void_p,ctypes.c_void_p],None),
            ('dc_rbc_guards',[ctypes.c_void_p],ctypes.c_int)]:
            f=getattr(cls.lib,name);f.argtypes=args;f.restype=ret
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);self.owners=[]
        self.lib.dc_rbc_arm(0,0,0)
    def tearDown(self):
        for owner in self.owners:self.lib.dc_rbc_close(owner)
        self.assertEqual(self.stats(None)[13:15],[0,0]);self.temp.cleanup()
    def stats(self,c):
        words=(ctypes.c_uint32*16)();self.lib.dc_rbc_stats(c,words);return list(words)
    def packet(self,xyz,name='packet'):
        p=_package(_one(xyz).to_bytes(),cell_size=16);root=self.root/name;r.publish_package(root,p);return p,root
    def owner(self,root,budget=2*1024*1024):
        c=self.lib.dc_rbc_new(str(root).encode(),budget);self.assertTrue(c,self.lib.dc_rbc_error());self.owners.append(c);return c
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
    def draw_owner(self,root,budget=2*1024*1024):
        c=self.lib.dc_rbc_new_draw(str(root).encode(),budget);self.assertTrue(c,self.lib.dc_rbc_error());self.owners.append(c);return c
    def stripped(self,p,q):
        return b''.join(indexed.strip_normals(f['raw']) for f in r.assemble_window(p,q))
    def test_draw_blocks_lend_zero_normals_on_misses_and_hits(self):
        p,root=self.packet([(0.2,0,.2),(1,0,.2),(.2,1,1),(30,0,.2),(31,0,.2),(30,1,1)]);c=self.draw_owner(root)
        for q in ((0,2,0,2),(-1,3,-1,3),(0,33,-1,3)):
            self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),self.stripped(p,q))
        s=self.stats(c);self.assertGreater(s[0],0);self.assertEqual(s[3],0);self.assertEqual(s[5],0)
        self.assertEqual(s[7],s[6]);self.assertEqual(s[4],0)
    def test_draw_select_pass_expands_nothing_and_counts_as_plain(self):
        xyz=[(0.2,0,.2),(1,0,.2),(.2,1,1),(30,0,.2),(31,0,.2),(30,1,1)]
        p,root=self.packet(xyz);plain,draw=self.owner(root),self.draw_owner(root)
        for c in (plain,draw):
            for q in ((0,2,0,2),(-1,3,-1,3),(20,33,-1,3),(0,2,0,2)):self.assertIsNone(self.load(c,q))
        self.assertEqual(self.stats(plain)[:4],self.stats(draw)[:4])
        self.assertEqual(self.payload(draw),self.stripped(p,(0,2,0,2)))
        self.lib.dc_rbc_arm(0,0,0);self.assertIsNone(self.load(plain,(-1,3,-1,3)));before=self.stats(plain)[10]
        self.lib.dc_rbc_arm(0,0,0);self.assertIsNone(self.load(draw,(-1,3,-1,3)))
        blocks=self.stats(draw)[0]-self.stats(plain)[0]
        self.assertEqual(blocks,0);self.assertLessEqual(self.stats(draw)[10],before)
    def test_draw_files_replace_the_sectors(self):
        xyz=[(0.2,0,.2),(1,0,.2),(.2,1,1),(30,0,.2),(31,0,.2),(30,1,1)]
        p,root=self.packet(xyz);indexed.write_draw_files(root,remove_sectors=True)
        self.assertEqual(list(root.rglob('*.drs')),[])
        c=self.draw_owner(root)
        for q in ((0,2,0,2),(-1,3,-1,3),(0,33,-1,3)):
            self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),self.stripped(p,q))
        s=self.stats(c);self.assertGreater(s[0],0);self.assertEqual(s[5],0);self.assertEqual(s[4],0)
        self.assertEqual(s[6],sum(f.stat().st_size for f in root.rglob('*.drx')))
    def test_draw_files_count_as_the_sectors_do(self):
        xyz=[(0.2,0,.2),(1,0,.2),(.2,1,1),(30,0,.2),(31,0,.2),(30,1,1)]
        _,encoded=self.packet(xyz,'encoded');p,files=self.packet(xyz,'files');indexed.write_draw_files(files)
        a,b=self.draw_owner(encoded),self.draw_owner(files)
        for c in (a,b):
            for q in ((0,2,0,2),(-1,3,-1,3),(0,33,-1,3),(0,2,0,2)):self.assertIsNone(self.load(c,q))
        self.assertEqual(self.stats(a)[:4],self.stats(b)[:4]);self.assertEqual(self.stats(a)[6],self.stats(b)[6])
    def test_a_bad_draw_file_is_refused_not_trusted(self):
        for name,spoil in (('truncated',lambda b:b[:-1]),('flipped',lambda b:b[:20]+bytes([b[20]^1])+b[21:]),
                           ('empty',lambda b:b''),('other block',None)):
            with self.subTest(name=name):
                p,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1),(30,0,.2),(31,0,.2),(30,1,1)],name.replace(' ','-'))
                indexed.write_draw_files(root,remove_sectors=True);one=root/'sectors/0_0.drx'
                one.write_bytes(spoil(one.read_bytes()) if spoil else (root/'global.drx').read_bytes())
                c=self.draw_owner(root);self.assertIsNotNone(self.load(c,(0,2,0,2)));self.assertEqual(self.stats(c)[15],0)
    def test_a_bad_draw_file_falls_back_to_its_sector(self):
        p,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)]);indexed.write_draw_files(root)
        (root/'sectors/0_0.drx').write_bytes(b'junk');c=self.draw_owner(root);q=(0,2,0,2)
        self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),self.stripped(p,q))
    def test_a_file_above_the_budget_falls_back(self):
        p,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)]);indexed.write_draw_files(root);c=self.draw_owner(root,16);q=(0,2,0,2)
        self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),self.stripped(p,q));self.assertLessEqual(self.stats(c)[6],16)
    def test_every_draw_file_allocation_and_io_failure_is_clean(self):
        p,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)]);indexed.write_draw_files(root);q=(0,2,0,2)
        for faults in [(a,0,0) for a in range(1,12)]+[(0,o,0) for o in range(1,6)]+[(0,0,k) for k in range(1,6)]:
            with self.subTest(faults=faults):
                c=self.draw_owner(root);self.lib.dc_rbc_arm(*faults);self.load(c,q);self.lib.dc_rbc_arm(0,0,0)
                self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),self.stripped(p,q))
                self.lib.dc_rbc_close(c);self.owners.remove(c);self.assertEqual(self.stats(None)[13:15],[0,0])
    def drawn(self,c,q):
        args=(ctypes.c_double*4)(q[0],q[2],q[1],q[3]);error=self.lib.dc_rbc_dw_build(c,args);self.assertEqual(self.stats(c)[14],0)
        return error
    def drawn_payload(self,c):
        n=self.lib.dc_rbc_dw_count(c);out=(ctypes.c_ubyte*(n*116+1))();self.lib.dc_rbc_dw_copy(c,out);return bytes(out)[:n*116]
    def window_owner(self,root,budget=2*1024*1024,caps=(8192,4096,8192)):
        c=self.draw_owner(root,budget);self.assertIsNone(self.lib.dc_rbc_dw_make(c,*caps));return c
    def spread(self):
        xyz=[]
        for x in range(0,64,4):
            for z in (0,5,30):xyz+=[(x+.2,0,z+.2),(x+1,0,z+.2),(x+.2,1,z+1)]
        xyz+=[(14,0,1),(18,0,1),(18,1,18)]  # crosses cells: replicated
        p,root=self.packet(xyz);indexed.write_draw_files(root,remove_sectors=True);return p,root
    def test_indexed_windows_match_the_assembled_faces(self):
        p,root=self.spread();c=self.window_owner(root)
        for q in ((0,2,0,2),(12,20,0,20),(0,36,0,36),(30,60,0,6),(100,101,100,101)):
            with self.subTest(q=q):
                self.assertIsNone(self.drawn(c,q));self.assertEqual(self.drawn_payload(c),self.stripped(p,q))
    def test_indexed_windows_share_vertices(self):
        tris=[(.2,0,.2),(1,0,.2),(1,1,.2),(.2,0,.2),(1,1,.2),(.2,1,.2)]
        p,root=self.packet(tris);indexed.write_draw_files(root,remove_sectors=True);c=self.window_owner(root);q=(0,2,0,2)
        self.assertIsNone(self.drawn(c,q));self.assertEqual(self.lib.dc_rbc_dw_count(c),2)
        self.assertLessEqual(self.lib.dc_rbc_dw_vertices(c),6);self.assertEqual(self.drawn_payload(c),self.stripped(p,q))
    def test_a_covered_query_keeps_the_window(self):
        p,root=self.spread();c=self.window_owner(root);self.assertIsNone(self.drawn(c,(0,32,0,32)));hits=self.stats(c)[0]
        self.assertIsNone(self.drawn(c,(1,2,1,2)));self.assertEqual(self.stats(c)[0],hits)
        self.assertEqual(self.drawn_payload(c),self.stripped(p,(0,32,0,32)))
    def test_every_capacity_failure_leaves_the_window(self):
        p,root=self.spread();q=(0,36,0,36)
        for caps,reason in (((2,4096,8192),b'render cache face cap'),((8192,3,8192),b'draw window vertex capacity'),((8192,4096,2),b'draw window remap capacity')):
            with self.subTest(reason=reason):
                c=self.window_owner(root,caps=caps);error=self.drawn(c,q);self.assertIsNotNone(error);self.assertIn(reason.split()[-1],error);self.assertEqual(self.lib.dc_rbc_dw_count(c),0)
    def test_a_failed_build_keeps_the_last_window(self):
        p,root=self.spread();c=self.window_owner(root);a=(0,2,0,2);self.assertIsNone(self.drawn(c,a));before=self.drawn_payload(c)
        for f in root.rglob('*.drx'):
            if f.name!='global.drx' and f.name!='0_0.drx':f.write_bytes(b'junk')
        self.assertIsNotNone(self.drawn(c,(20,36,0,36)));self.assertEqual(self.drawn_payload(c),before);self.assertEqual(self.stats(c)[5],0)
    def test_a_budget_below_the_window_is_refused_not_evicted_mid_build(self):
        p,root=self.spread();c=self.window_owner(root,300);error=self.drawn(c,(0,36,0,36))
        self.assertEqual(error,b'block cache window residency');self.assertLessEqual(self.stats(c)[6],300)
    def test_mismatched_replicas_are_refused(self):
        p,root=self.spread()
        crossing=[f for f in sorted((root/'sectors').glob('*.drx'))]
        from zlib import crc32
        import struct
        done=False
        ids={}
        for f in crossing:
            blob=f.read_bytes();nv,nf=struct.unpack_from('<II',blob,4)
            for i in range(nf):
                sid=struct.unpack_from('<I',blob,12+nv*24+i*16)[0]
                ids.setdefault(sid,[]).append((f,i,nv))
        f,i,nv=next(v for v in ids.values() if len(v)>1)[0]
        raw=bytearray(f.read_bytes());a=struct.unpack_from('<H',raw,12+nv*24+i*16+8)[0]
        raw[12+a*24+20]^=1;struct.pack_into('<I',raw,len(raw)-4,crc32(raw[:-4]));f.write_bytes(raw)
        c=self.window_owner(root);self.assertIn(b'replica',self.drawn(c,(0,36,0,36)))
    def test_a_window_is_prepared_only_for_the_current_camera_and_build(self):
        p,root=self.spread();c=self.window_owner(root);self.assertEqual(self.lib.dc_rbc_dw_prepare(c,1),0)
        self.assertIsNone(self.drawn(c,(0,2,0,2)));self.assertEqual(self.lib.dc_rbc_dw_prepared(c),0)
        self.assertEqual(self.lib.dc_rbc_dw_prepare(c,1),(1<<31)|1)
        self.assertEqual(self.lib.dc_rbc_dw_prepare(c,0),0);self.assertEqual(self.lib.dc_rbc_dw_prepared(c),0)
        self.assertEqual(self.lib.dc_rbc_dw_prepare(c,1),(1<<31)|1)
        self.assertIsNone(self.drawn(c,(20,28,0,8)));self.assertEqual(self.lib.dc_rbc_dw_prepared(c),0)
    def test_a_failed_build_keeps_the_window_prepared(self):
        p,root=self.spread();c=self.window_owner(root);self.assertIsNone(self.drawn(c,(0,2,0,2)));self.lib.dc_rbc_dw_prepare(c,1)
        for f in root.rglob('*.drx'):
            if f.name not in ('global.drx','0_0.drx'):f.write_bytes(b'junk')
        self.assertIsNotNone(self.drawn(c,(20,36,0,36)));self.assertEqual(self.lib.dc_rbc_dw_prepared(c),1)
    def test_draw_files_are_held_to_the_sector_bounds(self):
        import struct
        from zlib import crc32
        for offset,value in ((0,1000001.0),(4,-1000001.0),(8,1000001.0),(12,65537.0),(16,-65537.0)):
            with self.subTest(offset=offset):
                p,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)],f'bounds{offset}');indexed.write_draw_files(root,remove_sectors=True)
                f=root/'sectors/0_0.drx';raw=bytearray(f.read_bytes());struct.pack_into('<f',raw,12+offset,value)
                struct.pack_into('<I',raw,len(raw)-4,crc32(raw[:-4]));f.write_bytes(raw)
                c=self.window_owner(root);self.assertEqual(self.drawn(c,(0,2,0,2)),b'block cache window residency')
                self.assertIsNotNone(self.load(self.draw_owner(root),(0,2,0,2)))
    def test_every_window_allocation_and_io_failure_is_clean(self):
        p,root=self.spread();q=(0,20,0,20);want=self.stripped(p,q)
        for faults in [(a,0,0) for a in range(1,20)]+[(0,o,0) for o in range(1,10)]+[(0,0,k) for k in range(1,10)]:
            with self.subTest(faults=faults):
                c=self.window_owner(root);self.lib.dc_rbc_arm(*faults);self.drawn(c,q);self.lib.dc_rbc_arm(0,0,0)
                self.assertIsNone(self.drawn(c,q));self.assertEqual(self.drawn_payload(c),want)
                self.lib.dc_rbc_close(c);self.owners.remove(c);self.assertEqual(self.stats(None)[13:15],[0,0])
    def test_draw_retention_counts_drx2_bytes_exactly(self):
        p,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)]);c=self.draw_owner(root)
        self.assertIsNone(self.load(c,(0,2,0,2)))
        blocks=[v for v in list(p.sectors.values())+[p.global_sector]]
        self.assertEqual(self.stats(c)[6],sum(len(indexed.encode_draw_sector(f['raw'] for f in r.parse_sector(b)['records'])) for b in blocks))
    def test_draw_keeps_what_a_drm2_budget_would_bypass(self):
        fan=[(.2,0,.2),(1,0,.2),(1,1,.2),(.2,1,.2),(.2,0,1),(1,0,1),(1,1,1),(.2,1,1)]
        tris=[fan[i] for t in ((0,1,2),(0,2,3),(4,5,6),(4,6,7),(0,1,5),(0,5,4),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,0,4),(3,4,7)) for i in t]
        p,root=self.packet(tris);raw=sum(len(f) for f in [b''.join(x['raw'] for x in r.parse_sector(p.sectors[(0,0)])['records'])])
        budget=raw//2;self.assertGreater(raw,budget)
        q=(0,2,0,2);plain,draw=self.owner(root,budget),self.draw_owner(root,budget)
        for c in (plain,draw):
            for _ in range(3):self.assertIsNone(self.load(c,q))
        self.assertGreater(self.stats(draw)[0],self.stats(plain)[0]);self.assertEqual(self.payload(draw),self.stripped(p,q))
    def test_draw_eviction_stays_inside_the_budget(self):
        xyz=[v for x in range(0,160,16) for v in ((x+.2,0,.2),(x+1,0,.2),(x+.2,1,1))]
        p,root=self.packet(xyz);c=self.draw_owner(root,300)
        for x in range(0,160,16):
            q=(x,x+2,0,2);self.assertIsNone(self.load(c,q));self.assertEqual(self.payload(c),self.stripped(p,q))
            self.assertLessEqual(self.stats(c)[6],300)
        self.assertGreater(self.stats(c)[3],0);self.assertLessEqual(self.stats(c)[7],300)
    def test_every_draw_allocation_failure_is_clean(self):
        p,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)]);q=(0,2,0,2);full=self.stripped(p,q)
        for at in range(1,40):
            with self.subTest(at=at):
                c=self.draw_owner(root);self.assertIsNone(self.load(c,q));self.lib.dc_rbc_arm(at,0,0)
                error=self.load(c,(-1,3,-1,3));self.lib.dc_rbc_arm(0,0,0)
                self.assertEqual(self.payload(c),full)
                self.assertIsNone(self.load(c,(-1,3,-1,3)));self.assertEqual(self.payload(c),full)
                self.lib.dc_rbc_close(c);self.owners.remove(c);self.assertEqual(self.stats(None)[13:15],[0,0])
    def test_draw_guards_match_the_plain_owner(self):
        _,root=self.packet([(.2,0,.2),(1,0,.2),(.2,1,1)]);self.assertEqual(self.lib.dc_rbc_guards(self.draw_owner(root)),1)
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
