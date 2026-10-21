"""Native 64-bit shifts using SH-4 register halves (AI-assisted)."""
def emit_shift64(e,kind,count=None,prefix='shift'):
    """R0:R1 value, R2 variable count; clobbers R2/R3/R4 and T.

    The original x86 backend masks counts to six bits. SH-4 shifts operate on
    32-bit halves, so cross-half bits and the 32..63 cases are explicit.
    Constant counts emit only their selected path, with no helper calls.
    """
    if kind not in ('left','logical','arithmetic'): raise ValueError('invalid shift kind')
    if count is not None and (not isinstance(count,int) or not 0<=count<=255):
        raise ValueError('constant shift requires original unsigned-byte amount')
    n=None if count is None else count&63
    right=0x400c if kind=='arithmetic' else 0x400d  # SHAD / SHLD
    done,wide=prefix+'_done',prefix+'_wide'
    if n==0: return
    if n is None:
        e.immediate(3,63); e.rr(0x2009,2,3)
        e.rr(0x2008,2,2); e.branch('bt',done)
        e.immediate(3,32); e.rr(0x2008,2,3); e.branch('bf',wide)
    if n is None or n<32:
        if n is not None: e.immediate(2,n)
        if kind=='left':
            e.mov(3,0); e.mov(4,2); e.add_immediate(4,-32)
            e.rr(0x400d,3,4); e.rr(0x400d,1,2); e.rr(0x200b,1,3); e.rr(0x400d,0,2)
        else:
            e.mov(3,1); e.immediate(4,32); e.sub(4,2); e.rr(0x400d,3,4)
            e.neg(4,2); e.rr(0x400d,0,4); e.rr(0x200b,0,3); e.rr(right,1,4)
    if n is None: e.branch('bra',done); e.label(wide)
    if n is None or n>=32:
        if n is None: e.add_immediate(2,-32)
        elif n!=32: e.immediate(2,n-32)
        if kind=='left':
            e.mov(1,0)
            if n!=32: e.rr(0x400d,1,2)
            e.immediate(0,0)
        else:
            e.mov(0,1)
            if n!=32: e.neg(2,2); e.rr(right,0,2)
            if kind=='arithmetic': e.immediate(2,-31); e.rr(0x400c,1,2)
            else: e.immediate(1,0)
    if n is None: e.label(done)

def shift_return_leaf(function):
    """Coalesce copy-only one-shift returns directly through the GCC registers."""
    from sh4_backend import Emitter
    if function.get('return_type') in ('float','none'): return None
    registers={r['id']:r for r in function['registers']}
    origin={p['register']:('parameter',i) for i,p in enumerate(function['parameters'])}
    result=None; shifts=0
    for op in function['instructions']:
        kind=op['kind']; reads,writes=op['reads'],op['writes']
        if kind=='nop': continue
        if result is not None: return None
        if any(registers[r]['class']!='gpr64' for r in reads+writes): return None
        if kind=='move' and len(reads)==len(writes)==1:
            if reads[0] not in origin: return None
            origin[writes[0]]=origin[reads[0]]
        elif kind in ('shl64','shr64','sar64','shlv64','shrv64','sarv64'):
            variable=kind in ('shlv64','shrv64','sarv64')
            if shifts or len(reads)!=(2 if variable else 1) or len(writes)!=1: return None
            inputs=[origin.get(r) for r in reads]
            if any(value is None or value[0]!='parameter' or value[1]>=2 for value in inputs): return None
            count=None if variable else op.get('shift')
            if not variable and (not isinstance(count,int) or not 0<=count<=255): return None
            origin[writes[0]]=('shift',kind[:3],inputs[0][1],inputs[1][1] if variable else None,count)
            shifts+=1
        elif kind=='return' and len(reads)==1:
            result=origin.get(reads[0])
        else: return None
    if result is None or result[0]!='shift': return None
    _,kind,value_index,count_index,count=result
    low,high=4+value_index*2,5+value_index*2
    e=Emitter()
    if count is not None and count&63==32:
        if kind=='shl': e.immediate(0,0); e.word(0x000b); e.mov(1,low)
        elif kind=='shr': e.immediate(1,0); e.word(0x000b); e.mov(0,high)
        else:
            e.mov(0,high); e.mov(1,high); e.immediate(2,-31); e.word(0x000b); e.rr(0x400c,1,2)
    else:
        e.mov(0,low); e.mov(1,high)
        if count is None: e.mov(2,4+count_index*2)
        emit_shift64(e,{'shl':'left','shr':'logical','sar':'arithmetic'}[kind],count,'leaf_shift')
        e.ret()
    data,relocations=e.finish()
    return data,{'name':function['name'],'bytes':len(data),'stack_bytes':0,
                 'relocations':relocations,'optimization':'coalesced_shift_return',
                 'instruction_stream_bytes':len(e.code),'literal_pool_and_alignment_bytes':0,
                 'baseline_stack_bytes':len(registers)*8+16,
                 'minimum_32_byte_cache_lines':(len(data)+31)//32}
