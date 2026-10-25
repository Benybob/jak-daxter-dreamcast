"""Independent native-C cache contracts through ctypes (AI-assisted), ISC."""
from pathlib import Path
import ctypes as c
import math, os, struct, sys, tempfile, unittest, zlib, subprocess
PORT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PORT))
import partition_collision as pc
class Vec(c.Structure): _fields_=[('x',c.c_float),('y',c.c_float),('z',c.c_float)]
class Tri(c.Structure): _fields_=[('a',Vec),('b',Vec),('c',Vec),('pat',c.c_uint32)]
class World(c.Structure):
    _fields_=[('triangles',c.c_void_p),('count',c.c_uint32),('candidates',c.c_uint16*1024)]+[(n,c.c_uint32) for n in ('candidate_count','distance_tests','failure','failed_triangle','ready')]
class Bounds(c.Structure): _fields_=[(n,c.c_float) for n in ('min_x','max_x','min_z','max_z')]
class Entry(c.Structure): _fields_=[('ix',c.c_int32),('iz',c.c_int32)]+[(n,c.c_uint32) for n in ('count','crc','bytes')]
class Index(c.Structure):
    _fields_=[(n,c.c_uint32) for n in ('source_count','source_crc','package_crc','count')]+[('cell_size',c.c_float)]+[(n,c.c_int32) for n in ('ix_min','ix_max','iz_min','iz_max')]+[('halo',c.c_uint32),('entries',c.POINTER(Entry))]
class Cache(c.Structure): _fields_=[('world',World),('origin',Vec),('coverage',Bounds),('valid',c.c_uint32),('package_crc',c.c_uint32)]
DLL=PORT/'build'/('sector-cache-check.dll' if sys.platform=='win32' else 'sector-cache-check.so')
lib=c.CDLL(str(DLL))
for name,args,ret in (
 ('dc_test_index',[c.c_char_p,c.POINTER(Index)],c.c_char_p),
 ('dc_sector_index_free',[c.POINTER(Index)],None),
 ('dc_sector_cache_free',[c.POINTER(Cache)],None),
 ('dc_sector_retarget',[c.POINTER(Index),c.c_char_p,Bounds,Vec,c.POINTER(Cache)],c.c_char_p),
 ('dc_sector_covers',[c.POINTER(Cache),Bounds],c.c_int),
 ('dc_test_reset',[c.c_uint],None),('dc_test_io_reset',[c.c_uint,c.c_uint],None),
 ('dc_test_calls',[],c.c_uint),('dc_test_live',[],c.c_uint),('dc_test_open_files',[],c.c_uint),
 ('dc_test_bytes',[],c.c_size_t),('dc_test_peak',[],c.c_size_t),
 ('dc_test_row',[c.POINTER(Cache),c.c_uint,c.POINTER(Tri)],c.c_int)):
    f=getattr(lib,name);f.argtypes=args;f.restype=ret
def row(x=1,z=1,pat=0):return struct.pack('<9fI',x,0,z,x,0,z+1,x+1,0,z,pat)
def resign(b):return bytes(b[:-4])+struct.pack('<I',zlib.crc32(b[:-4]))
def package(groups,cell=16,source_count=None):
    """Literal serializer, independent of the production converter."""
    sectors={};entries=[]
    for (ix,iz),rows in sorted(groups.items()):
        b=struct.pack('<4sIiiII',b'DCSS',1,ix,iz,len(rows),44*len(rows))+b''.join(struct.pack('<I',i)+r for i,r in rows)
        crc=zlib.crc32(b);b+=struct.pack('<I',crc);sectors[ix,iz]=b;entries.append((ix,iz,len(rows),crc,len(b)))
    h=bytearray(108);h[:4]=b'DCS1'
    values={4:1,8:43,12:1,16:source_count or max(i for rows in groups.values() for i,_ in rows)+1,
            24:len(entries),28:sum(e[2] for e in entries),32:max(e[2] for e in entries),36:2048,40:36,44:4096,48:262144,
            68:math.floor(22.4/cell)+1,72:123,92:8192,96:1024,100:76,104:40}
    for off,v in values.items():struct.pack_into('<I',h,off,v)
    for off,v in zip((52,56,60,64),(min(e[0] for e in entries),max(e[0] for e in entries),min(e[1] for e in entries),max(e[1] for e in entries))):struct.pack_into('<i',h,off,v)
    for off,v in ((20,cell),(76,64),(80,10.4),(84,8),(88,2)):struct.pack_into('<f',h,off,v)
    b=bytes(h)+b''.join(struct.pack('<iiIII',*e) for e in entries);return b+struct.pack('<I',zlib.crc32(b)),sectors
class CacheContracts(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.index=Index();self.cache=Cache()
        lib.dc_test_reset(0);lib.dc_test_io_reset(0,0)
    def tearDown(self):
        lib.dc_sector_cache_free(c.byref(self.cache));lib.dc_sector_index_free(c.byref(self.index))
        self.assertEqual(lib.dc_test_live(),0);self.assertEqual(lib.dc_test_bytes(),0);self.assertEqual(lib.dc_test_open_files(),0);self.tmp.cleanup()
    def install(self,data=None):
        manifest,sectors=data or package({(0,0):[(0,row())]})
        (self.root/'sectors').mkdir(exist_ok=True)
        for (ix,iz),b in sectors.items():(self.root/f'sectors/{ix}_{iz}.dcs').write_bytes(b)
        (self.root/'manifest.dcs').write_bytes(manifest)
        return lib.dc_test_index(os.fsencode(self.root/'manifest.dcs'),c.byref(self.index))
    def target(self,query=None,origin=None,root=None):
        return lib.dc_sector_retarget(c.byref(self.index),os.fsencode(root or self.root),query or Bounds(0,4,0,4),origin or Vec(),c.byref(self.cache))
    def rows(self):
        result=[]
        for i in range(self.cache.world.count):
            t=Tri();self.assertTrue(lib.dc_test_row(c.byref(self.cache),i,c.byref(t)));result.append(bytes(t))
        return result
    def expect_rollback(self,call):
        prior=bytes(self.cache);live=lib.dc_test_bytes();error=call();self.assertIsNotNone(error)
        self.assertEqual(bytes(self.cache),prior);self.assertEqual(lib.dc_test_bytes(),live);self.assertEqual(lib.dc_test_open_files(),0)
        return error
    def test_roundtrip_literal_and_production_package(self):
        r=row(-20,-40,8|(5<<14));data=struct.pack('<4sIII',b'DCR1',1,43,1)+r
        p=pc.build_package(data);self.assertIsNone(self.install((p.manifest,p.sectors)))
        self.assertIsNone(self.target(Bounds(-23,-18,-43,-38),Vec(-24,0,-40)))
        floats=struct.unpack('<9fI',r);expected=struct.pack('<9fI',*[v-(-24 if i%3==0 else -40 if i%3==2 else 0) for i,v in enumerate(floats[:9])],floats[9])
        self.assertEqual(self.rows(),[expected]);self.assertEqual(self.index.source_count,1)
    def test_manifest_every_truncation_and_eof(self):
        manifest,_=package({(0,0):[(0,row())]})
        for b in [manifest[:i] for i in range(len(manifest))]+[manifest+b'x']:
            (self.root/'manifest.dcs').write_bytes(b)
            self.assertIsNotNone(lib.dc_test_index(os.fsencode(self.root/'manifest.dcs'),c.byref(self.index)))
            self.assertEqual(bytes(self.index),bytes(Index()));self.assertEqual(lib.dc_test_live(),0)
    def test_manifest_resigned_fields_and_entries(self):
        manifest,_=package({(0,0):[(0,row())]})
        changes={0:0,4:2,8:44,12:0,16:65537,20:0x7fc00000,24:4097,28:262145,32:2049,36:4096,40:37,44:8192,48:262145,52:1,56:1,60:1,64:1,68:99,76:0,80:0,84:0,88:0,92:16384,96:2048,100:40,104:76,108:1,112:1,116:0,124:73}
        for off,value in changes.items():
            with self.subTest(off=off):
                b=bytearray(manifest);struct.pack_into('<I',b,off,value);(self.root/'manifest.dcs').write_bytes(resign(b))
                self.assertIsNotNone(lib.dc_test_index(os.fsencode(self.root/'manifest.dcs'),c.byref(self.index)));self.assertEqual(lib.dc_test_live(),0)
        b=bytearray(manifest);b[-1]^=1;(self.root/'manifest.dcs').write_bytes(b)
        self.assertIsNotNone(lib.dc_test_index(os.fsencode(self.root/'manifest.dcs'),c.byref(self.index)))
    def test_duplicate_and_unsorted_manifest_cells(self):
        manifest,sectors=package({(0,0):[(0,row())],(1,0):[(1,row(17))]})
        for duplicate in (True,False):
            b=bytearray(manifest)
            if duplicate:b[128:136]=b[108:116]
            else:b[108:128],b[128:148]=b[128:148],b[108:128]
            self.assertIsNotNone(self.install((resign(b),sectors)));self.assertEqual(lib.dc_test_live(),0)
    def test_sector_every_truncation_eof_and_crc_are_atomic(self):
        self.assertIsNone(self.install());self.assertIsNone(self.target());p=self.root/'sectors/0_0.dcs';good=p.read_bytes()
        for b in [good[:i] for i in range(len(good))]+[good+b'x',good[:-1]+bytes([good[-1]^1])]:
            p.write_bytes(b);self.cache.package_crc=self.index.package_crc^1;self.expect_rollback(lambda:self.target())
    def test_sector_resigned_bad_records_even_when_filtered(self):
        self.assertIsNone(self.install());self.assertIsNone(self.target())
        for off,value in ((0,0),(4,2),(8,1),(12,1),(16,2),(20,1),(24,1),(28,0x7fc00000),(32,0x7f800000),(64,24),(64,56)):
            manifest,sectors=package({(0,0):[(0,row(10,10))]});b=bytearray(sectors[0,0]);struct.pack_into('<I',b,off,value);b=resign(b)
            entry=self.index.entries[0];entry.crc=struct.unpack_from('<I',b,len(b)-4)[0]
            (self.root/'sectors/0_0.dcs').write_bytes(b);self.cache.package_crc=self.index.package_crc^1
            with self.subTest(off=off,value=value):self.expect_rollback(lambda:self.target())
    def test_pat_modes_and_unchanged_other_bits(self):
        for mode in (0,8,16):
            lib.dc_sector_cache_free(c.byref(self.cache));lib.dc_sector_index_free(c.byref(self.index))
            pat=mode|(63<<14)|0x40000000
            self.assertIsNone(self.install(package({(0,0):[(0,row(pat=pat))]})));self.assertIsNone(self.target());self.assertEqual(self.rows(),[row(pat=pat)])
    def test_dedup_sort_payload_match_and_boundary_filter(self):
        data=package({(-1,0):[(2,row(-1))],(0,0):[(0,row(6)),(1,row(6.01)),(2,row(-1))]},source_count=3)
        self.assertIsNone(self.install(data));self.assertIsNone(self.target());self.assertEqual(self.rows(),[row(6),row(-1)])
        b=bytearray(data[1][0,0]);struct.pack_into('<f',b,24+2*44+8,1);b=resign(b);(self.root/'sectors/0_0.dcs').write_bytes(b)
        self.index.entries[1].crc=struct.unpack_from('<I',b,len(b)-4)[0];self.cache.package_crc=self.index.package_crc^1
        self.assertEqual(self.expect_rollback(lambda:self.target()),b'sector replica payload mismatch')
    def test_invalid_arguments_before_reuse(self):
        self.assertIsNone(self.install());self.assertIsNone(self.target())
        for b in (Bounds(4,0,0,4),Bounds(0,float('nan'),0,4),Bounds(0,34,0,4),Bounds(0,4,4,0)):
            self.expect_rollback(lambda:self.target(b))
        for v in (Vec(float('inf'),0,0),Vec(1000001,0,0)):
            self.expect_rollback(lambda:self.target(origin=v))
        for root in (None,b'',b'a'*476):
            self.expect_rollback(lambda:lib.dc_sector_retarget(c.byref(self.index),root,Bounds(0,4,0,4),Vec(),c.byref(self.cache)))
    def test_reuse_keeps_origin_and_does_no_io_or_allocation(self):
        self.assertIsNone(self.install());self.assertIsNone(self.target(origin=Vec(1,0,1)));before=bytes(self.cache)
        lib.dc_test_reset(1);lib.dc_test_io_reset(1,0)
        self.assertIsNone(self.target(origin=Vec(4,0,4),root=self.root/'absent'));self.assertEqual(bytes(self.cache),before);self.assertEqual(lib.dc_test_calls(),0)
    def test_known_air_missing_region_and_unloaded(self):
        self.assertEqual(lib.dc_sector_covers(c.byref(self.cache),Bounds()),0)
        self.assertIsNone(self.install());self.assertIsNone(self.target(Bounds(20,21,20,21)))
        self.assertEqual((self.cache.valid,self.cache.world.ready,self.cache.world.count,self.cache.world.triangles),(1,1,0,None));self.assertEqual(lib.dc_test_calls(),1)
        self.assertEqual(self.expect_rollback(lambda:self.target(Bounds(1000,1001,0,1))),b'missing sector region')
        self.assertIsNone(self.target());(self.root/'sectors/0_0.dcs').unlink();self.cache.package_crc=self.index.package_crc^1
        self.assertEqual(self.expect_rollback(lambda:self.target()),b'missing sector file')
    def test_allocation_failures_keep_existing_world(self):
        self.assertIsNone(self.install());self.assertIsNone(self.target())
        for n in range(1,5):
            lib.dc_test_reset(n);self.cache.package_crc=self.index.package_crc^1;self.expect_rollback(lambda:self.target())
        lib.dc_test_reset(0);self.assertIsNone(self.target());self.assertLess(lib.dc_test_peak(),2*1024*1024)
    def test_manifest_allocation_failure_is_zero(self):
        lib.dc_test_reset(1);self.assertEqual(self.install(),b'sector manifest allocation failed');self.assertEqual(bytes(self.index),bytes(Index()))
    def test_read_failures_keep_existing_world_and_close_files(self):
        self.assertIsNone(self.install());self.assertIsNone(self.target())
        for n in range(1,4):
            self.cache.package_crc=self.index.package_crc^1;lib.dc_test_io_reset(n,0);self.expect_rollback(lambda:self.target())
    def test_close_failure_is_atomic(self):
        self.assertIsNone(self.install());self.assertIsNone(self.target());self.cache.package_crc=self.index.package_crc^1;lib.dc_test_io_reset(0,1);self.expect_rollback(lambda:self.target())
    def test_8192_limit_accepts_and_8193_refuses_atomic(self):
        for count in (8192,8193):
            lib.dc_sector_index_free(c.byref(self.index))
            groups={}
            for i in range(count):groups.setdefault((i//2048,0),[]).append((i,row((i//2048)*4+.1)))
            self.assertIsNone(self.install(package(groups,4,count)));self.cache.package_crc=self.index.package_crc^1
            if count==8192:self.assertIsNone(self.target(Bounds(0,22,0,3)));self.assertEqual(self.cache.world.count,8192)
            else:self.assertEqual(self.expect_rollback(lambda:self.target(Bounds(0,22,0,3))),b'sector unique triangle cap exceeded')
        self.assertLess(lib.dc_test_peak(),2*1024*1024)
    def test_rebase_and_build_failure_are_atomic(self):
        self.assertIsNone(self.install());self.assertIsNone(self.target())
        self.cache.package_crc=self.index.package_crc^1;self.assertEqual(self.expect_rollback(lambda:self.target(origin=Vec(1000000,0,0))),b'rebased triangle exceeds local limit')
        r=struct.pack('<9fI',*[1]*9,0);manifest,sectors=package({(0,0):[(0,r)]});(self.root/'sectors/0_0.dcs').write_bytes(sectors[0,0]);self.index.entries[0].crc=zlib.crc32(sectors[0,0][:-4]);self.cache.package_crc=self.index.package_crc^1
        self.expect_rollback(lambda:self.target())
    def test_corrupt_committed_bounds_cannot_claim_coverage(self):
        self.assertIsNone(self.install());self.assertIsNone(self.target());self.cache.coverage.min_x=float('nan')
        self.assertEqual(lib.dc_sector_covers(c.byref(self.cache),Bounds(0,4,0,4)),0)
    def test_shared_native_oracle_with_synthetic_geometry(self):
        from make_sector_fixture import prepare
        root=self.root/'fixture';self.assertEqual(prepare(root),4)
        exe=PORT/'build'/('sector-cache-check.exe' if sys.platform=='win32' else 'sector-cache-check')
        run=subprocess.run([str(exe),str(root)],capture_output=True,text=True,check=True)
        self.assertIn('44/44 checks windows=4',run.stdout)
    def test_public_oracle_preparation_binds_source_and_runs_shared_c(self):
        from make_sector_oracle import prepare
        data=struct.pack('<4sIII',b'DCR1',1,43,2)+row(-8,-8)+row(17,0)
        source=self.root/'source.dcr';source.write_bytes(data);p=pc.build_package(data)
        package_dir=self.root/'package';pc.write_package(package_dir,p)
        result=prepare(source,package_dir,self.root/'oracle')
        exe=PORT/'build'/('sector-cache-check.exe' if sys.platform=='win32' else 'sector-cache-check')
        run=subprocess.run([str(exe),str(self.root/'oracle')],capture_output=True,text=True,check=True)
        self.assertIn(f"{result['native_checks']}/{result['native_checks']} checks",run.stdout)
        source.write_bytes(data[:-1]+bytes([data[-1]^1]))
        with self.assertRaises(ValueError):prepare(source,package_dir,self.root/'refused')
        self.assertFalse((self.root/'refused').exists())
if __name__=='__main__':unittest.main(verbosity=2)
