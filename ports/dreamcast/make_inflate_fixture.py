"""Original-free zlib framing/maximum-block fixtures (AI-assisted), ISC."""
from pathlib import Path
import argparse,zlib
def pattern(n,kind):
    if kind==0:return (b'123456789'*((n+8)//9))[:n]
    if kind==1:return bytes(i&255 for i in range(n))
    if kind==2:return bytes(((i*37)^(i>>3))&255 for i in range(n))
    if kind==4:return bytes(n)
    state=0x13579bdf;out=bytearray()
    for _ in range(n):state=(state*1664525+1013904223)&0xffffffff;out.append(state>>24)
    return bytes(out)
def prepare(path):
    lines=['/* Synthetic zlib fixtures (AI-assisted), ISC. Never game data. */',
           'typedef struct {const unsigned char *data;unsigned bytes,output,crc,kind;} dc_inflate_fixture;']
    rows=[]
    def array(name,data):
        lines.append('static const unsigned char '+name+'[]={')
        for i in range(0,len(data),24):lines.append(','.join(str(x) for x in data[i:i+24])+',')
        lines.append('};')
    for i,(n,kind,level,strategy) in enumerate(((63,0,9,zlib.Z_DEFAULT_STRATEGY),(256,1,9,0),(327680,2,9,0),(524288,3,9,0),(524288,4,9,0),(8193,2,0,0),(4096,2,9,zlib.Z_FIXED))):
        raw=pattern(n,kind);obj=zlib.compressobj(level,zlib.DEFLATED,15,8,strategy);data=obj.compress(raw)+obj.flush();name='inflate_fixture_'+str(i);array(name,data)
        rows.append('{'+name+',sizeof('+name+'),'+str(n)+'u,'+str(zlib.crc32(raw))+'u,'+str(kind)+'u}')
    lines.append('static const dc_inflate_fixture inflate_fixtures[]={'+','.join(rows)+'};')
    for name,wbits,kwargs in (('inflate_gzip',31,{}),('inflate_raw',-15,{}),('inflate_dictionary',15,dict(zdict=b'123456789'))):
        obj=zlib.compressobj(9,zlib.DEFLATED,wbits,**kwargs);array(name,obj.compress(pattern(63,0))+obj.flush())
    array('inflate_bomb',zlib.compress(bytes(524289),9))
    array('inflate_empty',zlib.compress(b'',9))
    array('inflate_empty_stored',zlib.compress(b'',0))
    path.parent.mkdir(parents=True,exist_ok=True);path.write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('header',type=Path);a=p.parse_args();prepare(a.header)
