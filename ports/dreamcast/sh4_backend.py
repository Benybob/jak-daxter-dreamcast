"""Native SH-4 emitter/linking bootstrap for a future GOAL backend (AI-assisted).

This is NOT an integrated GOAL compiler. It exercises instruction emission,
literal pools, branch fixups, external relocations and GCC-compatible ABI calls.
Unsupported/range-limited constructs fail; there is no interpreter fallback.
"""
import argparse
from bisect import bisect_left
import json
import os
from pathlib import Path
import struct

class Emitter:
    def __init__(self):
        self.code=bytearray()
        self.labels={}
        self.branches=[]
        self.literals=[]
        self.last_label=-1
        self.pending=None  # deferred code, emitted before the next word or label

    def flush(self):
        if self.pending: pending,self.pending=self.pending,None; pending()

    def word(self,value):
        if self.pending: self.flush()
        self.code+=struct.pack('<H',value)

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
        self.flush()  # the recorded offset is this instruction's
        offset=len(self.code)
        self.word(0xd000|self.reg(dst)<<8)
        self.literals.append((offset,key))

    def label(self,name,keep_pending=False):
        # A label is another way in: deferred code lands before it, unless the
        # caller knows no branch targets it.
        if not keep_pending: self.flush()
        if name in self.labels: raise ValueError('duplicate label')
        self.labels[name]=len(self.code)
        self.last_label=len(self.code)

    def branch(self,kind,target):
        if kind not in ('bt','bf','bra','bsr'): raise ValueError('unsupported branch')
        self.flush()
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

    def instruction_size(self,relax=False,literal_islands=False,far_branches=False):
        return self.finish_with_stats(relax,literal_islands,far_branches)[2]['instruction_bytes']

    def _finish_terminal(self,relax=False):
        code,branches,labels,literals=self._layout(relax)
        instruction_bytes=len(code)
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
        return bytes(code),relocations,dict(instruction_bytes=instruction_bytes,
            expanded_conditional_branches=(instruction_bytes-len(self.code))//4,literal_islands=0)

    @staticmethod
    def _uses(word,n):
        """How an instruction uses Rn: 'stop' (control transfer), 'read', 'write' (an
        overwrite that does not read it) or None. Unsure forms count as reads."""
        top,hi,lo,low=word>>12,word>>8&15,word>>4&15,word&15
        if top in (0xa,0xb) or word in (0x000b,0x002b) or word&0xf0df in (0x400b,0x0003): return 'stop'
        if top==0x8:
            if hi in (0x9,0xb,0xd,0xf): return 'stop'
            return 'read' if n==0 or lo==n else None
        if top in (0x9,0xd,0xe): return 'write' if hi==n else None
        if top in (0x5,0x6):
            if lo==n: return 'read'
            return 'write' if hi==n else None
        if top==0x0:
            if word in (0x0008,0x0009,0x0018): return None
            if (low==0xa and lo in (0,1,5,6)) or word&0xf0ff==0x0029 or (low==2 and lo<5):
                return 'write' if hi==n else None  # STS, MOVT, STC
            if low in (0xc,0xd,0xe):  # MOV.x @(R0,Rm),Rn
                if n==0 or lo==n: return 'read'
                return 'write' if hi==n else None
            return 'read' if n==0 or hi==n or lo==n else None
        if top==0xc: return 'read' if n==0 else None
        if top==0xf:
            if low in (6,7) and n==0: return 'read'
            if low in (6,8,9) and lo==n or low in (7,0xa,0xb) and hi==n: return 'read'
            return None
        return 'read' if hi==n or lo==n else None

    def remove_dead_writes(self,budget=96):
        """Drop side-effect-free register writes (MOV #imm, MOV Rm,Rn, frame slot and
        literal loads) whose register is overwritten before any read on every path
        that follows: conditional branches go both ways and BRA runs its delay slot
        first. Calls, returns, other jumps and paths longer than budget instructions
        count as reads."""
        if self.pending: raise ValueError('deferred code outstanding')
        literal_at={offset for offset,_ in self.literals}
        words=[struct.unpack_from('<H',self.code,offset)[0] for offset in range(0,len(self.code),2)]
        target={offset//2:(kind,self.labels[name]//2) if name in self.labels else ('stop',None)
                for offset,kind,name in self.branches}
        def overwritten(start,n):
            seen=set(); paths=[start]; steps=0
            while paths:
                j=paths.pop()
                while True:
                    if j in seen: break  # a cycle with no read of Rn never reads it
                    if j>=len(words) or steps>budget: return False
                    seen.add(j); steps+=1
                    kind,to=target.get(j,(None,None))
                    if kind in ('bt','bf'): paths.append(to); j+=1; continue
                    if kind=='bra':
                        use=self._uses(words[j+1],n) if j+1<len(words) else 'read'
                        if use=='write': break
                        if use: return False
                        j=to; continue
                    use=self._uses(words[j],n)
                    if use=='write': break
                    if use: return False
                    j+=1
            return True
        dead=set()
        # Only labels a branch reaches join paths; the rest mark op boundaries.
        labels_at={self.labels[name] for _,_,name in self.branches if name in self.labels}
        # A reload of what the instruction just before stored, into the same
        # register, with no path joining between: MOV.L Rm,@(d,Rn) then
        # MOV.L @(d,Rn),Rm, and the float slot form MOV #d,R0; FMOV.S FRm,@(R0,Rn)
        # then MOV #d,R0; FMOV.S @(R0,Rn),FRm.
        def plain(i): return 2*i not in labels_at and not (i and self._delayed(words[i-1]))
        for i in range(1,len(words)):
            a,b=words[i-1],words[i]
            if a>>12==0x1 and b>>12==0x5 and b>>8&15==a>>4&15 and b>>4&15==a>>8&15 and \
               a&15==b&15 and a>>4&15!=a>>8&15 and plain(i) and plain(i-1):
                dead.add(2*i)
        for i in range(3,len(words)):
            c0,s,c1,l=words[i-3:i+1]
            if c0>>8==0xe0 and c1==c0 and s&0xf00f==0xf007 and l&0xf00f==0xf006 and \
               l>>8&15==s>>4&15 and l>>4&15==s>>8&15 and all(plain(j) for j in range(i-3,i+1)):
                dead.update((2*(i-1),2*i))
        for i,word in enumerate(words):
            top,n,m=word>>12,word>>8&15,word>>4&15
            if not (top==0xe or top==0x6 and word&15==3 and m!=n or top==0x5 and 10<=m<=14 or
                    top==0xd and 2*i in literal_at) or n==15 or i and self._delayed(words[i-1]): continue
            if overwritten(i+1,n): dead.add(2*i)
        # Copy propagation: MOV Rm,Rn whose Rm was last written, on the straight
        # path just before, by a write that can name its destination (a load,
        # MOV, extension, STS...) and is dead after the MOV: that write targets
        # Rn and the MOV goes. Nothing between may touch Rn or Rm.
        for i,word in enumerate(words):
            n,m=word>>8&15,word>>4&15
            if word&0xf00f!=0x6003 or m==n or n==15 or m==15 or 2*i in dead or 2*i in labels_at or                i and self._delayed(words[i-1]): continue
            k=i-1
            while k>=0 and 2*(k+1) not in labels_at:
                wk=words[k]; top,low=wk>>12,wk&15
                if 2*k not in dead and wk>>8&15==m and (top in (0xd,0xe) or top==0x5 or
                        top==0x6 and low not in (4,5,6) or wk&0xf0ff in (0x001a,0x005a,0x0029)):
                    if (k==0 or not self._delayed(words[k-1])) and overwritten(i+1,m):
                        words[k]=wk&0xf0ff|n<<8; struct.pack_into('<H',self.code,2*k,words[k]); dead.add(2*i)
                    break
                if self._uses(wk,m) or self._uses(wk,n): break
                k-=1
        if not dead: return 0
        ordered=sorted(dead)
        def moved(offset): return offset-2*bisect_left(ordered,offset)
        self.code=bytearray(b''.join(self.code[o:o+2] for o in range(0,len(self.code),2) if o not in dead))
        self.labels={name:moved(offset) for name,offset in self.labels.items()}
        self.branches=[(moved(offset),kind,target) for offset,kind,target in self.branches]
        self.literals=[(moved(offset),key) for offset,key in self.literals if offset not in dead]
        self.last_label=moved(self.last_label) if self.last_label>=0 else -1
        return len(dead)

    @staticmethod
    def _delayed(word):
        return (word>>12 in (0xa,0xb) or word in (0x000b,0x002b) or
                word&0xf0ff in (0x400b,0x402b,0x0003,0x0023) or word&0xff00 in (0x8d00,0x8f00))

    def _finish_islands(self,relax,literal_islands=True,far_branches=False,reuse=False):
        """Lay out pool skips and branch expansions together; preserve all delay slots."""
        literal_at=dict(self.literals)
        if len(literal_at)!=len(self.literals): raise ValueError('duplicate literal instruction')
        branch_at={offset:(kind,name) for offset,kind,name in self.branches}
        words={offset:struct.unpack_from('<H',self.code,offset)[0] for offset in range(0,len(self.code),2)}
        unsafe={offset+2 for offset,word in words.items() if self._delayed(word)}
        if any(offset in unsafe for offset in literal_at):
            raise ValueError('PC-relative literal in a delay slot is unsupported')
        if len(self.code) in unsafe: raise ValueError('missing branch delay slot')
        expanded=set(); far=set()
        while True:
            code=bytearray(); mapped={}; pending=[]; keys={}; relocations=[]; islands=0
            instruction_bytes=0; relative_jumps=[]
            def pool(skip):
                nonlocal islands,instruction_bytes
                if not pending: return
                if skip:
                    start=len(code); code.extend(b'\0\xa0\x09\x00')
                    islands+=1; instruction_bytes+=4
                if len(code)%4: code.extend(b'\x09\x00')
                locations={}
                for key in keys:
                    locations[key]=len(code)
                    if key[0]=='symbol':
                        relocations.append(dict(offset=len(code),symbol=key[1])); code.extend(b'\0'*4)
                    else: code.extend(struct.pack('<I',key[1]))
                for offset,key in pending:
                    delta=locations[key]-((offset+4)&~3)
                    if delta<0 or delta%4 or delta>1020: raise ValueError('literal island range exceeded')
                    original=struct.unpack_from('<H',code,offset)[0]
                    struct.pack_into('<H',code,offset,original|(delta//4))
                if skip:
                    displacement=(len(code)-(start+4))//2
                    if not 0<=displacement<=2047: raise ValueError('literal island skip range exceeded')
                    struct.pack_into('<H',code,start,0xa000|displacement)
                pending.clear(); keys.clear()
            for offset in range(0,len(self.code),2):
                # Bound the entire next pool against the earliest load. Twenty
                # bytes of margin cover the next expansion, load and delay slot.
                if pending and offset not in unsafe:
                    end=((len(code)+7)&~3)+4*len(keys)
                    earliest=(pending[0][0]+4)&~3
                    if literal_islands and end-earliest>=1000: pool(True)
                mapped[offset]=len(code)
                if offset in far:
                    kind,target=branch_at[offset]
                    conditional=kind in ('bt','bf')
                    if conditional:
                        inverse=0x8b00 if kind=='bt' else 0x8900
                        skip=len(code); code.extend(struct.pack('<H',inverse)); instruction_bytes+=2
                    elif kind!='bra' or words.get(offset+2)!=0x0009:
                        raise ValueError('far branch requires BRA with a NOP delay slot')
                    # GCC-compatible writable stack: preserve r0 in the BRAF
                    # delay slot, keep T/PR/r1 intact, and use a relative delta
                    # so copied native modules require no address relocation.
                    code.extend(struct.pack('<H',0x2f06))  # mov.l r0,@-r15
                    load=len(code); code.extend(b'\0\xd0')
                    jump=len(code); code.extend(struct.pack('<2H',0x0023,0x60f6))
                    instruction_bytes+=8
                    if len(code)%4: code.extend(b'\x09\x00')
                    data=len(code); code.extend(b'\0'*4)
                    struct.pack_into('<H',code,load,0xd000|((data-((load+4)&~3))//4))
                    relative_jumps.append((data,jump,target))
                    if conditional: struct.pack_into('<H',code,skip,inverse|((len(code)-(skip+4))//2))
                elif offset in expanded:
                    inverse=0x8b00 if branch_at[offset][0]=='bt' else 0x8900
                    code.extend(struct.pack('<3H',inverse|1,0xa000,0x0009)); instruction_bytes+=6
                else:
                    if offset in literal_at:
                        key=literal_at[offset]; pending.append((len(code),key)); keys[key]=None
                    code.extend(self.code[offset:offset+2]); instruction_bytes+=2
            mapped[len(self.code)]=len(code)
            pool(False)
            added=set(); added_far=set()
            # Trampolines: every BRA name; NOP already laid out (an expansion's,
            # or a plain one such as a return's BRA epilogue). An out-of-range
            # BT/BF to name may jump to one instead of expanding.
            # (site, distance left to the target; 0 for a far jump, which reaches it).
            trampolines={}
            for offset,kind,name in self.branches:
                target=mapped[self.labels[name]] if name in self.labels else None
                if offset in far:
                    site=mapped[offset]+(2 if kind in ('bt','bf') else 0); left=0
                elif offset in expanded: site=mapped[offset]+2; left=abs(target-site)
                elif kind=='bra' and words.get(offset+2)==0x0009 and offset not in unsafe:
                    site=mapped[offset]; left=abs(target-site)
                else: continue
                trampolines.setdefault(name,[]).append((site,left))
            planned={}  # name -> estimated trampoline sites added this pass
            def reach(name,position,low,high,closer=None):
                """The delta to the best trampoline in [low,high], or None. A BRA only
                moves to one closer to the target (or a far jump), so no cycles form."""
                best=None
                for site,left in trampolines.get(name,()):
                    delta=site-(position+4)
                    if low<=delta<=high and (closer is None or left<closer) and (best is None or left<best[1]):
                        best=(delta,left)
                return best and best[0]
            def soon(name,position,low,high):
                return any(low+32<=site-(position+4)<=high-32 for site in planned.get(name,()))
            for offset,kind,name in self.branches:
                if name not in self.labels: raise ValueError('undefined label')
                if offset in far: continue
                position=mapped[offset]+(2 if offset in expanded else 0)
                delta=mapped[self.labels[name]]-(position+4)
                if delta%2: raise ValueError('unaligned branch')
                if kind in ('bt','bf') and offset not in expanded:
                    if not -256<=delta<=254 and relax and reuse:
                        found=reach(name,position,-256,254)
                        if found is not None: delta=found
                        elif soon(name,position,-256,254): continue
                    if not -256<=delta<=254:
                        if relax:
                            added.add(offset); planned.setdefault(name,[]).append(mapped[offset]+2); continue
                        raise ValueError('branch range exceeded')
                    bits=8
                else: bits=12
                displacement=delta//2
                if not -(1<<(bits-1))<=displacement<(1<<(bits-1)):
                    if far_branches and kind in ('bra','bt','bf'):
                        if reuse:
                            found=reach(name,position,-4096,4094,abs(mapped[self.labels[name]]-position))
                            if found is not None:
                                delta=found; displacement=delta//2
                                original=struct.unpack_from('<H',code,position)[0]
                                struct.pack_into('<H',code,position,original|(displacement&0xfff))
                                continue
                            if soon(name,position,-4096,4094): continue
                            planned.setdefault(name,[]).append(mapped[offset]+(2 if kind in ('bt','bf') else 0))
                        added_far.add(offset); continue
                    raise ValueError('branch range exceeded')
                original=struct.unpack_from('<H',code,position)[0]
                struct.pack_into('<H',code,position,original|(displacement&((1<<bits)-1)))
            if added or added_far:
                expanded.update(added); far.update(added_far); continue
            for data,jump,name in relative_jumps:
                delta=mapped[self.labels[name]]-(jump+4)
                if not -(1<<31)<=delta<(1<<31): raise ValueError('far branch relative delta range exceeded')
                struct.pack_into('<i',code,data,delta)
            return bytes(code),relocations,dict(instruction_bytes=instruction_bytes,
                expanded_conditional_branches=len(expanded),literal_islands=islands,far_branches=len(far))

    def finish_with_stats(self,relax=False,literal_islands=False,far_branches=False):
        try: result=self._finish_terminal(relax)
        except ValueError as error:
            if not ((literal_islands and str(error)=='literal pool range exceeded') or
                    (far_branches and str(error)=='branch range exceeded')): raise
            result=self._finish_islands(relax,literal_islands,far_branches)
        if relax and result[2]['expanded_conditional_branches'] and not os.environ.get('DC_SH4_NO_TRAMPOLINES'):
            # Out-of-range BT/BF reuse BRAs to the same target where one is near.
            reused=self._finish_islands(relax,literal_islands,far_branches,reuse=True)
            if len(reused[0])<len(result[0]): return reused
        return result

    def finish(self,relax=False,literal_islands=False,far_branches=False):
        return self.finish_with_stats(relax,literal_islands,far_branches)[:2]

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
