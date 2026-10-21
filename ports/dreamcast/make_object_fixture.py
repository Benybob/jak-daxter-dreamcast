"""Independent V2/V4 format fixtures and optional local retail sample (AI-assisted)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

def object_file(payload,pointers=b'\0',imports=b'\0',version=4):
    tables=pointers+imports
    length=(12+len(tables)+63)&~63
    link=struct.pack('<III',0xffffffff,length,2)+tables+b'\0'*(length-12-len(tables))
    if version==2: return link+payload
    return struct.pack('<IIII',0xffffffff,length,4,len(payload))+payload+link

def fixture(version=4):
    payload=bytearray(64)
    struct.pack_into('<4I',payload,0,0xffffffff,0xffffffff,0xffffffff,20)
    imports=b'\x80basic\0\0\0global\0\x04\0_empty_\0\x08\0\0'
    return object_file(payload,b'\x03\x01\0',imports,version)

def retail_sample(path,name='game-cnt'):
    with Path(path).open('rb') as stream:
        header=stream.read(64)
        if len(header)!=64 or header[:4]==b'oZlB': raise ValueError('invalid/uncompressed archive required')
        count=struct.unpack_from('<I',header)[0]
        if count>65536: raise ValueError('archive count limit')
        end=Path(path).stat().st_size
        for _ in range(count):
            header=stream.read(64)
            if len(header)!=64: raise ValueError('short object header')
            size=struct.unpack_from('<I',header)[0]
            if size>end-stream.tell(): raise ValueError('object outside archive')
            if header[4:].split(b'\0',1)[0].decode('ascii')==name:
                if size>8*1024*1024: raise ValueError('object budget')
                return stream.read(size)
            stream.seek(size+((-size)&15),1)
    raise ValueError('retail object missing: '+name)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--header',type=Path,required=True)
    parser.add_argument('--retail-archive',type=Path)
    parser.add_argument('--retail-art-archive',type=Path)
    args=parser.parse_args()
    large=object_file(b'\0'*(520*4),bytes([255,255,0,10,0]))
    failure=object_file(fixture()[16:80],b'\x03\x01\0',
                        b'\x80fresh\0\0\0\x80taken\0\x04\0_empty_\0\x08\0\0')
    objects={'v4':fixture(),'v2':fixture(2),'large':large,'failure':failure}
    if args.retail_archive: objects['retail']=retail_sample(args.retail_archive)
    if args.retail_art_archive: objects['retail_art']=retail_sample(args.retail_art_archive,'warpgate')
    lines=['/* Generated original-object test inputs (AI-assisted). */']
    for name,data in objects.items():
        lines.append('static const unsigned char object_'+name+'[] = {'+','.join(map(str,data))+'};')
    lines.append('#define DC_OBJECT_HAS_RETAIL '+str(int('retail' in objects)))
    lines.append('#define DC_OBJECT_HAS_ART '+str(int('retail_art' in objects)))
    args.header.parent.mkdir(parents=True,exist_ok=True)
    args.header.write_text('\n'.join(lines)+'\n')
    manifest={'ai_assisted':True,'scope':'Format fixtures; optional unmodified local retail data',
              'objects':{name:{'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()} for name,data in objects.items()}}
    args.header.with_suffix('.manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))

if __name__=='__main__': main()
