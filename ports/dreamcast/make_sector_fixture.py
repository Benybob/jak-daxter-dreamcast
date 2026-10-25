"""Original-free sector IO/geometry fixture (AI-assisted), ISC."""
from pathlib import Path
import argparse,math,struct,zlib
import partition_collision as pc
def prepare(out):
    faces=[(-8.,-8.,0),(0.,0.,8|(3<<14)),(17.,0.,16)]
    rows=[struct.pack('<9fI',x,0,z,x,0,z+1,x+1,0,z,pat) for x,z,pat in faces]
    package=pc.build_package(struct.pack('<4sIII',b'DCR1',1,43,len(rows))+b''.join(rows))
    pc.write_package(out,package)
    queries=[(-10,-6,-10,-6),(-2,2,-2,2),(15,19,-2,2),(5,6,20,21)]
    body=bytearray(struct.pack('<4sIIII',b'SCO1',1,len(queries),len(rows),zlib.crc32(package.manifest[:-4])))
    for q in queries:
        origin=((q[0]+q[1])/2,0,(q[2]+q[3])/2);included=[]
        for row in rows:
            values=struct.unpack('<9fI',row);xs=values[0:9:3];zs=values[2:9:3]
            if max(xs)>=q[0]-2 and min(xs)<=q[1]+2 and max(zs)>=q[2]-2 and min(zs)<=q[3]+2:
                included.append(struct.pack('<9fI',*[v-origin[i%3] for i,v in enumerate(values[:9])],values[9]))
        body+=struct.pack('<7fII',*q,*origin,len(included),zlib.crc32(b''.join(included)))
    (out/'oracle.bin').write_bytes(body)
    return len(queries)
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path);args=parser.parse_args()
    print('synthetic sector windows='+str(prepare(args.output)))
