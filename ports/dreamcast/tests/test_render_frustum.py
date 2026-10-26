"""Independent exact rational viewport geometry/attribute oracle (AI-assisted)."""
from pathlib import Path
import ctypes,math,struct,sys,unittest
PORT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(PORT))
from frustum_oracle import cases,oracle,f32

class Vertex(ctypes.Structure):
    _fields_=[(n,ctypes.c_float) for n in ('x','y','z','u','v')]+[('argb',ctypes.c_uint32)]

class RenderFrustum(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        dll=PORT/'build/render-frustum-check.dll'
        if not dll.exists():dll=dll.with_suffix('.so')
        cls.lib=ctypes.CDLL(str(dll));cls.clip=cls.lib.dc_render_clip_viewport
        cls.clip.argtypes=[ctypes.POINTER(Vertex),ctypes.POINTER(Vertex)];cls.clip.restype=ctypes.c_int
    def run_clip(self,triangle):
        source=(Vertex*3)(*(Vertex(*v) for v in triangle))
        class Fenced(ctypes.Structure):
            _fields_=[('before',ctypes.c_uint32*4),('v',Vertex*8),('after',ctypes.c_uint32*4)]
        out=Fenced();ctypes.memset(ctypes.byref(out),0x6d,ctypes.sizeof(out));before=bytes(source)
        n=self.clip(source,out.v)
        self.assertEqual(bytes(source),before)
        self.assertEqual(list(out.before)+list(out.after),[0x6d6d6d6d]*8)
        return n,out
    def test_exact_barycentric_oracle_across_five_planes_and_long_faces(self):
        for index,triangle in enumerate(cases(384)):
            with self.subTest(index=index):
                expected=oracle(triangle);n,out=self.run_clip(triangle);self.assertEqual(n,len(expected))
                unmatched=list(expected)
                for v in out.v[:n]:
                    got=(v.x,v.y,v.z,v.u,v.v,v.argb)
                    best=min(unmatched,key=lambda e:sum((a-b)**2 for a,b in zip(got[:3],e[:3])))
                    unmatched.remove(best)
                    for a,b in zip(got[:5],best[:5]):self.assertAlmostEqual(a,b,delta=1e-5+2e-6*abs(b))
                    for shift in (0,8,16,24):self.assertLessEqual(abs(((v.argb>>shift)&255)-((best[5]>>shift)&255)),1)
                    self.assertGreaterEqual(v.z,f32(.05)-1e-7)
                    self.assertGreaterEqual(320+390*v.x/v.z,-.002)
                    self.assertLessEqual(320+390*v.x/v.z,640.002)
                    self.assertGreaterEqual(260-390*v.y/v.z,-.002)
                    self.assertLessEqual(260-390*v.y/v.z,480.002)
    def test_inside_is_exact_copy_and_rejection_leaves_output_unchanged(self):
        for tri in cases(2):
            n,out=self.run_clip(tri)
            if n:self.assertEqual(bytes(out.v)[:72],bytes((Vertex*3)(*(Vertex(*v) for v in tri))))
            else:self.assertEqual(bytes(out.v),b'\x6d'*ctypes.sizeof(out.v))
    def test_invalid_attributes_and_bounds_are_transactional(self):
        base=cases(1)[0]
        for attr in range(5):
            for value in (math.nan,math.inf,-math.inf,4000001. if attr<3 else 65537.):
                tri=[list(v) for v in base];tri[2][attr]=value
                n,out=self.run_clip(tri);self.assertEqual(n,-1);self.assertEqual(bytes(out.v),b'\x6d'*ctypes.sizeof(out.v))
    def test_ambiguous_single_precision_plane_uses_exact_side(self):
        x=f32(-320/390);bits=struct.unpack('<I',struct.pack('<f',x))[0]
        for edge in (x,struct.unpack('<f',struct.pack('<I',bits-1))[0]):
            tri=[(edge,-.1,1.,0.,0.,0xffffffff),(edge,.1,1.,1.,0.,0xffffffff),(edge,0.,1.,0.,1.,0xffffffff)]
            n,out=self.run_clip(tri);self.assertEqual(n,0 if 390*edge+320<0 else 3)
    def test_winding_and_affine_uv_at_exact_clipped_corner(self):
        tri=[(-1.,-1.,1.,0.,0.,0xff000000),(1.,-1.,1.,1.,0.,0xff800080),(0.,1.,1.,.5,1.,0xffffffff)]
        n,out=self.run_clip(tri);self.assertGreaterEqual(n,3)
        for v in out.v[:n]:
            self.assertAlmostEqual(v.u,(v.x+1)/2,delta=1e-6)
            self.assertAlmostEqual(v.v,(v.y+1)/2,delta=1e-6)
        def area(vs):return sum(a.x*b.y-b.x*a.y for a,b in zip(vs,vs[1:]+vs[:1]))
        self.assertGreater(area(list(out.v[:n])),0)
        n,reverse=self.run_clip(list(reversed(tri)));self.assertLess(area(list(reverse.v[:n])),0)
if __name__=='__main__':unittest.main()
