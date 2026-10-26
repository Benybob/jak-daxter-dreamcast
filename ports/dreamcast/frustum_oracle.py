"""Exact rational half-space intersection oracle (AI-assisted), ISC.

Independent of the C edge-by-edge algorithm: enumerate intersections of eight
constraints in triangle barycentric coordinates, then order the feasible hull.
No original game assets. All inputs and final outputs are binary32.
"""
from fractions import Fraction as F
import itertools,math,random,struct

def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
NEAR=f32(.05)
def oracle(triangle):
    p=[[F(v) for v in vertex[:5]] for vertex in triangle]
    constraints=[(F(0),F(1),F(0)),(F(0),F(0),F(1)),(F(1),F(-1),F(-1))]
    for normal,bias in (((0,0,1),-F(NEAR)),((390,0,320),0),((-390,0,320),0),((0,-390,260),0),((0,390,220),0)):
        d=[sum(n*v for n,v in zip(normal,point))+bias for point in p]
        constraints.append((d[0],d[1]-d[0],d[2]-d[0]))
    feasible=set()
    for (c,a,b),(cc,aa,bb) in itertools.combinations(constraints,2):
        det=a*bb-b*aa
        if not det:continue
        u=(b*cc-c*bb)/det;v=(c*aa-a*cc)/det
        if all(c+a*u+b*v>=0 for c,a,b in constraints):feasible.add((u,v))
    if len(feasible)<3:return []
    center=[sum(point[k] for point in feasible)/len(feasible) for k in (0,1)]
    ordered=sorted(feasible,key=lambda q:math.atan2(float(q[1]-center[1]),float(q[0]-center[0])))
    result=[]
    for u,v in ordered:
        weights=(1-u-v,u,v);values=[f32(float(sum(weights[i]*p[i][k] for i in range(3)))) for k in range(5)]
        color=0
        for shift in range(0,32,8):
            ch=sum(weights[i]*((triangle[i][5]>>shift)&255) for i in range(3))
            color|=int(ch+F(1,2))<<shift
        result.append((*values,color))
    assert len(result)<=8
    return result

def cases(count=256):
    rng=random.Random(0x44524332)
    seed=[
      [(-.1,-.1,1),(.1,-.1,1),(0,.1,1)],
      [(-.1,-.1,-1),(.1,-.1,-1),(0,.1,-1)],
      # Near-clipped diamond intersects all four viewport sides: exact cap8.
      [(.1076923076923077,.0871794871794872,0),(0,-.08205128205128205,.1),(-.1076923076923077,.005128205128205128,.1)],
      [(-1000000,-1000000,-1000000),(1000000,-1000000,1000000),(0,1000000,1)],
      [(-1897,0,.01),(1897,0,1),(0,1897,100)],
      [(-.5,-.5,.05),(.5,-.5,.05),(0,.5,.05)],
    ]
    while len(seed)<count:
        scale=rng.choice((.01,.1,1,10,100,10000,1000000))
        seed.append([(rng.uniform(-1,1)*scale,rng.uniform(-1,1)*scale,rng.uniform(-.25,2)*scale) for _ in range(3)])
    return [[(*map(f32,xyz),f32((i==1)*1.),f32((i==2)*1.),(0xff000000,0xff800040,0xffffffff)[i])
             for i,xyz in enumerate(triangle)] for triangle in seed[:count]]
