"""DCSH v1 native SH-4 module container writer (AI-assisted)."""
import struct
import zlib

HEADER=64
ABI=1  # little-endian SH-4, GCC -m4-single, uint64 scalar containers
MAX_FILE=8*1024*1024

def pack_native_module(objects,data,initializer=None,initializer_kind=0):
    functions=list(objects.values())
    if not 1<=len(functions)<=4096 or len(data)>8192: raise ValueError('native module export count limit')
    if initializer_kind not in (0,1,2) or bool(initializer)!=(initializer_kind!=0):
        raise ValueError('native module initializer ABI must be absent, uint64 or void')
    if initializer and initializer not in {report['name'] for _,report in functions}:
        raise ValueError('native module initializer is not exported')
    strings=bytearray(); names={}; payload=bytearray(); relocs=[]
    def string(value):
        if not isinstance(value,str) or not value or '\0' in value: raise ValueError('invalid native module name')
        if value not in names:
            names[value]=len(strings); strings.extend(value.encode('utf-8')+b'\0')
        return names[value]
    def blob(value):
        payload.extend(b'\0'*((-len(payload))&3)); offset=len(payload); payload.extend(value)
        return offset
    def relocations(values):
        if len(values)>4096: raise ValueError('native module per-object relocation limit')
        start=len(relocs)
        relocs.extend((r['offset'],string(r['symbol'])) for r in values)
        return start,len(values)
    code_rows=[]; data_rows=[]
    for code,report in functions:
        start,count=relocations(report['relocations'])
        code_rows.append((string(report['name']),blob(code),len(code),start,count,0))
    for item in data:
        start,count=relocations(item['relocations'])
        data_rows.append((string(item['name']),blob(item['data']),len(item['data']),item['address_offset'],start,count))
    init=string(initializer) if initializer else 0xffffffff
    if initializer and len(initializer.encode('utf-8'))>=256: raise ValueError('native initializer name limit')
    if len(relocs)>262144 or len(strings)>1048576 or not 1<=len(payload)<=4*1048576:
        raise ValueError('native module metadata/payload limit')
    body=b''.join(struct.pack('<6I',*row) for row in code_rows+data_rows)
    body+=b''.join(struct.pack('<2I',*r) for r in relocs)+strings+payload
    total=HEADER+len(body)
    if total>MAX_FILE: raise ValueError('native module file budget')
    return struct.pack('<4s15I',b'DCSH',1,ABI,total,len(functions),len(data),len(relocs),len(strings),len(payload),
                       init,initializer_kind,zlib.crc32(body),0,0,0,0)+body
