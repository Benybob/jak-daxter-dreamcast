"""Native reader framing, role, memory and IO failures (AI-assisted), ISC."""
import ctypes,sys,struct,tempfile,unittest,zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import prepare_render_sectors as r
from test_render_sectors import _package,_interior,_solo,_one

DLL=Path(__file__).resolve().parents[1]/'build/render-package-check.dll'
@unittest.skipUnless(DLL.exists(),'build the native C render reader host bridge')
class RenderPackageNative(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ctypes.CDLL(str(DLL));cls.check=cls.lib.dc_render_test_check
        cls.check.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32)]
        cls.check.restype=ctypes.c_char_p
    def call(self,p,blob=None,index=0,global_=False,af=0,rf=0,manifest=None):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);m=root/'m';s=root/'s'
            m.write_bytes(p.manifest if manifest is None else manifest)
            if blob is not None:s.write_bytes(blob)
            words=(ctypes.c_uint32*16)()
            error=self.check(str(m).encode(),str(s).encode() if blob is not None else None,index,global_,af,rf,words)
            self.assertEqual(words[12],0,'all reader allocations must be freed')
            return error,list(words)
    def resign(self,blob):
        raw=bytearray(blob);struct.pack_into('<I',raw,len(raw)-4,r.crc32(raw[:-4]));return bytes(raw)
    def mutate_field(self,p,name,value,kind='I'):
        raw=bytearray(p.manifest);struct.pack_into('<'+kind,raw,r.HEADER_OFFSETS[name],value);return self.resign(raw)
    def rewritten(self,p,payload,global_=False,compressed=None):
        h=r.parse_manifest(p.manifest);old=p.global_sector if global_ else p.sectors[sorted(p.sectors)[0]]
        original=r.parse_sector(old);comp=zlib.compress(payload,9) if compressed is None else compressed
        hdr=r.SECTOR_HEADER_STRUCT.pack(b'DRS2',2,int(global_),original['ix'],original['iz'],original['record_count'],original['uncompressed_bytes'],len(comp))
        blob=hdr+comp+struct.pack('<II',r.crc32(payload),r.crc32(comp))
        raw=bytearray(p.manifest)
        if global_:
            struct.pack_into('<III',raw,152,len(blob),r.crc32(payload),r.crc32(comp))
        else:
            offset=256+h['texture_count']*16+h['texture_blob_bytes']+h['material_count']*64
            struct.pack_into('<III',raw,offset+16,len(blob),r.crc32(payload),r.crc32(comp))
        struct.pack_into('<I',raw,44,h['package_bytes']+len(blob)-len(old))
        return self.resign(raw),blob
    def test_valid_empty_global_and_triangle_payload(self):
        p=_package(_interior().to_bytes());blob=next(iter(p.sectors.values()))
        error,words=self.call(p,blob);self.assertIsNone(error);self.assertEqual(words[0],1);self.assertEqual(words[7],1)
        self.assertEqual(words[8],116);self.assertEqual(words[9],r.parse_sector(blob)['payload_crc'])
        error,words=self.call(p,p.global_sector,global_=True);self.assertIsNone(error);self.assertEqual(words[7],0)
    def test_allocation_and_io_failures_release_ownership(self):
        p=_package(_interior().to_bytes());blob=next(iter(p.sectors.values()))
        err,words=self.call(p,blob);self.assertIsNone(err)
        for n in range(1,words[10]+1):
            with self.subTest(allocation=n):self.assertIsNotNone(self.call(p,blob,af=n)[0])
        for n in range(1,words[11]+1):
            with self.subTest(io=n):self.assertIsNotNone(self.call(p,blob,rf=n)[0])
        self.assertIsNone(self.call(p,blob)[0])
    def test_manifest_exact_eof_crc_and_each_truncation(self):
        p=_package(_interior().to_bytes())
        for size in (0,1,255,256,257,len(p.manifest)-1):
            with self.subTest(size=size):self.assertIsNotNone(self.call(p,manifest=p.manifest[:size])[0])
        self.assertIsNotNone(self.call(p,manifest=p.manifest+b'X')[0])
        raw=bytearray(p.manifest);raw[-1]^=1;self.assertIsNotNone(self.call(p,manifest=raw)[0])
    def test_resigned_manifest_contract_and_overflow_attempts(self):
        p=_package(_interior().to_bytes())
        fields={'version':1,'flags':0,'source_count':0,'texture_count':257,'material_count':257,
                'local_sector_count':8193,'texture_blob_bytes':0xffffffff,'tables_bytes':0xffffffff,
                'global_count':2,'package_bytes':8388609,'replica_count':37,'reserved_2':1,
                'face_stride':80,'vertex_stride':24,'material_stride':16,'normal_encoding':0}
        for name,value in fields.items():
            with self.subTest(name=name):self.assertIsNotNone(self.call(p,manifest=self.mutate_field(p,name,value))[0])
        for value in (float('nan'),float('inf'),3.9,64.1):
            self.assertIsNotNone(self.call(p,manifest=self.mutate_field(p,'cell_size',value,'f'))[0])
    def test_texture_offset_dimension_and_material_role_forgery(self):
        p=_package(_interior().to_bytes());h=r.parse_manifest(p.manifest)
        mat=256+h['texture_count']*16+h['texture_blob_bytes']
        for offset,value in ((256,0xffffffff),(256+4,0xffffffff),(256+12,2),
                             (mat,999),(mat+4,4|16),(mat+16,0),(mat+32,999),(mat+56,1)):
            raw=bytearray(p.manifest);struct.pack_into('<I',raw,offset,value)
            with self.subTest(offset=offset,value=value):self.assertIsNotNone(self.call(p,manifest=self.resign(raw))[0])
    def test_reflection_materials_flags_are_accepted(self):
        p=_package(_interior().to_bytes());h=r.parse_manifest(p.manifest);raw=bytearray(p.manifest)
        offset=256+h['texture_count']*16+h['texture_blob_bytes']+h['material_count']*64
        for pos,value in ((24,2),(204,2),(offset+8,2),(offset+12,232)):
            struct.pack_into('<I',raw,pos,value)
        self.assertIsNotNone(self.call(p,manifest=self.resign(raw))[0])
        b=_solo({'doubleSided':True,'pbrMetallicRoughness':{'metallicRoughnessTexture':{'index':0}},
                 'extensions':{'KHR_materials_specular':{'specularColorTexture':{'index':1}}}},
                 images=[(255,0,128,255),(255,0,0,255)],textures=[{'source':0,'sampler':0},{'source':1}],
                 samplers=[{'magFilter':9728,'minFilter':9728}])
        p=_package(b.to_bytes());self.assertIsNone(self.call(p,next(iter(p.sectors.values())))[0])
    def test_blend_material_and_its_four_bit_texture_role(self):
        b=_solo({'alphaMode':'BLEND','pbrMetallicRoughness':{'baseColorTexture':{'index':0}}},
                images=[(255,128,0,136)],textures=[{'source':0}])
        p=_package(b.to_bytes());h=r.parse_manifest(p.manifest)
        self.assertEqual((h['materials'][0]['base_flags'],h['textures'][0]['format']),(r.FLAG_BLEND,r.TEXTURE_ARGB4444))
        self.assertIsNone(self.call(p,next(iter(p.sectors.values())))[0])
        mat=256+h['texture_count']*16+h['texture_blob_bytes']
        for offset,value in ((mat+4,r.FLAG_BLEND|r.FLAG_MASK),(mat+12,1),(256+12,r.TEXTURE_ARGB1555)):
            raw=bytearray(p.manifest);struct.pack_into('<I',raw,offset,value)
            with self.subTest(offset=offset,value=value):self.assertIsNotNone(self.call(p,manifest=self.resign(raw))[0])
    def test_sector_roles_header_and_eof(self):
        p=_package(_interior().to_bytes());blob=next(iter(p.sectors.values()))
        for raw in (blob[:-1],blob+b'X',b'',bytes(32)):
            with self.subTest(size=len(raw)):self.assertIsNotNone(self.call(p,raw)[0])
        for offset,value in ((4,1),(8,1),(12,999),(20,2),(24,0),(28,0xffffffff)):
            raw=bytearray(blob);struct.pack_into('<I',raw,offset,value)
            self.assertIsNotNone(self.call(p,raw)[0])
        raw=bytearray(blob);raw[-1]^=1;self.assertIsNotNone(self.call(p,raw)[0])
    def test_resigned_invalid_face_fields_and_denormal_normal(self):
        p=_package(_interior().to_bytes());raw=r.parse_sector(next(iter(p.sectors.values())))['records'][0]['raw']
        for offset,value,kind in ((0,1,'I'),(4,1,'I'),(8,float('nan'),'f'),(8,1000001,'f'),
                                  (20,65537,'f'),(28,float('nan'),'f')):
            payload=bytearray(raw);struct.pack_into('<'+kind,payload,offset,value)
            m,s=self.rewritten(p,payload);self.assertIsNotNone(self.call(p,s,manifest=m)[0])
        for normal in ((2,0,0),(1e-40,0,0),(float('inf'),0,0)):
            payload=bytearray(raw);struct.pack_into('<3f',payload,28,*normal)
            m,s=self.rewritten(p,payload);self.assertIsNotNone(self.call(p,s,manifest=m)[0])
    def test_resigned_deflate_truncation_trailing_and_bomb(self):
        p=_package(_interior().to_bytes());raw=r.parse_sector(next(iter(p.sectors.values())))['records'][0]['raw'];comp=zlib.compress(raw,9)
        for data in (comp[:-1],comp+b'X',comp+comp,zlib.compress(bytes(500000),9)):
            m,s=self.rewritten(p,raw,compressed=data);self.assertIsNotNone(self.call(p,s,manifest=m)[0])
        m,s=self.rewritten(p,b'',global_=True,compressed=zlib.compress(bytes(8192),9))
        self.assertIsNotNone(self.call(p,s,global_=True,manifest=m)[0])
        raw=bytearray(p.manifest);struct.pack_into('<I',raw,156,1)
        sector=bytearray(p.global_sector);struct.pack_into('<I',sector,len(sector)-8,1)
        self.assertIsNotNone(self.call(p,sector,global_=True,manifest=self.resign(raw))[0])
    def test_maximum_block_with_zero_normals(self):
        b=_one([(1,0,1),(1,0,1),(1,0,1)]*4096);p=_package(b.to_bytes())
        blob=next(iter(p.sectors.values()));error,words=self.call(p,blob)
        self.assertIsNone(error);self.assertEqual(words[8],475136)
