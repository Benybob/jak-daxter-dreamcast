"""Native SH-4 emitter/linking bootstrap for a future GOAL backend (AI-assisted).

This is NOT an integrated GOAL compiler. It exercises instruction emission,
literal pools, branch fixups, external relocations and GCC-compatible ABI calls.
Unsupported/range-limited constructs fail; there is no interpreter fallback.
"""
import argparse
from bisect import bisect_left
import json
from pathlib import Path
import struct

class Emitter:
    def __init__(self):
        self.code=bytearray()
        self.labels={}
        self.branches=[]
        self.literals=[]

    def word(self,value): self.code+=struct.pack('<H',value)

    @staticmethod
    def reg(value):
        if not isinstance(value,int) or not 0<=value<16: raise ValueError('invalid SH-4 register')
        return value

    def rr(self,opcode,dst,src): self.word(opcode|self.reg(dst)<<8|self.reg(src)<<4)
    def mov(self,dst,src): self.rr(0x6003,dst,src)
    def add(self,dst,src): self.rr(0x300c,dst,src)
    def sub(self,dst,src): self.rr(0x3008,dst,src)
    def addc(self,dst,src): self.rr(0x300e,dst,src)
    def subc(self,dst,src): self.rr(0x300a,dst,src)
    def neg(self,dst,src): self.rr(0x600b,dst,src)
    def mul(self,left,right): self.rr(0x0007,left,right)
    def macl(self,dst): self.word(0x001a|self.reg(dst)<<8)
    def fadd(self,dst,src): self.rr(0xf000,dst,src)
    def fmov(self,dst,src): self.rr(0xf00c,dst,src)
    def load(self,dst,address): self.rr(0x6002,dst,address)
    def store(self,address,src): self.rr(0x2002,address,src)
    def compare_gt(self,left,right): self.rr(0x3007,left,right)
    def compare_pz(self,reg): self.word(0x4011|self.reg(reg)<<8)
    def nop(self): self.word(0x0009)
    def clrt(self): self.word(0x0008)
    def ret(self): self.word(0x000b); self.nop()

    def immediate(self,dst,value):
        if not -128<=value<=127: raise ValueError('SH-4 immediate outside signed byte')
        self.word(0xe000|self.reg(dst)<<8|(value&255))

    def add_immediate(self,dst,value):
        if not -128<=value<=127: raise ValueError('SH-4 immediate outside signed byte')
        self.word(0x7000|self.reg(dst)<<8|(value&255))

    def constant(self,dst,value):
        if not -(1<<31)<=value<(1<<32): raise ValueError('32-bit literal required')
        if -128<=value<=127: self.immediate(dst,value)
        else: self.literal(dst,('integer',value&0xffffffff))

    def literal(self,dst,key):
        offset=len(self.code)
        self.word(0xd000|self.reg(dst)<<8)
        self.literals.append((offset,key))

    def label(self,name):
        if name in self.labels: raise ValueError('duplicate label')
        self.labels[name]=len(self.code)

    def branch(self,kind,target):
        if kind not in ('bt','bf','bra','bsr'): raise ValueError('unsupported branch')
        offset=len(self.code)
        self.word({'bt':0x8900,'bf':0x8b00,'bra':0xa000,'bsr':0xb000}[kind])
        self.branches.append((offset,kind,target))
        if kind in ('bra','bsr'): self.nop()

    def call_symbol(self,name):
        self.word(0x4f22)  # sts.l pr,@-r15
        self.literal(1,('symbol',name))
        self.word(0x410b)  # jsr @r1
        self.nop()
        self.word(0x4f26)  # lds.l @r15+,pr

    def _layout(self,relax):
        expanded=set()
        while True:
            ordered=sorted(expanded)
            def mapped(offset): return offset+4*bisect_left(ordered,offset)
            added=set()
            for offset,kind,name in self.branches:
                if name not in self.labels: raise ValueError('undefined label')
                if relax and kind in ('bt','bf') and offset not in expanded:
                    delta=mapped(self.labels[name])-(mapped(offset)+4)
                    if not -256<=delta<=254: added.add(offset)
            if not added: break
            expanded.update(added)
        code=bytearray()
        branch_kinds={offset:kind for offset,kind,_ in self.branches}
        for offset in range(0,len(self.code),2):
            if offset in expanded:
                # Invert the condition over a longer BRA and its delay slot.
                inverse=0x8b00 if branch_kinds[offset]=='bt' else 0x8900
                code+=struct.pack('<3H',inverse|1,0xa000,0x0009)
            else: code+=self.code[offset:offset+2]
        branches=[(mapped(offset)+(2 if offset in expanded else 0),
                   'bra' if offset in expanded else kind,name) for offset,kind,name in self.branches]
        return code,branches,{name:mapped(offset) for name,offset in self.labels.items()},[(mapped(offset),key) for offset,key in self.literals]

    def instruction_size(self,relax=False): return len(self._layout(relax)[0])

    def finish(self,relax=False):
        code,branches,labels,literals=self._layout(relax)
        relocations=[]
        for offset,kind,name in branches:
            delta=labels[name]-(offset+4)
            if delta%2: raise ValueError('unaligned branch')
            displacement=delta//2
            bits=8 if kind in ('bt','bf') else 12
            if not -(1<<(bits-1))<=displacement<(1<<(bits-1)): raise ValueError('branch range exceeded')
            original=struct.unpack_from('<H',code,offset)[0]
            struct.pack_into('<H',code,offset,original|(displacement&((1<<bits)-1)))
        if literals and len(code)%4: code+=struct.pack('<H',0x0009)
        pools={}
        for offset,key in literals:
            if key not in pools:
                pools[key]=len(code)
                if key[0]=='symbol':
                    relocations.append({'offset':len(code),'symbol':key[1]})
                    code+=b'\0'*4
                else: code+=struct.pack('<I',key[1])
            delta=pools[key]-((offset+4)&~3)
            if delta<0 or delta%4 or delta//4>255: raise ValueError('literal pool range exceeded')
            original=struct.unpack_from('<H',code,offset)[0]
            struct.pack_into('<H',code,offset,original|(delta//4))
        return bytes(code),relocations

def probes():
    result={}
    e=Emitter(); e.mov(0,4); e.add(0,5); e.ret(); result['add32']=e.finish()
    e=Emitter(); e.compare_pz(4); e.branch('bf','negative'); e.mov(0,4); e.add_immediate(0,5); e.ret()
    e.label('negative'); e.neg(0,4); e.ret(); result['choose32']=e.finish()
    e=Emitter(); e.constant(0,1); e.constant(1,1); e.label('loop'); e.compare_gt(4,1); e.branch('bf','end')
    e.mul(0,4); e.macl(0); e.add_immediate(4,-1); e.branch('bra','loop'); e.label('end'); e.ret()
    result['factorial32']=e.finish()
    e=Emitter(); e.constant(0,0x12345678); e.ret(); result['literal32']=e.finish()
    e=Emitter(); e.clrt(); e.mov(0,4); e.addc(0,6); e.mov(1,5); e.addc(1,7); e.ret(); result['add64']=e.finish()
    e=Emitter(); e.clrt(); e.mov(0,4); e.subc(0,6); e.mov(1,5); e.subc(1,7); e.ret(); result['sub64']=e.finish()
    e=Emitter(); e.fmov(0,4); e.fadd(0,5); e.ret(); result['addfloat']=e.finish()
    e=Emitter(); e.store(4,5); e.load(0,4); e.ret(); result['memory32']=e.finish()
    e=Emitter(); e.call_symbol('bridge'); e.add_immediate(0,3); e.ret(); result['call_bridge']=e.finish()
    e=Emitter(); e.literal(1,('symbol','global_value')); e.load(0,1); e.ret(); result['global_load']=e.finish()
    e=Emitter(); e.constant(0,42); e.ret(); result['cache_patch']=e.finish()
    return result

def write_header(path):
    lines=['/* Generated native SH-4 probes (AI-assisted); not GOAL game code. */']
    objects=probes()
    for name,(code,relocations) in objects.items():
        lines.append(f'static const unsigned char probe_{name}[] = {{'+','.join(str(b) for b in code)+'};')
        lines.append(f'#define PROBE_{name.upper()}_SIZE {len(code)}u')
        for relocation in relocations:
            lines.append(f'#define PROBE_{name.upper()}_{relocation["symbol"].upper()}_OFFSET {relocation["offset"]}u')
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text('\n'.join(lines)+'\n')
    return {name:{'bytes':len(data),'relocations':rels} for name,(data,rels) in objects.items()}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--header',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(write_header(args.header),indent=2))
