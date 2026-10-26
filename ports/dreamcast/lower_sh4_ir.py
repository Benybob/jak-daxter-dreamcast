"""Lower a strictly supported subset of original typed GOAL IR to SH-4 (AI-assisted).

Bootstrap allocation uses eight-byte stack slots except coalesced return leaves.
This is native code generation, without a C transpiler or interpreter. Unknown
operations/types fail closed. Full game/runtime support remains future work.
"""
import argparse
import json
from pathlib import Path
import re
import struct
from sh4_backend import Emitter
from shift_sh4 import emit_shift64,shift_return_leaf
from division_sh4 import emit_div32

# GCC -m4-single -ml places singles in swapped pairs (verified with GCC).
FLOAT_ARGUMENT_REGISTERS=(5,4,7,6,9,8,11,10)
INTEGER_VALUE_TYPES=frozenset(('int','uint','integer'))
TAGGED_VALUE_TYPES=frozenset(('object','basic','symbol','type','string','pair','function','pointer'))

def native_identifiers(names):
    """Readable legacy spellings when unique; reversible UTF-8 names otherwise.

    Group case-insensitively because generated offset macros use uppercase.
    The encoded prefix is reserved even for otherwise valid source names.
    Native bindings and relocations retain their original GOAL symbol strings.
    """
    names=list(names)
    if any(not isinstance(name,str) or not name or '\0' in name for name in names):
        raise ValueError('function identifier requires a nonempty name')
    if len(names)!=len(set(names)): raise ValueError('duplicate identifier name')
    prefix='_dc_utf8_'
    bases={name:re.sub('[^A-Za-z0-9_]','_',name) for name in names}
    counts={}
    for base in bases.values(): counts[base.upper()]=counts.get(base.upper(),0)+1
    result={}
    for name,base in bases.items():
        result[name]=(prefix+name.encode('utf-8').hex() if counts[base.upper()]>1 or
                      base[0].isdigit() or base.lower().startswith(prefix) else base)
    if len({name.upper() for name in result.values()})!=len(names):
        raise ValueError('ambiguous native identifiers')
    return result

def scalar_kind(name,runtime):
    # Function signatures describe tagged native addresses, not a wider value.
    if runtime and name and name.startswith('(function ') and name.endswith(')'):
        return 'function'
    if runtime and name and name.startswith('(pointer ') and name.endswith(')'):
        return 'pointer'
    return name

def argument_return_leaf(function):
    """Coalesce copy-only argument returns, including the useful RTS delay slot."""
    registers={r['id']:r for r in function['registers']}
    origin={p['register']:i for i,p in enumerate(function['parameters'])}
    result=None
    for op in function['instructions']:
        if op['kind']=='nop': continue
        if result is not None: return None
        if op['kind']=='move' and len(op['reads'])==len(op['writes'])==1:
            src,dst=op['reads'][0],op['writes'][0]
            if src not in origin or registers[src]['class']!=registers[dst]['class']: return None
            origin[dst]=origin[src]
        elif op['kind'] in ('shl64','shr64','sar64') and len(op['reads'])==len(op['writes'])==1 and \
             isinstance(op.get('shift'),int) and 0<=op['shift']<=255 and op['shift']&63==0:
            src,dst=op['reads'][0],op['writes'][0]
            if src not in origin or registers[src]['class']!='gpr64' or registers[dst]['class']!='gpr64': return None
            origin[dst]=origin[src]
        elif op['kind']=='return' and len(op['reads'])==1:
            if op['reads'][0] not in origin: return None
            result=origin[op['reads'][0]]
        else: return None
    if result is None: return None
    floating=function['return_type']=='float'
    if (function['parameters'][result]['type']=='float')!=floating: return None
    result=sum((p['type']=='float')==floating for p in function['parameters'][:result])
    e=Emitter()
    stacked=not floating and result>=2
    if floating:
        e.word(0x000b); e.fmov(0,FLOAT_ARGUMENT_REGISTERS[result])
    elif stacked:
        offset=(result-2)*8
        # No prologue: incoming GCC stack slots remain relative to R15.
        # MOV.L displacement is an unsigned word index (0..15).
        if offset>56: return None
        if offset: e.word(0x50f0|(offset//4))
        else: e.load(0,15)
        e.word(0x000b)
        e.word(0x51f0|((offset+4)//4))  # useful RTS delay slot
    else:
        e.mov(0,4+result*2); e.word(0x000b); e.mov(1,5+result*2)
    data,relocations=e.finish()
    return data,{'name':function['name'],'bytes':len(data),'stack_bytes':0,
                 'relocations':relocations,'optimization':'coalesced_stack_argument_return' if stacked else 'coalesced_argument_return',
                 'instruction_stream_bytes':len(e.code),'literal_pool_and_alignment_bytes':0,
                 'minimum_32_byte_cache_lines':(len(data)+31)//32}

def validate_definitions(function,registers):
    """Check definite assignment along reachable paths, including loop edges."""
    ops=function['instructions']; count=len(ops)
    if not count: return  # the original frontend can emit an empty (none) body
    successors=[]
    for index,op in enumerate(ops):
        ids=set(op['reads'])|set(op['writes'])
        if not ids<=registers: raise ValueError('IR references unknown virtual register')
        edges=[] if op['kind']=='return' else ([index+1] if index+1<count else [])
        if op['kind'] in ('goto','branch'):
            target=op['target']
            if not isinstance(target,int) or not 0<=target<count: raise ValueError('invalid IR branch target')
            edges=[target] if op['kind']=='goto' else edges+[target]
        successors.append(edges)
    reachable=set(); pending=[0]
    while pending:
        node=pending.pop()
        if node in reachable: continue
        reachable.add(node); pending.extend(successors[node])
    predecessors={node:[] for node in reachable}
    for node in reachable:
        for target in successors[node]: predecessors[target].append(node)
    inputs={node:set(registers) for node in reachable}
    outputs={node:set(registers) for node in reachable}
    entry={p['register'] for p in function['parameters']}
    if not entry<=registers: raise ValueError('unknown incoming argument register')
    changed=True
    while changed:
        changed=False
        for node in sorted(reachable):
            sources=[outputs[p] for p in predecessors[node]]
            if node==0: sources.append(entry)
            before=set.intersection(*sources)
            # ValueReset/Null are allocation markers, not initialization.
            writes=set() if ops[node]['kind']=='nop' else set(ops[node]['writes'])
            after=before|writes
            if before!=inputs[node] or after!=outputs[node]: changed=True
            inputs[node]=before; outputs[node]=after
    for node in sorted(reachable):
        if not successors[node] and ops[node]['kind']!='return' and function.get('return_type')!='none':
            raise ValueError('value-returning function has a reachable fallthrough without a return')
        missing=set(ops[node]['reads'])-inputs[node]
        if missing: raise ValueError(f'IR {node} reads uninitialized virtual registers {sorted(missing)}')

def allocate_slots(function):
    """Reuse eight-byte slots using CFG liveness, including loop/call edges."""
    ops=function['instructions']; count=len(ops)
    successors=[]
    for i,op in enumerate(ops):
        edges=[] if op['kind']=='return' else ([i+1] if i+1<count else [])
        if op['kind'] in ('goto','branch'):
            edges=[op['target']] if op['kind']=='goto' else edges+[op['target']]
        successors.append(edges)
    reachable=set(); pending=[0]
    while pending:
        i=pending.pop()
        if i not in reachable: reachable.add(i); pending.extend(successors[i])
    before={i:set() for i in reachable}; after={i:set() for i in reachable}
    changed=True
    while changed:
        changed=False
        for i in sorted(reachable,reverse=True):
            op=ops[i]
            live=set().union(*(before[n] for n in successors[i]))
            writes=set(op['writes']) if op['kind']!='nop' else set()
            incoming=set(op['reads'])|(live-writes)
            if live!=after[i] or incoming!=before[i]: changed=True
            after[i],before[i]=live,incoming
    graph={r['id']:set() for r in function['registers']}
    def edge(a,b):
        if a!=b: graph[a].add(b); graph[b].add(a)
    def clique(values):
        values=list(values)
        for i,a in enumerate(values):
            for b in values[i+1:]: edge(a,b)
    # Entry stores happen before IR, including copies of otherwise unused args.
    clique(p['register'] for p in function['parameters'])
    for i in reachable:
        op=ops[i]; writes=op['writes'] if op['kind']!='nop' else []
        clique(op['reads']); clique(writes)
        for value in writes:
            for live in after[i]: edge(value,live)
    colors={}
    for value in sorted(graph,key=lambda r:(-len(graph[r]),r)):
        occupied={colors[n] for n in graph[value] if n in colors}
        color=0
        while color in occupied: color+=1
        colors[value]=color
    return {value:color*8 for value,color in colors.items()}

LEXICAL_SCOPE_KINDS=('process_scope_enter','process_scope_leave')

def normalize_register_asm(function):
    """Read explicit x86 (.mov a b)/(.add a b) asm ops as plain 64-bit IR (AI-assisted).

    The original inline-asm forms between general registers are exactly a
    64-bit register copy and a 64-bit add; only their operand layout is checked.
    Every other asm op stays unsupported.
    """
    ops=[]
    for op in function['instructions']:
        text=op.get('text','').strip()
        if op['kind']=='unsupported' and text.startswith('.mov ') and len(op['reads'])==1 and len(op['writes'])==1:
            op=dict(op,kind='move')
        elif op['kind']=='unsupported' and text.startswith('.add ') and len(op['reads'])==2 and \
             len(op['writes'])==1 and op['reads'][0]==op['writes'][0]:
            op=dict(op,kind='add64')
        ops.append(op)
    return dict(function,instructions=ops)

STATE_CODE=12          # state code
MAIN_THREAD=40         # process main-thread
THREAD_STACK_TOP=28    # thread stack-top
CARG_REGISTERS=('rdi','rsi','rdx','rcx')

def state_code_idiom(function):
    """Recognize original enter-state's x86 jump into a new state's code (AI-assisted).

    The desktop branch loads carg0-3, resets rsp to the main thread's stack top,
    pushes return-from-thread-dead and jumps to (-> new-state code). It becomes
    one explicit state_code_transfer operation reading the code and the four
    arguments; the machine-register bindings it used disappear. Any deviation,
    or any other use of those registers, keeps refusing.
    """
    ops=function['instructions']
    roles={b.get('frontend_register'):b['register'] for b in function.get('fixed_registers',[])
           if b.get('role')=='unsupported'}
    if not all(name in roles for name in ('rsp','r15')+CARG_REGISTERS):return function
    jumps=[p for p,op in enumerate(ops) if op['kind']=='unsupported' and op.get('text','').startswith('.jr ')]
    if len(jumps)!=1:return function
    jr=jumps[0];offset=roles['r15'];sp=roles['rsp']
    targets={op['target'] for op in ops if op['kind'] in ('branch','goto')}
    def straight(begin,end):
        return all(not begin<t<=end for t in targets) and \
               all(ops[q]['kind'] not in ('branch','goto','call','return') for q in range(begin,end))
    def writer(value,before):
        for q in range(before-1,-1,-1):
            if value in ops[q]['writes']:return q
        return None
    def offset_add(value,before):
        """value = <base> + r15, returning the base's defining position."""
        q=writer(value,before)
        if q is None or ops[q]['kind']!='add64' or ops[q]['reads']!=[value,offset] or ops[q]['writes']!=[value]:return None,None
        m=writer(value,q)
        if m is None or ops[m]['kind']!='move':return None,None
        return m,ops[m]['reads'][0]
    push=jr-1
    if ops[push]['kind']!='unsupported' or not ops[push].get('text','').startswith('.push ') or len(ops[push]['reads'])!=1:
        return function
    ret_move,ret_base=offset_add(ops[push]['reads'][0],push)
    if ret_move is None:return function
    ret_def=writer(ret_base,ret_move)
    if ret_def is None or ops[ret_def]['kind']!='symbol_value' or ops[ret_def].get('symbol')!='return-from-thread-dead':
        return function
    code_reg=ops[jr]['reads'][0] if len(ops[jr]['reads'])==1 else None
    code_move,code_base=offset_add(code_reg,jr)
    if code_move is None:return function
    code_def=writer(code_base,code_move)
    if code_def is None or ops[code_def]['kind']!='load_memory' or ops[code_def].get('offset')!=STATE_CODE:return function
    sp_move,sp_base=offset_add(sp,jr)
    if sp_move is None:return function
    sp_def=writer(sp_base,sp_move)
    if sp_def is None or ops[sp_def]['kind']!='load_memory' or ops[sp_def].get('offset')!=THREAD_STACK_TOP:return function
    thread_def=writer(ops[sp_def]['reads'][0],sp_def)
    if thread_def is None or ops[thread_def]['kind']!='load_memory' or ops[thread_def].get('offset')!=MAIN_THREAD:return function
    arguments=[];start=jr
    for name in CARG_REGISTERS:
        q=writer(roles[name],jr)
        if q is None or ops[q]['kind']!='move' or len(ops[q]['reads'])!=1:return function
        arguments.append(ops[q]['reads'][0]);start=min(start,q)
    start=min(start,code_def,ret_def,thread_def)
    # The two dead (.add carg) keep-alive ops follow the jump; nothing else may.
    dead=[jr+1,jr+2]
    if any(len(ops)<=q or ops[q]['kind']!='add64' or ops[q]['writes'][0] not in [roles[n] for n in CARG_REGISTERS]
           for q in dead):return function
    if not straight(start,jr):return function
    machine={roles[n] for n in ('rsp',)+CARG_REGISTERS}
    rewritten=[dict(op) for op in ops]
    for q in range(start,jr+3):
        if q in (code_def,thread_def,sp_def,ret_def):continue
        op=ops[q]
        if op['kind']=='nop':continue
        if not (set(op['writes'])|set(op['reads']))&(machine|{offset}) and q not in (code_move,ret_move,sp_move,push,jr):
            return function  # unrelated work inside the transfer window
        rewritten[q]=dict(kind='nop',reads=[],writes=[],text=op.get('text',''))
    rewritten[jr]=dict(kind='state_code_transfer',reads=[code_base]+arguments,writes=[],text=ops[jr].get('text',''))
    if any((set(op['reads'])|set(op['writes']))&machine for op in rewritten):
        return function  # the machine registers have no other native meaning
    return dict(function,instructions=rewritten,
                fixed_registers=[b for b in function['fixed_registers'] if b['register'] not in machine])

def return_from_thread_idiom(function):
    """Recognize original x86 (+! temp off) (.push temp) (.ret) to return-from-thread.

    The desktop branch jumps to the kernel's return-from-thread through the
    r15 memory offset. It becomes one explicit native transfer operation and the
    offset binding is dropped; any other use of r15 or of the jump still refuses.
    (AI-assisted)
    """
    ops=function['instructions']
    offsets=[b for b in function.get('fixed_registers',[])
             if b.get('frontend_register')=='r15' and b.get('role')=='unsupported']
    pushes=[p for p,op in enumerate(ops) if op['kind']=='unsupported' and op.get('text','').startswith('.push ')]
    if len(offsets)!=1 or not pushes: return function
    offset=offsets[0]['register']
    rewritten=[dict(op) for op in ops]
    targets={op['target'] for op in ops if op['kind'] in ('branch','goto')}
    for push in pushes:
        following=[p for p in range(push+1,len(ops)) if ops[p]['kind']!='nop']
        if not following or ops[following[0]]['kind']!='unsupported' or ops[following[0]].get('text','').strip()!='.ret':
            return function
        if len(ops[push]['reads'])!=1: return function
        # Straight-line backward scan: moves, one offset add, then the address.
        value,add,base=ops[push]['reads'][0],None,None
        for position in range(push-1,-1,-1):
            op=ops[position]
            if op['kind'] in ('branch','goto','call','return'): return function
            if value not in op['writes']: continue
            if op['writes']!=[value]: return function
            if op['kind']=='move': value=op['reads'][0]
            elif op['kind']=='add64' and add is None and op['reads']==[value,offset]: add=position
            elif op['kind'] in ('symbol_value','function_address') and add is not None and \
                 op.get('symbol',op.get('function'))=='return-from-thread':
                base=position; break
            else: return function
        if base is None or any(base<t<=push for t in targets): return function
        rewritten[add]=dict(kind='nop',reads=[],writes=[],text='native return-from-thread offset')
        rewritten[push]=dict(kind='return_from_thread',reads=[],writes=[],text=ops[push].get('text',''))
        rewritten[following[0]]=dict(kind='nop',reads=[],writes=[],text='.ret')
    if any(offset in op['reads'] or offset in op['writes'] for op in rewritten):
        return function
    return dict(function,instructions=rewritten,
                fixed_registers=[b for b in function['fixed_registers'] if b['register']!=offset])

def lexical_process_bracket(function,index,binding):
    """Prove one original (old=pp; pp=X; ...; pp=old) bracket (AI-assisted).

    The entry value comes from the authenticated getter. The single assignment
    becomes an explicit lexical scope entry and the restore its matching leave.
    The region between them must be single-entry/single-exit with no return,
    so every normal path that enters also leaves. Anything else refuses.
    """
    ops=[dict(op) for op in function['instructions']]
    for position,op in enumerate(ops):
        if op['kind']=='fixed_register_reset' and index in op['writes']:
            if op['reads'] or op['writes']!=[index] or op.get('bindings')!=[binding]:
                raise ValueError('unsupported lexical process reset layout')
            if any(o['kind'] in ('branch','goto','call') or index in o['reads'] or index in o['writes']
                   for o in ops[:position]):
                raise ValueError('lexical process reset must precede every process use and control transfer')
            ops[position]=dict(kind='nop',reads=[],writes=[],text=op.get('text','entry pp reset'))
    writes=[p for p,op in enumerate(ops) if index in op['writes']]
    if not writes:
        # Only entry resets: an authenticated read-only context, no bracket.
        return dict(function,instructions=ops)
    if len(writes)!=2:
        raise ValueError('lexical process bracket requires exactly one assignment and one restore')
    enter,leave=writes
    for position in writes:
        if ops[position]['kind']!='move' or len(ops[position]['reads'])!=1 or ops[position]['writes']!=[index]:
            raise ValueError('lexical process assignment must be a plain move')
    target,saved=ops[enter]['reads'][0],ops[leave]['reads'][0]
    if index in (target,saved) or target==saved:
        raise ValueError('lexical process assignment must not restore its own entry value')
    saved_writers=[p for p,op in enumerate(ops) if saved in op['writes']]
    if len(saved_writers)!=1 or not saved_writers[0]<enter or ops[saved_writers[0]]['kind']!='move' or \
       ops[saved_writers[0]]['reads']!=[index]:
        raise ValueError('lexical process restore must use a single saved copy of the entry value')
    for position,op in enumerate(ops):
        inside=enter<position<leave
        if inside and op['kind']=='return':
            raise ValueError('return inside lexical process bracket is unsupported')
        if op['kind'] in ('branch','goto'):
            lands_inside=isinstance(op['target'],int) and enter<op['target']<=leave
            if inside and not lands_inside:
                raise ValueError('control transfer leaves lexical process bracket')
            if not inside and position!=leave and lands_inside:
                raise ValueError('control transfer enters lexical process bracket')
    ops[enter]=dict(ops[enter],kind='process_scope_enter')
    ops[leave]=dict(ops[leave],kind='process_scope_leave')
    return dict(function,instructions=ops,lexical_saved=saved)

SUSPEND_TOP_THREAD=44  # process top-thread
SUSPEND_HOOK=12        # thread suspend-hook

def native_machine_registers(function):
    """Give the desktop rsp/r15 roles their exact native meaning (AI-assisted).

    r15 is the x86 GOAL memory base; native SH-4 GOAL pointers are absolute, so a
    read-only r15 is the constant 0. A reset rsp reads the native stack pointer.
    Both must be integer containers; r15 must never be written, rsp only reset.
    """
    keep=[];ops=[dict(op) for op in function['instructions']];entry=[]
    registers={r['id']:r for r in function['registers']}
    for binding in function.get('fixed_registers',[]):
        index,name=binding['register'],binding.get('frontend_register')
        if binding.get('role')!='unsupported' or name not in ('r15','rsp') or \
           registers.get(index,{}).get('class')!='gpr64' or registers[index].get('abi_type',registers[index]['type']) not in ('int','uint'):
            keep.append(binding);continue
        writers=[p for p,op in enumerate(ops) if index in op['writes']]
        if name=='r15':
            if writers:keep.append(binding);continue
            entry.append(dict(kind='constant',value=0,reads=[],writes=[index],text='native r15 memory offset is zero'))
        else:
            if not all(ops[p]['kind']=='fixed_register_reset' and ops[p]['writes']==[index] and not ops[p]['reads']
                       for p in writers):
                keep.append(binding);continue
            for p in writers:ops[p]=dict(kind='stack_pointer',reads=[],writes=[index],text='native stack pointer')
    if not entry and len(keep)==len(function.get('fixed_registers',[])):return function
    if entry:
        ops=[dict(op,target=op['target']+len(entry)) if op['kind'] in ('branch','goto') and isinstance(op.get('target'),int) else op
             for op in ops]
        ops=entry+ops
        shared=function.get('shared_exit')
        if shared:function=dict(function,shared_exit=dict(shared,exit_ir=shared['exit_ir']+len(entry)))
    return dict(function,instructions=ops,fixed_registers=keep)

def suspend_idiom(function):
    """Recognize original (suspend): pp=top-thread; (suspend-hook 0) (AI-assisted).

    The original saves the thread and returns to the kernel, which restores pp
    on resume. Natively the current fiber yields, and pp keeps the authenticated
    process, so the thread-typed write and hook load disappear. Each pp reset
    re-reads the authenticated process. Anything else keeps refusing.
    """
    bindings=[b for b in function.get('fixed_registers',[])
              if b.get('frontend_register')=='r13' and b.get('role')=='unsupported']
    if len(bindings)!=1:return function
    index=bindings[0]['register'];ops=[dict(op) for op in function['instructions']]
    reads={};writes={}
    for p,op in enumerate(ops):
        for v in op['reads']:reads.setdefault(v,[]).append(p)
        for v in op['writes']:writes.setdefault(v,[]).append(p)
    def only(value,at):return reads.get(value,[])==[at] and len(writes.get(value,[]))==1
    rewritten=0
    for p,op in enumerate(ops):
        if index not in op['writes'] or op['kind']=='fixed_register_reset':continue
        # pp = (-> pp top-thread)
        if op['kind']!='move' or len(writes.get(op['reads'][0],[]))!=1:return function
        load=ops[writes[op['reads'][0]][0]]
        if load['kind']!='load_memory' or load.get('offset')!=SUSPEND_TOP_THREAD or load.get('size')!=4:return function
        calls=[q for q in range(p+1,len(ops)) if ops[q]['kind'] not in ('nop','constant','move','load_memory')]
        if not calls or ops[calls[0]]['kind']!='call':return function
        c=calls[0];call=ops[c]
        if len(call['reads'])!=2 or call['writes'] or call.get('abi',{}).get('parameters')!=['cpu-thread'] or \
           call['abi'].get('return_type')!='none':return function
        def chain(value):
            """Single-writer move chain inside (p,c) back to its defining op."""
            path=[]
            while True:
                sites=writes.get(value,[])
                if len(sites)!=1 or not p<sites[0]<c:return None,path
                path.append(sites[0])
                if ops[sites[0]]['kind']!='move':return sites[0],path
                value=ops[sites[0]]['reads'][0]
        hook,hook_path=chain(call['reads'][0])
        if hook is None or ops[hook]['kind']!='load_memory' or ops[hook].get('offset')!=SUSPEND_HOOK:return function
        base,base_path=chain(ops[hook]['reads'][0])
        if base_path!=[] and (base is not None or ops[base_path[-1]]['reads']!=[index]):return function
        if not base_path:return function
        zero,zero_path=chain(call['reads'][1])
        if zero is None or ops[zero]['kind']!='constant' or ops[zero].get('value')!=0:return function
        local=set(hook_path)|set(base_path)|set(zero_path)
        # Every value defined in the window is consumed only inside it.
        for q in range(p+1,c):
            if ops[q]['kind']=='nop':continue
            if q not in local or index in ops[q]['writes']:return function
            for v in ops[q]['writes']:
                if any(not p<r<=c for r in reads.get(v,[])):return function
        targets={o['target'] for o in ops if o['kind'] in ('branch','goto')}
        if any(p<t<=c for t in targets):return function
        for q in [p]+sorted(local):ops[q]=dict(kind='nop',reads=[],writes=[],text=ops[q].get('text',''))
        ops[c]=dict(kind='suspend',reads=[],writes=[],text=call.get('text',''))
        rewritten+=1
    if not rewritten:return function
    for p,op in enumerate(ops):
        if index in op['writes']:
            if op['kind']!='fixed_register_reset' or op['writes']!=[index] or op['reads']:return function
            ops[p]=dict(kind='process_reread',reads=[],writes=[index],text='pp restored after resume')
    return dict(function,instructions=ops,
                fixed_registers=[b for b in function['fixed_registers'] if b['register']!=index],
                suspend_process=index)

def compile_function(function,module=None,runtime=False,optimize=True,statics=None):
    semantic_types={r['id']:r['type'] for r in function['registers']}
    if runtime:
        # Preserve semantic types in exported IR, use original-type-system ABI
        # classifications locally rather than guessing from user type names.
        function=dict(function,
            return_type=function.get('return_abi_type',function.get('return_type')),
            registers=[dict(r,type=r.get('abi_type',r['type'])) for r in function['registers']],
            parameters=[dict(p,type=p.get('abi_type',p['type'])) for p in function['parameters']])
    if function.get('asm'): raise ValueError('inline/assembly functions unsupported')
    if runtime:
        function=normalize_register_asm(function)
        function=state_code_idiom(function)
        function=return_from_thread_idiom(function)
        function=native_machine_registers(function)
        function=suspend_idiom(function)
    transfers=any(op['kind'] in ('return_from_thread','suspend','process_reread','state_code_transfer')
                  for op in function['instructions'])
    implicit=function.get('implicit_parameters',[])
    if implicit and (not runtime or len(implicit)!=1 or function['parameters'] or
                     implicit[0].get('kind')!='fixed_entry' or implicit[0].get('name')!='self' or
                     implicit[0].get('type')!='process' or implicit[0].get('abi_type')!='object' or
                     function.get('return_type')!='object'):
        raise ValueError('implicit process/self entry ABI not implemented for this signature')
    value_types=INTEGER_VALUE_TYPES|(TAGGED_VALUE_TYPES if runtime else frozenset())
    void_return=runtime and function.get('return_type')=='none'
    if not void_return and scalar_kind(function.get('return_type'),runtime) not in value_types|{'float'}:
        raise ValueError('bootstrap ABI requires a numeric return type')
    if not void_return and not any(op['kind']=='return' for op in function['instructions']):
        raise ValueError('numeric function has no return')
    for op in function['instructions']:
        if op['kind']=='return' and len(op['reads'])!=(0 if void_return else 1):
            raise ValueError('return value does not match function return type')
    registers={r['id']:r for r in function['registers']}
    if len(registers)!=len(function['registers']): raise ValueError('duplicate virtual register')
    fixed={}; context_reads=[]; lexical=[]
    for binding in function.get('fixed_registers',[]):
        index=binding['register']
        if index in fixed or index not in registers: raise ValueError('invalid fixed register metadata')
        if not runtime: raise ValueError('fixed runtime register requires native runtime profile')
        process_self=(binding.get('frontend_register')=='r13' and binding.get('role')=='process_self' and
                      implicit and implicit[0]['register']==index)
        context_read=(runtime and binding.get('frontend_register')=='r13' and
                      binding.get('role')=='process_context_read' and not implicit)
        lexical_role=(runtime and binding.get('frontend_register')=='r13' and
                      binding.get('role')=='process_lexical' and not implicit)
        if not process_self and not context_read and not lexical_role and \
           (binding.get('frontend_register')!='r14' or binding.get('role')!='symbol_table'):
            raise ValueError('fixed runtime register role not implemented')
        if lexical_role:
            if semantic_types[index]!='process' or registers[index]['class']!='gpr64' or registers[index]['type']!='object':
                raise ValueError('lexical process bracket requires an exact process container')
            if index in {p['register'] for p in function['parameters']}:
                raise ValueError('lexical process bracket must not alias an argument')
            lexical.append(index)
        elif context_read:
            if semantic_types[index]!='process' or registers[index]['class']!='gpr64' or registers[index]['type']!='object':
                raise ValueError('read-only process context requires an exact process container')
            if index in {p['register'] for p in function['parameters']} or not any(index in op['reads'] for op in function['instructions']):
                raise ValueError('read-only process context must be a separate read input')
            context_reads.append(index)
        elif process_self:
            if registers[index]['class']!='gpr64' or registers[index]['type']!='object':
                raise ValueError('implicit self requires a process reference container')
        elif registers[index]['class']!='gpr64' or registers[index]['type'] not in ('uint','int','integer'):
            raise ValueError('fixed symbol-table register requires an integer container')
        fixed[index]=binding
    if len(context_reads)>1:
        raise ValueError('ambiguous read-only process context')
    if len(lexical)>1 or (lexical and context_reads):
        raise ValueError('ambiguous lexical process context')
    if implicit and (implicit[0]['register'] not in fixed or
                     fixed[implicit[0]['register']].get('role')!='process_self'):
        raise ValueError('implicit self is missing its fixed entry declaration')
    if lexical:
        function=lexical_process_bracket(function,lexical[0],fixed[lexical[0]])
        context_reads=[lexical[0]]
        if 'lexical_saved' not in function: lexical=[]  # read-only: no scope to restore
    translated=[]
    for position,op in enumerate(function['instructions']):
        if op['kind']=='current_process': raise ValueError('current process must originate from a fixed entry reset')
        if op['kind']=='fixed_register_reset':
            if op['reads'] or len(op['writes'])!=1 or len(op.get('bindings',[]))!=1:
                raise ValueError('unsupported fixed register reset layout')
            index=op['writes'][0]
            if index not in fixed or op['bindings'][0]!=fixed[index]:
                raise ValueError('fixed register reset does not match its declaration')
            if index in context_reads:
                raise ValueError('read-only process context must not reset')
            if fixed[index]['role']=='process_self':
                if position!=0: raise ValueError('implicit self reset must be at function entry')
                op=dict(op,kind='current_process')
            else: op=dict(op,kind='symbol_pointer',symbol='#f')
        elif op['kind'] in LEXICAL_SCOPE_KINDS:
            if not lexical or op['writes']!=lexical: raise ValueError('lexical scope operation without its bracket')
        elif set(op['writes'])&fixed.keys():
            raise ValueError('fixed runtime register mutation is unsupported')
        translated.append(op)
    if context_reads:
        # Internal entry definition is visible to definite assignment/liveness.
        # Original CFG edges must continue to name the same original operation,
        # including back edges to original IR zero; they must not rerun the guard.
        for op in translated:
            if op['kind'] in ('branch','goto') and (not isinstance(op['target'],int) or not 0<=op['target']<len(translated)):
                raise ValueError('invalid IR branch target')
        translated=[dict(op,target=op['target']+1) if op['kind'] in ('branch','goto') else op
                    for op in translated]
        translated.insert(0,dict(kind='current_process',reads=[],writes=context_reads))
    # (when x (void-call)) copies the void call's phantom none result into an
    # unread expression value. No native value exists, and nothing reads the
    # destination, so the copy is dead. Any read keeps definite-assignment strict.
    read_values={value for op in translated for value in op['reads'] if op['kind']!='move'}
    read_values|={op['reads'][0] for op in translated if op['kind']=='move'}
    written={value for op in translated for value in op['writes']}
    translated=[dict(kind='nop',reads=[],writes=[],text=op.get('text',''))
                if op['kind']=='move' and semantic_types.get(op['reads'][0])=='none' and
                   op['reads'][0] not in written and op['writes'][0] not in read_values else op
                for op in translated]
    function=dict(function,instructions=translated)
    if implicit and (not translated or translated[0]['kind']!='current_process'):
        raise ValueError('implicit self requires an explicit entry reset')
    validate_definitions(function,set(registers))
    module=module or {}
    statics=statics or {}
    writers={index:[] for index in registers}
    for op in function['instructions']:
        if op['kind']!='nop':
            for index in op['writes']: writers[index].append(op)
    def function_origin(index,seen=None):
        seen=set() if seen is None else seen
        if index in seen or len(writers[index])!=1:
            raise ValueError('call requires an immutable linked function symbol')
        seen.add(index); source=writers[index][0]
        if source['kind']=='symbol_value' and source['symbol'] in module: return source['symbol']
        if runtime and source['kind']=='function_address' and source['function'] in module:
            return source['function']
        if source['kind']=='move': return function_origin(source['reads'][0],seen)
        raise ValueError('call requires an immutable linked function symbol')
    if any(r['class'] not in ('gpr64','float') for r in registers.values()):
        raise ValueError('128-bit/vector register lowering not implemented')
    parameter_types=[scalar_kind(p['type'],runtime) for p in function['parameters']]
    if len(parameter_types)>8: raise ValueError('more than eight arguments unsupported')
    if len({p['register'] for p in function['parameters']})!=len(function['parameters']):
        raise ValueError('duplicate incoming argument register')
    for kind in parameter_types:
        if kind not in value_types|{'float'}:
            raise ValueError('bootstrap ABI currently accepts numeric parameters only' if not runtime else 'unsupported tagged/scalar parameter')
    if void_return and all(op['kind']=='nop' for op in function['instructions']):
        e=Emitter(); e.ret(); data,relocations=e.finish()
        return data,{'name':function['name'],'bytes':len(data),'stack_bytes':0,
                     'relocations':relocations,'optimization':'empty_void_return',
                     'instruction_stream_bytes':len(data),'literal_pool_and_alignment_bytes':0,
                     'minimum_32_byte_cache_lines':1}
    if optimize:
        leaf=argument_return_leaf(function)
        if leaf is None: leaf=shift_return_leaf(function)
        if leaf is not None: return leaf
    slots=allocate_slots(function) if optimize else {index:i*8 for i,index in enumerate(sorted(registers))}
    frame=max(slots.values(),default=-8)+8
    # A dedicated slot, outside liveness reuse, records an entered lexical
    # scope so the call-fault exit can restore the saved process first.
    scope_flag=None
    if lexical:
        scope_flag=frame; frame+=8
    if frame>4096: raise ValueError('bootstrap stack frame too large')
    e=Emitter()
    local=0
    coalesced_moves=0
    def label_name():
        nonlocal local
        local+=1
        return 'local_'+str(local)
    def adjust_stack(amount):
        while amount:
            step=max(-120,min(120,amount))
            e.add_immediate(15,step); amount-=step
    def address(index):
        if index not in slots: raise ValueError('unknown virtual register')
        e.mov(8,14)
        offset=slots[index]
        if offset<=127: e.add_immediate(8,offset)
        else: e.constant(9,offset); e.add(8,9)
    def load64(index,low=0,high=1):
        if registers[index]['class']!='gpr64': raise ValueError('expected integer virtual value')
        address(index); e.load(low,8); e.add_immediate(8,4); e.load(high,8)
    def store64(index,low=0,high=1):
        if registers[index]['class']!='gpr64': raise ValueError('expected integer virtual value')
        address(index); e.store(8,low); e.add_immediate(8,4); e.store(8,high)
    def load_float(index,reg=0):
        address(index); e.word(0xf008|(reg<<8)|(8<<4))
    def store_float(index,reg=0):
        address(index); e.word(0xf00a|(8<<8)|(reg<<4))
    def constant(reg,value):
        value&=0xffffffff
        e.constant(reg,value if value<0x80000000 else value-0x100000000)
    def sign_extend():
        e.mov(1,0); e.immediate(9,-31); e.rr(0x400c,1,9)  # shad r9,r1
    def float_condition(kind,target):
        # Match the pinned frontend's UCOMISS + unsigned condition contract,
        # including unordered EQ/LT/LE. Do not substitute C/IEEE predicates.
        if kind in ('gt','le'):
            e.rr(0xf005,0,1); e.branch('bt' if kind=='gt' else 'bf',target)
        elif kind=='ge':
            e.rr(0xf005,0,1); e.branch('bt',target)
            e.rr(0xf004,0,1); e.branch('bt',target)
        elif kind in ('eq','lt'):
            e.rr(0xf004 if kind=='eq' else 0xf005,0 if kind=='eq' else 1,1 if kind=='eq' else 0)
            e.branch('bt',target)
            for reg in (0,1): e.rr(0xf004,reg,reg); e.branch('bf',target)
        elif kind=='ne':
            done=label_name()
            e.rr(0xf004,0,1); e.branch('bt',done)
            e.rr(0xf004,0,0); e.branch('bf',done)
            e.rr(0xf004,1,1); e.branch('bt',target); e.label(done)
        else: raise ValueError('unsupported float comparison')
    def condition(kind,signed,target):
        other=label_name(); done=label_name()
        e.rr(0x3000,1,3)  # high halves equal?
        if kind in ('eq','ne'):
            if kind=='eq':
                e.branch('bf',done); e.rr(0x3000,0,2); e.branch('bt',target); e.label(done)
            else:
                e.branch('bf',target); e.rr(0x3000,0,2); e.branch('bf',target)
            return
        if kind not in ('lt','le','gt','ge'): raise ValueError('unsupported comparison')
        e.branch('bf',other)
        if kind=='gt': e.rr(0x3006,0,2)
        elif kind=='ge': e.rr(0x3002,0,2)
        elif kind=='lt': e.rr(0x3006,2,0)
        else: e.rr(0x3002,2,0)
        e.branch('bra',done); e.label(other)
        opcode=0x3007 if signed else 0x3006
        if kind in ('gt','ge'): e.rr(opcode,1,3)
        else: e.rr(opcode,3,1)
        e.label(done); e.branch('bt',target)
    def scope_flag_address():
        e.mov(8,14)
        if scope_flag<=127: e.add_immediate(8,scope_flag)
        else: e.constant(9,scope_flag); e.add(8,9)
    def set_scope_flag(value):
        scope_flag_address(); e.immediate(0,value); e.store(8,0)
    # Preserve GCC callee-saved address scratch registers and PR.
    e.word(0x4f22)
    for reg in (14,8,9): e.rr(0x2006,15,reg)
    adjust_stack(-frame); e.mov(14,15)
    if lexical: set_scope_flag(0)
    gpr_args=float_args=0
    for parameter in function['parameters']:
        index=parameter['register']
        if parameter['type']=='float':
            if float_args>=8: raise ValueError('stack-passed float arguments not implemented')
            native=FLOAT_ARGUMENT_REGISTERS[float_args]
            if registers[index]['class']=='float': store_float(index,native)
            else:
                # The original frontend keeps float argument bits in GPR
                # virtual values. Adapt the native GCC float ABI at entry.
                e.word(0xf01d|(native<<8)); e.word(0x005a)
                sign_extend(); store64(index)
            float_args+=1
        else:
            if scalar_kind(parameter['type'],runtime) not in value_types:
                raise ValueError('bootstrap ABI currently accepts numeric parameters only')
            if gpr_args<2: store64(index,4+gpr_args*2,5+gpr_args*2)
            else:
                e.mov(8,14)
                offset=frame+16+(gpr_args-2)*8
                if offset<=127: e.add_immediate(8,offset)
                else: constant(9,offset); e.add(8,9)
                e.load(0,8); e.add_immediate(8,4); e.load(1,8); store64(index)
            gpr_args+=1
    instructions=function['instructions']
    guarded_calls=runtime and any(op['kind']=='call' for op in instructions)
    used={value for op in instructions for value in op['reads']}
    for position,op in enumerate(instructions):
        e.label('ir_'+str(position))
        kind=op['kind']; reads=op['reads']; writes=op['writes']
        try:
            if kind=='nop': continue
            if kind=='current_process':
                e.literal(0,('symbol','function:dc-c-current-process'))
                e.word(0x400b); e.nop()
                e.rr(0x2008,0,0)  # TST R0,R0: missing context must exit before body memory access.
                e.branch('bt','process_entry_failure')
                store64(writes[0]); continue
            if kind=='state_code_transfer':
                # Restart this fiber at the new state's code with four arguments,
                # returning through return-from-thread-dead. Success never returns.
                outgoing=(len(reads)-2)*8
                adjust_stack(-outgoing)
                for position_,value in enumerate(reads):
                    if position_<2: load64(value,4+position_*2,5+position_*2)
                    else:
                        load64(value); e.mov(2,15); e.add_immediate(2,(position_-2)*8)
                        e.store(2,0); e.add_immediate(2,4); e.store(2,1)
                e.literal(0,('symbol','function:dc-c-enter-state-code'))
                e.word(0x400b); e.nop()
                adjust_stack(outgoing)
                e.branch('bra','process_entry_failure'); continue
            if kind=='process_reread':
                # Original pp reset after resume: re-authenticate before any use.
                e.literal(0,('symbol','function:dc-c-current-process'))
                e.word(0x400b); e.nop()
                e.rr(0x2008,0,0); e.branch('bt','process_entry_failure')
                store64(writes[0]); continue
            if kind=='suspend':
                # Original thread suspension: yield the current native fiber.
                e.literal(0,('symbol','function:dc-c-suspend'))
                e.word(0x400b); e.nop()
                e.rr(0x2008,0,0); e.branch('bt','process_entry_failure'); continue
            if kind=='stack_pointer':
                e.mov(0,15); e.immediate(1,0); store64(writes[0]); continue
            if kind=='return_from_thread':
                # The original abandons the running thread to the kernel. The
                # native bridge owns that transfer; until a verified continuation
                # exists it records UNSUPPORTED_TRANSFER and this body exits.
                e.literal(0,('symbol','function:dc-c-return-from-thread'))
                e.word(0x400b); e.nop()
                e.branch('bra','process_entry_failure'); continue
            if kind in LEXICAL_SCOPE_KINDS:
                entering=kind=='process_scope_enter'
                load64(reads[0],4,5)
                e.literal(0,('symbol','function:dc-c-'+('enter' if entering else 'leave')+'-process-scope'))
                e.word(0x400b); e.nop()
                e.rr(0x2008,0,0)  # TST R0,R0: refused selection/restore exits without the body
                e.branch('bt','process_entry_failure')
                set_scope_flag(1 if entering else 0)
                load64(reads[0]); store64(writes[0]); continue
            if kind=='symbol_pointer':
                # A while-expression's unused #f result has no side effects.
                # Never substitute a made-up address for a live GOAL symbol.
                if all(value not in used for value in writes): continue
                if not runtime: raise ValueError('live GOAL symbol requires native symbol runtime')
                e.literal(0,('symbol','pointer:'+op['symbol'])); e.immediate(1,0); store64(writes[0])
                continue
            if kind=='function_address':
                if not runtime: raise ValueError('function object requires native runtime')
                if op['function'] not in module: raise ValueError('unresolved function object '+op['function'])
                e.literal(0,('symbol','function:'+op['function'])); e.immediate(1,0); store64(writes[0])
            elif kind=='static_address':
                if not runtime: raise ValueError('static object requires native runtime')
                item=statics.get(op['static_id'])
                if not item or item['kind'] not in ('string','float','pair','structure'):
                    raise ValueError('unsupported/missing static object '+op['static_id'])
                e.literal(0,('symbol','data:'+op['static_id'])); e.immediate(1,0); store64(writes[0])
            elif kind=='float_constant':
                bits=op['value_bits']
                if not isinstance(bits,int) or not 0<=bits<=0xffffffff or registers[writes[0]]['class']!='float':
                    raise ValueError('invalid scalar float constant')
                constant(0,bits); e.word(0x405a); e.word(0xf00d); store_float(writes[0])
            elif kind=='int_to_float':
                load64(reads[0]); e.word(0x405a); e.word(0xf02d); store_float(writes[0])
            elif kind=='float_to_int':
                invalid,done=label_name(),label_name()
                load_float(reads[0])
                constant(0,0x4f000000); e.word(0x405a); e.word(0xf10d)  # +2^31 -> FR1
                e.rr(0xf005,1,0); e.branch('bf',invalid)  # threshold > input; excludes NaN/+overflow
                e.word(0xf03d); e.word(0x005a)  # FTRC -> FPUL -> R0
                e.branch('bra',done); e.label(invalid); constant(0,0x80000000)
                e.label(done); sign_extend(); store64(writes[0])
            elif kind=='symbol_value':
                if runtime:
                    e.literal(0,('symbol','cell:'+op['symbol'])); e.load(0,0)
                    if op.get('signed'): sign_extend()
                    else: e.immediate(1,0)
                else:
                    if op['symbol'] not in module: raise ValueError('unresolved symbol outside the immutable numeric module')
                    e.literal(0,('symbol',op['symbol'])); e.immediate(1,0)
                store64(writes[0])
            elif kind=='set_symbol':
                if not runtime: raise ValueError('mutable GOAL symbol requires native symbol runtime')
                load64(reads[0]); e.literal(2,('symbol','cell:'+op['symbol'])); e.store(2,0)
            elif kind in ('load_memory','store_memory'):
                if not runtime: raise ValueError('object memory access requires native runtime profile')
                size,offset=op['size'],op['offset']
                if size not in (1,2,4,8) or not isinstance(offset,int) or not -(1<<31)<=offset<(1<<31):
                    raise ValueError('unsupported memory width/offset')
                loading=kind=='load_memory'
                value=writes[0] if loading else reads[0]
                base=reads[0] if loading else reads[1]
                floating=registers[value]['class']=='float'
                if floating and (size!=4 or (loading and op.get('signed'))):
                    raise ValueError('unsupported floating memory width/sign')
                if loading and op['memory_class']!=registers[value]['class']:
                    raise ValueError('memory/register class mismatch')
                load64(base,2,3)
                if -128<=offset<=127: e.add_immediate(2,offset)
                else: constant(3,offset); e.add(2,3)
                # Typed native addresses must satisfy SH-4's natural alignment.
                # Pairs use tag-adjusted offsets (-2/+2), restoring word alignment.
                if loading:
                    if floating:
                        e.rr(0xf008,0,2); store_float(value)
                    else:
                        e.rr({1:0x6000,2:0x6001,4:0x6002,8:0x6002}[size],0,2)
                        if size==8:
                            e.add_immediate(2,4); e.load(1,2)
                        elif op['signed']: sign_extend()
                        else:
                            if size in (1,2): e.rr(0x600c if size==1 else 0x600d,0,0)
                            e.immediate(1,0)
                        store64(value)
                elif floating:
                    load_float(value); e.rr(0xf00a,2,0)
                else:
                    load64(value); e.rr({1:0x2000,2:0x2001,4:0x2002,8:0x2002}[size],2,0)
                    if size==8: e.add_immediate(2,4); e.store(2,1)
            elif kind=='call':
                if runtime and 'abi' in op:
                    if scalar_kind(registers[reads[0]]['type'],True)!='function':
                        raise ValueError('dynamic call requires original function-typed IR')
                    target={'parameters':[{'type':kind} for kind in op['abi'].get('parameter_abi_types',op['abi']['parameters'])],
                            'return_type':op['abi'].get('return_abi_type',op['abi']['return_type'])}
                else: target=module[function_origin(reads[0])]
                parameters=target['parameters']; args=reads[1:]
                void_call=runtime and target['return_type']=='none'
                if len(writes)!=(0 if void_call else 1):
                    raise ValueError('call result layout does not match return type')
                if len(args)!=len(parameters): raise ValueError('linked function argument count mismatch')
                if len(args)>8: raise ValueError('more than eight call arguments unsupported')
                kinds={p['type']=='float' for p in parameters}
                integer_count=sum(p['type']!='float' for p in parameters)
                if len(kinds)>1:
                    if '_varargs_' in op.get('function_type',''):
                        raise ValueError('mixed variadic call ABI not verified')
                outgoing=max(0,integer_count-2)*8
                adjust_stack(-outgoing)
                integer_index=float_index=0
                for argument,parameter in zip(args,parameters):
                    if parameter['type']=='float':
                        if float_index>=8 or registers[argument]['type']!='float': raise ValueError('unsupported float call argument')
                        native=FLOAT_ARGUMENT_REGISTERS[float_index]; float_index+=1
                        if registers[argument]['class']=='float': load_float(argument,native)
                        else:
                            load64(argument); e.word(0x405a); e.word(0xf00d|(native<<8))
                    else:
                        if scalar_kind(parameter['type'],runtime) not in value_types or scalar_kind(registers[argument]['type'],runtime) not in value_types:
                            raise ValueError('unsupported integer call argument')
                        if integer_index<2: load64(argument,4+integer_index*2,5+integer_index*2)
                        else:
                            load64(argument); e.mov(2,15); e.add_immediate(2,(integer_index-2)*8)
                            e.store(2,0); e.add_immediate(2,4); e.store(2,1)
                        integer_index+=1
                load64(reads[0])
                if runtime:
                    e.rr(0x2008,0,0); e.branch('bt','call_failure')
                    e.literal(2,('symbol','pointer:#f')); e.rr(0x3000,0,2); e.branch('bt','call_failure')
                e.word(0x400b); e.nop()  # jsr @r0, PR saved in prologue
                adjust_stack(outgoing)
                if void_call: pass  # no value exists in R0:R1 or FR0 to save
                elif target['return_type']=='float':
                    if registers[writes[0]]['type']!='float': raise ValueError('float call result type mismatch')
                    if registers[writes[0]]['class']=='float': store_float(writes[0])
                    else:
                        e.word(0xf01d); e.word(0x005a); sign_extend(); store64(writes[0])
                elif scalar_kind(target['return_type'],runtime) in value_types: store64(writes[0])
                else: raise ValueError('unsupported linked function return type')
            elif kind=='constant':
                value=op['value']; constant(0,value); constant(1,value>>32); store64(writes[0])
            elif kind=='move':
                src,dst=reads[0],writes[0]
                if optimize and slots[src]==slots[dst] and registers[src]['class']==registers[dst]['class']:
                    coalesced_moves+=1; continue
                if registers[src]['class']==registers[dst]['class']=='gpr64': load64(src); store64(dst)
                elif registers[src]['class']==registers[dst]['class']=='float': load_float(src); store_float(dst)
                elif registers[src]['class']=='gpr64':
                    load64(src); e.word(0x405a); e.word(0xf00d); store_float(dst)
                else:
                    load_float(src); e.word(0xf01d); e.word(0x005a); sign_extend(); store64(dst)
            elif kind in ('shl64','shr64','sar64','shlv64','shrv64','sarv64'):
                variable=kind in ('shlv64','shrv64','sarv64')
                if len(reads)!=(2 if variable else 1) or len(writes)!=1:
                    raise ValueError('invalid shift operand layout')
                load64(reads[0])
                if variable: load64(reads[1],2,3)
                emit_shift64(e,{'shl':'left','shr':'logical','sar':'arithmetic'}[kind[:3]],
                             None if variable else op['shift'],label_name())
                store64(writes[0])
            elif kind in ('idiv32','udiv32','imod32','umod32'):
                if not runtime: raise ValueError('integer division requires native runtime fault bridge')
                if len(reads)!=2 or len(writes)!=1: raise ValueError('invalid division operand layout')
                load64(reads[0]); load64(reads[1],2,3)
                emit_div32(e,kind in ('idiv32','imod32'),kind in ('imod32','umod32'),label_name())
                store64(writes[0])
            elif kind in ('add64','sub64','and64','or64','xor64','imul32','imul64','not64'):
                load64(reads[0])
                if kind!='not64': load64(reads[1],2,3)
                if kind=='add64': e.clrt(); e.addc(0,2); e.addc(1,3)
                elif kind=='sub64': e.clrt(); e.subc(0,2); e.subc(1,3)
                elif kind in ('and64','or64','xor64'):
                    opcode={'and64':0x2009,'or64':0x200b,'xor64':0x200a}[kind]
                    e.rr(opcode,0,2); e.rr(opcode,1,3)
                elif kind=='not64': e.rr(0x6007,0,0); e.rr(0x6007,1,1)
                elif kind=='imul32': e.mul(0,2); e.macl(0); sign_extend()
                else:
                    e.mov(4,0); e.mov(5,1); e.rr(0x3005,4,2)
                    e.macl(0); e.word(0x010a)
                    e.mul(5,2); e.macl(6); e.add(1,6)
                    e.mul(4,3); e.macl(6); e.add(1,6)
                store64(writes[0])
            elif kind in ('add_float','sub_float','mul_float','div_float','min_float','max_float','sqrt_float'):
                if kind=='sqrt_float':
                    load_float(reads[0],0); e.word(0xf06d); store_float(writes[0]); continue
                load_float(reads[0],0); load_float(reads[1],1)
                if kind in ('min_float','max_float'):
                    done=label_name()
                    e.rr(0xf005,1 if kind=='min_float' else 0,0 if kind=='min_float' else 1)
                    e.branch('bt',done); e.fmov(0,1); e.label(done)
                else: e.rr({'add_float':0xf000,'sub_float':0xf001,'mul_float':0xf002,'div_float':0xf003}[kind],0,1)
                store_float(writes[0],0)
            elif kind=='goto': e.branch('bra','ir_'+str(op['target']))
            elif kind=='branch':
                if op.get('float'):
                    if op.get('signed'): raise ValueError('signed flag unsupported for float branch')
                    load_float(reads[0],0); load_float(reads[1],1)
                    float_condition(op['condition'],'ir_'+str(op['target']))
                else:
                    load64(reads[0]); load64(reads[1],2,3)
                    condition(op['condition'],op['signed'],'ir_'+str(op['target']))
            elif kind=='return':
                if len(reads)!=(0 if void_return else 1):
                    raise ValueError('return value does not match function return type')
                if void_return: pass
                elif function.get('return_type')=='float':
                    if registers[reads[0]]['class']=='float': load_float(reads[0])
                    else: load64(reads[0]); e.word(0x405a); e.word(0xf00d)
                else: load64(reads[0])
                e.branch('bra','epilogue')
            else: raise ValueError('unsupported typed IR operation '+kind)
        except (ValueError,KeyError,IndexError) as error:
            raise ValueError(f'{function["name"]} IR {position} ({op.get("text",kind)}): {error}') from error
    e.label('ir_'+str(len(instructions)))
    if implicit or context_reads or guarded_calls or transfers:
        e.branch('bra','epilogue')
    if guarded_calls:
        e.label('call_failure')
        if lexical:
            # A missing callee inside the bracket must not leak the entered
            # scope: restore the saved process (an earlier error is retained).
            restored=label_name()
            scope_flag_address(); e.load(0,8); e.rr(0x2008,0,0); e.branch('bt',restored)
            load64(function['lexical_saved'],4,5)
            e.literal(0,('symbol','function:dc-c-leave-process-scope'))
            e.word(0x400b); e.nop(); set_scope_flag(0); e.label(restored)
        e.literal(0,('symbol','function:dc-c-call-error'))
        e.word(0x400b); e.nop()
        # The boxed fault bridge returns uint64 zero. Floating callers must
        # explicitly define FR0; void callers promise no result register.
        if function['return_type']=='float': e.word(0xf08d)  # FLDI0 FR0
        e.branch('bra','epilogue')
    if implicit or context_reads or transfers:
        e.label('process_entry_failure'); e.immediate(0,0); e.immediate(1,0)
        if function['return_type']=='float': e.word(0xf08d)  # defined zero on refusal
    e.label('epilogue'); e.mov(15,14); adjust_stack(frame)
    for reg in (9,8,14): e.rr(0x6006,reg,15)
    e.word(0x4f26); e.ret()
    data,relocations,layout=e.finish_with_stats(relax=True,literal_islands=True,far_branches=True)
    instruction_bytes=layout['instruction_bytes']
    return data,{'name':function['name'],'bytes':len(data),'stack_bytes':frame+16,'relocations':relocations,
                'optimization':'stack_slot_reuse' if optimize else 'stack_baseline','instruction_stream_bytes':instruction_bytes,
                'stack_slot_count':frame//8,'virtual_register_count':len(registers),
                'baseline_stack_bytes':len(registers)*8+16,'coalesced_stack_moves':coalesced_moves,
                'expanded_conditional_branches':layout['expanded_conditional_branches'],
                'literal_islands':layout['literal_islands'],
                'far_branches':layout.get('far_branches',0),
                'literal_pool_and_alignment_bytes':len(data)-instruction_bytes,
                'minimum_32_byte_cache_lines':(len(data)+31)//32}

def compile_module(doc,names,runtime=False,optimize=True,initializer=None):
    if doc.get('format_version')!=1: raise ValueError('unsupported typed IR format')
    functions={f['name']:f for f in doc['functions']}
    if len(functions)!=len(doc['functions']): raise ValueError('duplicate function name')
    names=list(names)
    if len(names)!=len(set(names)): raise ValueError('duplicate selected function')
    if initializer is not None:
        if not runtime: raise ValueError('initializer requires native runtime profile')
        if not initializer or initializer in functions: raise ValueError('initializer name must be nonempty and unique')
        initializers=[f for f in functions.values() if f.get('top_level')]
        if len(initializers)!=1 or initializers[0]['parameters']:
            raise ValueError('expected one original zero-argument top-level function')
        # Rename only the export metadata; retain the original instructions.
        functions[initializer]=dict(initializers[0],name=initializer)
        names.append(initializer)
    outputs={}
    statics={s['id']:s for s in doc.get('statics',[])}
    if len(statics)!=len(doc.get('statics',[])): raise ValueError('duplicate static object id')
    module={name:functions[name] for name in names if name in functions}
    identifiers=native_identifiers(names)
    for name in names:
        if name not in functions: raise ValueError('missing function '+name)
        identifier=identifiers[name]
        outputs[identifier]=compile_function(functions[name],module,runtime,optimize,statics)
    if runtime: module_data(doc,outputs)
    return outputs

def structured_static_data(item):
    payload=item.get('data_bytes'); offset=item.get('address_offset'); relocations=item.get('relocations')
    if not isinstance(payload,list) or not payload or len(payload)>1048576 or any(type(b) is not int or not 0<=b<=255 for b in payload):
        raise ValueError('invalid structured static bytes')
    if type(offset) is not int or offset not in (0,4) or offset>=len(payload):
        raise ValueError('unsupported structured static address offset')
    if not isinstance(relocations,list): raise ValueError('missing structured static relocations')
    seen=set()
    for relocation in relocations:
        if not isinstance(relocation,dict): raise ValueError('invalid structured static relocation')
        at=relocation.get('offset'); symbol=relocation.get('symbol')
        if type(at) is not int or at<0 or at&3 or at+4>len(payload) or at in seen:
            raise ValueError('invalid structured static relocation offset')
        seen.add(at)
        if not isinstance(symbol,str) or '\0' in symbol or ':' not in symbol:
            raise ValueError('invalid structured static import')
        kind,name=symbol.split(':',1)
        if kind not in ('type','pointer','function','data') or not name:
            raise ValueError('unsupported structured static import')
    return bytes(payload),offset,relocations

def module_data(doc,objects):
    referenced={r['symbol'][5:] for _,report in objects.values() for r in report['relocations']
                if r['symbol'].startswith('data:')}
    statics={s['id']:s for s in doc.get('statics',[])}
    functions={report['name'] for _,report in objects.values()}
    pending=list(referenced); visited=set()
    while pending:
        name=pending.pop()
        if name in visited: continue
        visited.add(name)
        item=statics.get(name)
        if not item: raise ValueError('missing referenced static object '+name)
        if item['kind']=='structure':
            _,_,relocations=structured_static_data(item)
            for relocation in relocations:
                kind,target=relocation['symbol'].split(':',1)
                if kind=='data': referenced.add(target); pending.append(target)
                elif kind=='function' and target not in functions:
                    raise ValueError('unresolved static function object '+target)
        if item['kind']=='pair':
            for atom in (item['car'],item['cdr']):
                if atom['kind']=='data': referenced.add(atom['target']); pending.append(atom['target'])
                elif atom['kind']=='function' and atom['target'] not in functions:
                    raise ValueError('unresolved static function object '+atom['target'])
    output=[]
    for item in doc.get('statics',[]):
        if item['id'] not in referenced: continue
        if item['kind']=='string':
            content=bytes(item['text_bytes'])
            data=struct.pack('<II',0,len(content))+content+b'\0'
            offset,relocations=4,[{'offset':0,'symbol':'type:string'}]
        elif item['kind']=='float':
            data=struct.pack('<I',item['value_bits']); offset,relocations=0,[]
        elif item['kind']=='structure':
            data,offset,relocations=structured_static_data(item)
        elif item['kind']=='pair':
            data=bytearray(8); offset,relocations=2,[]
            for at,atom in zip((0,4),(item['car'],item['cdr'])):
                if atom['kind']=='constant': struct.pack_into('<I',data,at,atom['value_bits'])
                elif atom['kind'] in ('data','pointer','function'):
                    relocations.append({'offset':at,'symbol':atom['kind']+':'+atom['target']})
                else: raise ValueError('unsupported static pair element')
            data=bytes(data)
        else: raise ValueError('unsupported static object '+item['id'])
        output.append({'name':item['id'],'data':data,'address_offset':offset,'relocations':relocations})
    if referenced!={d['name'] for d in output}: raise ValueError('missing referenced static object')
    return output

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path)
    parser.add_argument('--function',action='append',default=[])
    parser.add_argument('--header',type=Path,required=True)
    parser.add_argument('--manifest',type=Path)
    parser.add_argument('--native-module',type=Path,help='Also write a DCSH native module file')
    parser.add_argument('--runtime',action='store_true',help='Bind tagged values and mutable symbol cells through the native runtime')
    parser.add_argument('--no-optimize',action='store_true',help='Retain the stack baseline for differential checks')
    parser.add_argument('--initializer',help='Export the original top-level code with this unique name')
    args=parser.parse_args()
    if not args.function and args.initializer is None:
        parser.error('select at least one function or an initializer')
    try:
        doc=json.loads(args.input.read_text())
        objects=compile_module(doc,args.function,args.runtime,not args.no_optimize,args.initializer)
        if args.native_module:
            if not args.runtime: raise ValueError('native module file requires runtime profile')
            from native_module_format import pack_native_module
            init_kind=0
            if args.initializer is not None:
                top=next(f for f in doc['functions'] if f.get('top_level'))
                result_type=top.get('return_abi_type',top['return_type'])
                if result_type=='float': raise ValueError('native module floating initializer ABI is not implemented')
                init_kind=2 if result_type=='none' else 1
            native=pack_native_module(objects,module_data(doc,objects),args.initializer,init_kind)
            args.native_module.parent.mkdir(parents=True,exist_ok=True); args.native_module.write_bytes(native)
        lines=['/* Native code from original typed GOAL frontend IR (AI-assisted). */']
        if args.runtime: lines.append('#include "goal_module.h"')
        manifest=[]
        def c_string(value):
            return '"'+''.join(chr(b) if 32<=b<127 and b not in (34,92) else '\\%03o'%b for b in value.encode('utf-8'))+'"'
        for name,(data,report) in objects.items():
            lines.append('static const unsigned char goal_'+name+'[] = {'+','.join(str(b) for b in data)+'};')
            if args.runtime and report['relocations']:
                lines.append('static const dc_goal_code_relocation goal_'+name+'_relocations[] = {')
                lines += ['{'+str(r['offset'])+'u,'+c_string(r['symbol'])+'},' for r in report['relocations']]
                lines.append('};')
            elif not args.runtime:
                identifiers=native_identifiers(sorted({r['symbol'] for r in report['relocations']}))
                for relocation in report['relocations']:
                    symbol=identifiers[relocation['symbol']].upper()
                    lines.append(f'#define GOAL_{name.upper()}_{symbol}_OFFSET {relocation["offset"]}u')
            manifest.append(report)
        if args.runtime:
            module_name=re.sub('[^A-Za-z0-9_]','_',args.header.stem)
            if not module_name or module_name[0].isdigit(): raise ValueError('invalid module header name')
            data_objects=module_data(doc,objects)
            for index,item in enumerate(data_objects):
                label='goal_'+module_name+'_data_'+str(index)
                lines.append('static const unsigned char '+label+'[] = {'+','.join(str(b) for b in item['data'])+'};')
                if item['relocations']:
                    lines.append('static const dc_goal_code_relocation '+label+'_relocations[] = {')
                    lines += ['{'+str(r['offset'])+'u,'+c_string(r['symbol'])+'},' for r in item['relocations']]
                    lines.append('};')
            if data_objects:
                lines.append('static const dc_goal_data_export goal_'+module_name+'_data[] = {')
                for index,item in enumerate(data_objects):
                    label='goal_'+module_name+'_data_'+str(index)
                    lines.append('{'+c_string(item['name'])+','+label+','+str(len(item['data']))+'u,'+
                                 str(item['address_offset'])+'u,'+(label+'_relocations' if item['relocations'] else 'NULL')+
                                 ','+str(len(item['relocations']))+'u},')
                lines.append('};')
            lines.append(f'#define GOAL_{module_name.upper()}_DATA '+('goal_'+module_name+'_data' if data_objects else 'NULL'))
            lines.append(f'#define GOAL_{module_name.upper()}_DATA_COUNT {len(data_objects)}u')
            lines.append('static const dc_goal_code_export goal_'+module_name+'_exports[] = {')
            for name,(data,report) in objects.items():
                relocations='goal_'+name+'_relocations' if report['relocations'] else 'NULL'
                lines.append('{'+c_string(report['name'])+',goal_'+name+','+str(len(data))+'u,'+relocations+','+str(len(report['relocations']))+'u},')
            lines+=['};',f'#define GOAL_{module_name.upper()}_EXPORT_COUNT {len(objects)}u']
            if args.initializer is not None:
                lines.append(f'#define GOAL_{module_name.upper()}_INITIALIZER '+c_string(args.initializer))
        args.header.parent.mkdir(parents=True,exist_ok=True)
        args.header.write_text('\n'.join(lines)+'\n')
        if args.manifest: args.manifest.write_text(json.dumps(manifest,indent=2)+'\n')
        print(json.dumps(manifest,indent=2))
    except (ValueError,KeyError,IndexError) as error: parser.error(str(error))
