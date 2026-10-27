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
    if runtime and name and name.startswith(('(pointer ','(inline-array ')) and name.endswith(')'):
        return 'pointer'  # an inline-array is the address of its first element
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

class UninitializedRead(ValueError):
    def __init__(self,node,missing):
        super().__init__(f'IR {node} reads uninitialized virtual registers {sorted(missing)}')
        self.node=node

def validate_definitions(function,registers,entry_defined=()):
    """Check definite assignment along reachable paths, including loop edges.

    entry_defined registers count as assigned at entry (zero-filled vf slots)."""
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
    entry={p['register'] for p in function['parameters']}|set(entry_defined)
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
        if missing: raise UninitializedRead(node,missing)

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

def stack_object_reservation(function,registers,fixed,runtime):
    """Validate original FunctionEnv capacity, never infer it from addresses."""
    addresses=[op for op in function['instructions'] if op.get('kind')=='stack_address']
    if 'stack_slots_used_for_stack_vars' not in function:
        if addresses: raise ValueError('stack address operation missing stack slot count')
        return 0
    count=function['stack_slots_used_for_stack_vars']
    if type(count) is not int or count<0 or count>4096//8:
        raise ValueError('stack object slot count out of range')
    if (count or addresses) and not runtime:
        raise ValueError('stack objects require native runtime profile')
    for op in addresses:
        reads,writes,slot=op.get('reads'),op.get('writes'),op.get('slot')
        if not isinstance(reads,list) or not isinstance(writes,list) or reads or len(writes)!=1:
            raise ValueError('stack address operation must write one register and read none')
        dest=writes[0]
        if type(slot) is not int or slot<0 or slot>=count:
            raise ValueError('stack address slot outside reserved stack objects')
        if type(dest) is not int or dest not in registers or registers[dest].get('class')!='gpr64':
            raise ValueError('stack address destination requires a scalar register')
        if scalar_kind(registers[dest].get('type'),True) not in ('object','pointer'):
            raise ValueError('stack address destination is not a scalar address')
        if dest in fixed:
            raise ValueError('stack address destination overlaps a fixed register')
    return count

LEXICAL_SCOPE_KINDS=('process_scope_enter','process_scope_leave')

VF3_OPS={'.xor.vf':'xor','.sub.vf':'sub','.add.vf':'add','.mul.vf':'mul','.max.vf':'max',
         '.min.vf':'min','.div.vf':'div'}
VF_OPERAND=re.compile(r'ivf-(\d+)$')
# PS2 MMI ops: codes shared with goal_mmi.h (three-operand from 1, immediate from 32).
MMI3_OPS={name:1+i for i,name in enumerate(['pextlb', 'pextlh', 'pextlw', 'pextub', 'pextuh', 'pextuw', 'pceqb', 'pceqh', 'pceqw', 'pcgtb', 'pcgth', 'pcgtw', 'psubw', 'por', 'pxor', 'pand', 'packuswb', 'paddb'])}
MMI2_OPS={name:32+i for i,name in enumerate(['pw.sll', 'pw.srl', 'pw.sra', 'ph.sll', 'ph.srl', 'VPSRLDQ', 'VPSLLDQ', 'VPSHUFLW', 'VPSHUFHW'])}
QUAD_OPERAND=re.compile(r'i(?:i128|vf)-(\d+)$')

def mmi_asm(op):
    """Structured PS2 MMI 128-bit integer asm op, or None (AI-assisted).

    pcpyld/pcpyud stay inline; these go to the dc-c-mmi helper (goal_mmi.c),
    which follows the desktop x86 instruction and operand order for each kind."""
    text=op.get('text','').strip()
    if op['kind']!='unsupported' or ':no-color' in text: return None
    name,_,rest=text.partition(' ')
    fields=[f.strip() for f in rest.split(',')]
    def ids(names):
        out=[]
        for f in names:
            m=QUAD_OPERAND.match(f)
            if not m: return None
            out.append(int(m.group(1)))
        return out
    if name[1:] in MMI3_OPS and len(fields)==3:
        regs=ids(fields)
        if regs and op['writes']==[regs[0]] and set(op['reads'])==set(regs[1:]) and len(op['reads'])<=2:
            return dict(op,kind='mmi',code=MMI3_OPS[name[1:]],dst=regs[0],srcs=regs[1:])
    elif name[1:] in MMI2_OPS and len(fields)==3 and fields[2].isdigit() and int(fields[2])<256:
        regs=ids(fields[:2])
        if regs and op['writes']==[regs[0]] and op['reads']==[regs[1]]:
            return dict(op,kind='mmi',code=MMI2_OPS[name[1:]]|int(fields[2])<<8,dst=regs[0],srcs=regs[1:])
    return None

def vector_asm(op):
    """Structured form of a desktop vector asm IR op, or None (AI-assisted).

    The pinned frontend expands GOAL's VU0 macros into these x86 IR ops, exported
    only as their printed text. Each is parsed strictly: colored virtual vf
    operands only (never :no-color physical registers), and operand ids must
    equal the op's exported read/write lists. Semantics follow the desktop x86
    encodings (goalc/emitter/IGenX86.cpp): blend takes lane i from src2 when mask
    bit i is set; swizzle lane i = src[(control >> 2i) & 3]; splat is a swizzle.
    """
    text=op.get('text','').strip()
    if op['kind']!='unsupported' or ':no-color' in text: return None
    name,_,rest=text.partition(' ')
    fields=[f.strip() for f in rest.split(',')]
    # xor/blend/swizzle/splat are bit copies, valid on either 128-bit class.
    bitwise=name in ('.xor.vf','.blend.vf','.swizzle.vf','.splat.vf')
    def ids(names):
        out=[]
        for f in names:
            m=(QUAD_OPERAND if bitwise else VF_OPERAND).match(f)
            if not m: return None
            out.append(int(m.group(1)))
        return out
    def exact(dst,srcs):
        # The exporter lists a repeated source once per operand position.
        return op['writes']==[dst] and set(op['reads'])==set(srcs) and len(op['reads'])<=len(srcs)
    if name in VF3_OPS and len(fields)==3:
        regs=ids(fields)
        if regs and exact(regs[0],regs[1:]):
            if name=='.xor.vf' and regs[1]==regs[2]:
                # x ^ x is zero whatever x holds: the zeroing idiom reads nothing.
                return dict(op,kind='vfzero',reads=[],dst=regs[0])
            return dict(op,kind='vf3',vop=VF3_OPS[name],dst=regs[0],src1=regs[1],src2=regs[2])
    elif name in ('.itof.vf','.ftoi.vf','.sqrt.vf') and len(fields)==2:
        regs=ids(fields)
        if regs and exact(regs[0],regs[1:]):
            return dict(op,kind='vf2',vop=name[1:-3],dst=regs[0],src=regs[1])
    elif name=='.blend.vf' and len(fields)==4 and fields[3].isdigit() and int(fields[3])<16:
        regs=ids(fields[:3])
        if regs and exact(regs[0],regs[1:]):
            return dict(op,kind='vfblend',dst=regs[0],src1=regs[1],src2=regs[2],mask=int(fields[3]))
    elif name in ('.swizzle.vf','.splat.vf') and len(fields)==3 and fields[2].isdigit():
        value=int(fields[2]); regs=ids(fields[:2])
        if name=='.splat.vf':
            if value>3: return None
            value*=0x55  # X,Y,Z,W = 0..3: every lane selects that element
        if value<256 and regs and exact(regs[0],regs[1:]):
            return dict(op,kind='vfswizzle',dst=regs[0],src=regs[1],control=value)
    return None

PURE_KINDS=('move','constant','add64','sub64','and64','or64','xor64','not64')

def remove_dead_pure_ops(function):
    """Drop pure register-only ops whose results nothing needs (AI-assisted).

    E.g. original disable-irq/enable-irq: on PC .mfc0/.mtc0 expand to nothing,
    leaving a cycle of arithmetic on an unassigned rlet register whose result is
    unused. Roots are every non-pure op (memory, calls, returns, branches, asm)
    plus fixed machine registers and the shared exit value; a pure op is needed
    only if it writes a register a needed op reads. Flow-insensitive, so it only
    removes ops no path can observe. Every other op is untouched.
    """
    ops=list(function['instructions'])
    if any(not isinstance(op.get('reads'),list) or not isinstance(op.get('writes'),list) for op in ops):
        return function  # malformed: leave it for the shape checks to refuse
    needed_regs={b['register'] for b in function.get('fixed_registers',[])}
    # An addressed variable is observable through its pointer at any later op.
    needed_regs.update(op['source'] for op in ops if op['kind']=='register_address')
    if function.get('shared_exit'): needed_regs.add(function['shared_exit']['register'])
    needed=[op['kind'] not in PURE_KINDS for op in ops]
    changed=True
    while changed:
        changed=False
        for i,op in enumerate(ops):
            if needed[i]: needed_regs.update(op['reads'])
        for i,op in enumerate(ops):
            if not needed[i] and set(op['writes'])&needed_regs:
                needed[i]=True; changed=True
    for i,op in enumerate(ops):
        if not needed[i]: ops[i]=dict(op,kind='nop',reads=[],writes=[],text='dead: '+op.get('text',''))
    return dict(function,instructions=ops)

def normalize_register_asm(function):
    """Read explicit x86 (.mov a b)/(.add a b) asm ops as plain 64-bit IR (AI-assisted).

    The original inline-asm forms between general registers are exactly a
    64-bit register copy and a 64-bit add; only their operand layout is checked.
    Every other asm op stays unsupported.
    """
    ops=[]
    for op in function['instructions']:
        text=op.get('text','').strip()
        vector=vector_asm(op)
        if vector is None: vector=mmi_asm(op)
        if vector is not None: op=vector
        elif op['kind']=='unsupported' and text in ('.nop.vf','.wait.vf') and not op['reads'] and not op['writes']:
            op=dict(op,kind='nop')  # x87 FNOP/FWAIT on desktop: VU pipeline timing, no state
        elif op['kind']=='unsupported' and text.startswith('.mov ') and len(op['reads'])==1 and len(op['writes'])==1:
            op=dict(op,kind='move')
        elif op['kind']=='unsupported' and text.split(' ')[0] in ('.pcpyld','.pcpyud') and \
             len(op['reads'])==2 and len(op['writes'])==1:
            op=dict(op,kind=text.split(' ')[0][1:])  # class checks happen at emission
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
    # A none-typed value (e.g. a discarded method-set! result) carries nothing:
    # copies of one are dropped, and a (none) function's return of one reads nothing.
    kinds={r['id']:r['type'] for r in function['registers']}
    function=dict(function,instructions=[
        dict(op,kind='nop',reads=[],writes=[],text='none copy: '+op.get('text',''))
        if op['kind']=='move' and len(op['reads'])==1 and kinds.get(op['reads'][0])=='none' else
        dict(op,reads=[]) if function.get('return_type')=='none' and op['kind']=='return' and
                             len(op['reads'])==1 and kinds.get(op['reads'][0])=='none'
        else op for op in function['instructions']])
    if runtime:
        function=normalize_register_asm(function)
        function=state_code_idiom(function)
        function=return_from_thread_idiom(function)
        function=native_machine_registers(function)
        function=suspend_idiom(function)
        # After the idiom recognizers: they must match the original op sequence.
        function=remove_dead_pure_ops(function)
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
    classes={r['id']:r['class'] for r in function['registers']}
    # 128-bit returns travel in r0:r1 (low) and r2:r3 (high) between compiled code.
    wide_return=runtime and any(op['kind']=='return' and op['reads'] and classes.get(op['reads'][0])=='int128'
                                for op in function['instructions'])
    if not void_return and not wide_return and scalar_kind(function.get('return_type'),runtime) not in value_types|{'float'}:
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
    # Check stack-op shapes before generic CFG/definition walkers inspect them.
    stack_objects=stack_object_reservation(function,registers,fixed,runtime)
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
    # Original VU0 code leaves unused lanes undefined (an uninitialized acc whose
    # other lanes are don't-care). vf slots are zero-filled at entry instead, so
    # vf reads are defined and deterministic; every other class stays strict.
    vf_entry={index for index,r in registers.items() if r['class']=='vector_float'}
    try:
        validate_definitions(function,set(registers),vf_entry)
    except UninitializedRead as error:
        # A file's top-level value is discarded by every loader. A (declare-file
        # (debug)) body of only macros/constants returns an unassigned register
        # on its debug path (garbage on desktop x86); that top level runs as void.
        # Only a return may be the offender, and only in a top-level function.
        if not function.get('top_level') or function['instructions'][error.node]['kind']!='return': raise
        function=dict(function,return_type='none',instructions=[
            dict(op,reads=[]) if op['kind']=='return' else op for op in function['instructions']])
        void_return=True
        validate_definitions(function,set(registers),vf_entry)
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
    if any(r['class'] not in ('gpr64','float','int128','vector_float') for r in registers.values()):
        raise ValueError('unsupported 128-bit/vector register class')
    QUAD=('int128','vector_float')
    # vf values live in 16-byte slots like int128, but never cross the call ABI.
    vectors={index for index,r in registers.items() if r['class']=='vector_float'}
    if vectors and not runtime: raise ValueError('vector values require the native runtime profile')
    if any(p['register'] in vectors for p in function['parameters']):
        raise ValueError('vector parameters are not part of the native ABI')
    if any(op['kind']=='return' and op['reads'] and op['reads'][0] in vectors for op in function['instructions']):
        raise ValueError('vector returns are not part of the native ABI')
    wide={index for index,r in registers.items() if r['class']=='int128'}
    quads=wide|vectors
    addressed=set()
    for op in function['instructions']:
        if op['kind']!='register_address': continue
        if op.get('reads')!=[] or not isinstance(op.get('writes'),list) or len(op['writes'])!=1:
            raise ValueError('register address operation must write one register and read none')
        source,dest=op.get('source'),op['writes'][0]
        if type(source) is not int or source not in registers:
            raise ValueError('register address of an unknown virtual register')
        if dest not in registers or registers[dest]['class']!='gpr64' or dest in vectors:
            raise ValueError('register address destination requires a scalar register')
        if source in vectors: raise ValueError('register address of a vf value is unsupported')
        if not runtime: raise ValueError('register addresses require the native runtime profile')
        addressed.add(source)
    if wide and not runtime: raise ValueError('128-bit values require the native runtime profile')
    parameter_types=[scalar_kind(p['type'],runtime) for p in function['parameters']]
    if len(parameter_types)>8: raise ValueError('more than eight arguments unsupported')
    if len({p['register'] for p in function['parameters']})!=len(function['parameters']):
        raise ValueError('duplicate incoming argument register')
    if sum(2 if p['register'] in wide else 1 for p in function['parameters'] if p['type']!='float')>8:
        raise ValueError('more than eight integer argument positions unsupported')
    for parameter,kind in zip(function['parameters'],parameter_types):
        if parameter['register'] in wide: continue  # two integer positions: low, high
        if kind not in value_types|{'float'}:
            raise ValueError('bootstrap ABI currently accepts numeric parameters only' if not runtime else 'unsupported tagged/scalar parameter')
    if not stack_objects and void_return and all(op['kind']=='nop' for op in function['instructions']):
        e=Emitter(); e.ret(); data,relocations=e.finish()
        return data,{'name':function['name'],'bytes':len(data),'stack_bytes':0,
                     'relocations':relocations,'optimization':'empty_void_return',
                     'instruction_stream_bytes':len(data),'literal_pool_and_alignment_bytes':0,
                     'minimum_32_byte_cache_lines':1}
    if optimize and not stack_objects and not addressed:
        leaf=argument_return_leaf(function)
        if leaf is None: leaf=shift_return_leaf(function)
        if leaf is not None: return leaf
    slots=allocate_slots(function) if optimize else {index:i*8 for i,index in enumerate(sorted(registers))}
    frame=max(slots.values(),default=-8)+8
    # Each 128-bit value owns a dedicated 16-byte slot outside slot reuse.
    for index in sorted(quads):
        slots[index]=frame; frame+=16
    # (& var): the variable's home is its slot, which a pointer may reach at any
    # later op, so it is pinned outside liveness reuse like desktop force_on_stack.
    for index in sorted(addressed-quads):
        slots[index]=frame; frame+=8
    # A dedicated slot, outside liveness reuse, records an entered lexical
    # scope so the call-fault exit can restore the saved process first.
    scope_flag=None
    if lexical:
        scope_flag=frame; frame+=8
    # Independent function-lifetime objects above spills and the cleanup flag.
    # A fixed 16-byte slack allows slot0 alignment for either SP alias/alignment.
    stack_region=None
    if stack_objects:
        stack_region=frame
        frame+=16+stack_objects*8
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
    def stack_object_address(slot,dest):
        if stack_region is None: raise ValueError('stack address without a stack object region')
        e.mov(8,14)
        raw=stack_region+15
        if raw<=127: e.add_immediate(8,raw)
        else: e.constant(9,raw); e.add(8,9)
        e.immediate(9,-16); e.rr(0x2009,8,9)  # align upward, preserving native P0/P1
        if slot:
            if slot*8<=127: e.add_immediate(8,slot*8)
            else: e.constant(9,slot*8); e.add(8,9)
        e.mov(0,8); e.immediate(1,0); store64(dest)
    def load128(index,regs=(4,5,6,7)):
        """Four little-endian words of a 128-bit slot: lo64=(regs[0],regs[1])."""
        if registers[index]['class'] not in QUAD: raise ValueError('expected 128-bit virtual value')
        address(index)
        for n,reg in enumerate(regs):
            if n: e.add_immediate(8,4)
            e.load(reg,8)
    def store128(index,regs=(4,5,6,7)):
        if registers[index]['class'] not in QUAD: raise ValueError('expected 128-bit virtual value')
        address(index)
        for n,reg in enumerate(regs):
            if n: e.add_immediate(8,4)
            e.store(8,reg)
    def load_lanes(index,base=0):
        """The four float lanes of a vf slot into FR(base)..FR(base+3)."""
        if registers[index]['class']!='vector_float': raise ValueError('expected vector float value')
        address(index)
        for n in range(4):
            if n: e.add_immediate(8,4)
            e.rr(0xf008,base+n,8)  # FMOV.S @R8,FRn
    def store_lanes(index,base=0):
        if registers[index]['class']!='vector_float': raise ValueError('expected vector float value')
        address(index)
        for n in range(4):
            if n: e.add_immediate(8,4)
            e.rr(0xf00a,8,base+n)  # FMOV.S FRm,@R8
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
    if vectors:
        e.immediate(0,0)
        for index in sorted(vectors):
            address(index)
            for n in range(4):
                if n: e.add_immediate(8,4)
                e.store(8,0)
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
        elif index in wide:
            # A 128-bit argument occupies two integer positions, low then high.
            for half,(a,b) in enumerate(((0,1),(2,3))):
                position=gpr_args+half
                if position<2: e.mov(a,4+position*2); e.mov(b,5+position*2)
                else:
                    e.mov(8,14)
                    offset=frame+16+(position-2)*8
                    if offset<=127: e.add_immediate(8,offset)
                    else: constant(9,offset); e.add(8,9)
                    e.load(a,8); e.add_immediate(8,4); e.load(b,8)
            store128(index,(0,1,2,3)); gpr_args+=2
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
            if kind=='native_function':
                # Import of a loaded native counterpart (resolved and type-checked by the loader).
                e.literal(0,('symbol','function:'+op['symbol'])); e.immediate(1,0); store64(writes[0])
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
            elif kind=='static_load':
                # Quadword load of a whole static structure into a 128-bit value.
                item=statics.get(op['static_id'])
                if not item or item['kind']!='structure' or registers[writes[0]]['class'] not in QUAD:
                    raise ValueError('unsupported static quadword load '+op['static_id'])
                e.literal(2,('symbol','data:'+op['static_id']))
                for n,reg in enumerate((4,5,6,7)):
                    if n: e.add_immediate(2,4)
                    e.load(reg,2)
                store128(writes[0])
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
                if size==16 and isinstance(offset,int) and -(1<<31)<offset<(1<<31) and \
                   registers[(writes if kind=='load_memory' else reads)[0]]['class'] in QUAD:
                    # Quadword access: four aligned words (no native 128-bit load).
                    loading=kind=='load_memory'
                    value=writes[0] if loading else reads[0]
                    base=reads[0] if loading else reads[1]
                    if loading and op.get('memory_class')!=registers[value]['class']:
                        raise ValueError('memory/register class mismatch')
                    if not loading: load128(value)
                    load64(base,2,3)
                    if -128<=offset<=127: e.add_immediate(2,offset)
                    else: constant(3,offset); e.add(2,3)
                    for n,reg in enumerate((4,5,6,7)):
                        if n: e.add_immediate(2,4)
                        if loading: e.load(reg,2)
                        else: e.store(2,reg)
                    if loading: store128(value)
                    continue
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
                # Desktop GOAL passes every variadic argument in an integer slot,
                # floats as their bit pattern; native format reads them the same way.
                variadic=runtime and '_varargs_' in op.get('function_type','')
                if variadic:
                    parameters=[{'type':'variadic'} for _ in parameters]
                    if any(argument in wide for argument in args):
                        raise ValueError('128-bit variadic argument unsupported')
                integer_count=sum(2 if argument in wide else 1 for argument,p in zip(args,parameters) if p['type']!='float')
                if integer_count>8: raise ValueError('more than eight integer argument positions unsupported')
                def place(position,a,b):
                    if position<2: e.mov(4+position*2,a); e.mov(5+position*2,b)
                    else:
                        e.mov(8,15); e.add_immediate(8,(position-2)*8)
                        e.store(8,a); e.add_immediate(8,4); e.store(8,b)
                outgoing=max(0,integer_count-2)*8
                adjust_stack(-outgoing)
                integer_index=float_index=0
                for argument,parameter in zip(args,parameters):
                    if parameter['type']=='variadic':
                        if registers[argument]['class']=='float':
                            load_float(argument,0); e.word(0xf01d); e.word(0x005a)  # FLDS FR0,FPUL; STS FPUL,R0
                            e.immediate(1,0)
                        else: load64(argument)
                        place(integer_index,0,1); integer_index+=1
                    elif argument in wide:
                        if registers[argument]['class']!='int128': raise ValueError('128-bit argument container mismatch')
                        load128(argument,(0,1,2,3)); place(integer_index,0,1); place(integer_index+1,2,3)
                        integer_index+=2
                    elif parameter['type']=='float':
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
                elif writes and writes[0] in wide: store128(writes[0],(0,1,2,3))
                elif target['return_type']=='float':
                    if registers[writes[0]]['type']!='float': raise ValueError('float call result type mismatch')
                    if registers[writes[0]]['class']=='float': store_float(writes[0])
                    else:
                        e.word(0xf01d); e.word(0x005a); sign_extend(); store64(writes[0])
                elif scalar_kind(target['return_type'],runtime) in value_types: store64(writes[0])
                else: raise ValueError('unsupported linked function return type')
            elif kind=='constant':
                value=op['value']; constant(0,value); constant(1,value>>32); store64(writes[0])
            elif kind=='move' and (registers[reads[0]]['class'] in QUAD or registers[writes[0]]['class'] in QUAD):
                src,dst=reads[0],writes[0]
                sc,dc=registers[src]['class'],registers[dst]['class']
                if sc in QUAD and dc in QUAD:
                    load128(src); store128(dst)  # movaps: all 128 bits
                elif dc in QUAD and sc=='float':
                    load_float(src); store_float(dst)  # movss: lane 0, other lanes kept
                elif sc in QUAD and dc=='float':
                    load_float(src); store_float(dst)  # movss: lane 0
                elif dc=='vector_float' and sc=='gpr64':
                    load64(src,4,5); e.immediate(6,0); e.immediate(7,0); store128(dst)  # movq: zero-extends
                elif sc=='vector_float' and dc=='gpr64':
                    load128(src); e.mov(0,4); e.mov(1,5); store64(dst)  # movq: low 64 bits
                elif dc=='int128' and sc=='gpr64':
                    # Frontend 64->128 register move zero-extends.
                    load64(src,4,5); e.immediate(6,0); e.immediate(7,0); store128(dst)
                elif registers[src]['class']=='int128' and registers[dst]['class']=='gpr64':
                    load128(src); e.mov(0,4); e.mov(1,5); store64(dst)  # low 64 bits
                else: raise ValueError('unsupported 128-bit/float move')
            elif kind=='vf3':
                vop=op['vop']
                if vop=='xor':
                    load128(op['src1']); load128(op['src2'],(0,1,2,3))
                    for n in range(4): e.rr(0x200a,4+n,n)  # XOR Rm,Rn: all bits
                    store128(op['dst'])
                    continue
                load_lanes(op['src1'],0); load_lanes(op['src2'],4)
                for n in range(4):
                    if vop in ('max','min'):
                        # maxps/minps: src1 if src1 > src2 (resp. <), else src2,
                        # including NaN and signed-zero choices (as max_float/min_float).
                        done=label_name()
                        if vop=='max': e.rr(0xf005,n,4+n)  # T = src1 > src2
                        else: e.rr(0xf005,4+n,n)          # T = src2 > src1
                        e.branch('bt',done); e.fmov(n,4+n); e.label(done)
                    else:
                        e.rr({'add':0xf000,'sub':0xf001,'mul':0xf002,'div':0xf003}[vop],n,4+n)
                store_lanes(op['dst'],0)
            elif kind=='vf2':
                vop=op['vop']
                if vop=='itof':
                    load128(op['src'])  # cvtdq2ps: signed int32 lanes
                    for n in range(4): e.word(0x405a|(4+n)<<8); e.word(0xf02d|n<<8)  # LDS Rm,FPUL; FLOAT FPUL,FRn
                    store_lanes(op['dst'],0)
                elif vop=='sqrt':
                    load_lanes(op['src'],0)
                    for n in range(4): e.word(0xf06d|n<<8)  # FSQRT FRn
                    store_lanes(op['dst'],0)
                else:
                    # cvttps2dq: truncate; NaN and out-of-range give 0x80000000,
                    # the same threshold contract as scalar float_to_int.
                    load_lanes(op['src'],0)
                    constant(0,0x4f000000); e.word(0x405a); e.word(0xf40d)  # +2^31 -> FR4
                    for n in range(4):
                        invalid,done=label_name(),label_name()
                        e.rr(0xf005,4,n); e.branch('bf',invalid)  # 2^31 > lane
                        e.word(0xf03d|n<<8); e.word(0x005a|(4+n)<<8)  # FTRC FRn,FPUL; STS FPUL,Rn
                        e.branch('bra',done); e.label(invalid); constant(4+n,0x80000000); e.label(done)
                    store128(op['dst'])
            elif kind=='mmi':
                # dc-c-mmi(op, dst, a, b) on the 16-byte slots; it copies its inputs first.
                for reg,index in ((5,op['dst']),(6,op['srcs'][0]),(7,op['srcs'][-1] if len(op['srcs'])>1 else None)):
                    if index is None: e.immediate(reg,0); continue
                    if registers[index]['class'] not in QUAD: raise ValueError('MMI operand must be a 128-bit value')
                    address(index); e.mov(reg,8)
                constant(4,op['code'])
                e.literal(0,('symbol','function:dc-c-mmi'))
                e.word(0x400b); e.nop()
            elif kind=='vfzero':
                for n in range(4): e.immediate(4+n,0)
                store128(op['dst'])
            elif kind=='vfblend':
                load128(op['src1']); load128(op['src2'],(0,1,2,3))
                store128(op['dst'],tuple(n if op['mask']>>n&1 else 4+n for n in range(4)))
            elif kind=='vfswizzle':
                load128(op['src'])
                store128(op['dst'],tuple(4+(op['control']>>(2*n)&3) for n in range(4)))
            elif kind=='pcpyld':
                # MIPS PCPYLD rd,rs,rt: rd.hi = rs.lo, rd.lo = rt.lo (aliases safe).
                load128(reads[1]); e.mov(0,4); e.mov(1,5)
                load128(reads[0]); e.mov(2,4); e.mov(3,5)
                e.mov(4,0); e.mov(5,1); e.mov(6,2); e.mov(7,3); store128(writes[0])
            elif kind=='pcpyud':
                # MIPS PCPYUD rd,rs,rt: rd.lo = rs.hi, rd.hi = rt.hi (aliases safe).
                load128(reads[0]); e.mov(0,6); e.mov(1,7)
                load128(reads[1]); e.mov(2,6); e.mov(3,7)
                e.mov(4,0); e.mov(5,1); e.mov(6,2); e.mov(7,3); store128(writes[0])
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
                elif reads[0] in wide: load128(reads[0],(0,1,2,3))
                elif function.get('return_type')=='float':
                    if registers[reads[0]]['class']=='float': load_float(reads[0])
                    else: load64(reads[0]); e.word(0x405a); e.word(0xf00d)
                else: load64(reads[0])
                e.branch('bra','epilogue')
            elif kind=='stack_address':
                stack_object_address(op['slot'],writes[0])
            elif kind=='register_address':
                address(op['source']); e.mov(0,8); e.immediate(1,0); store64(writes[0])
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
                'baseline_stack_bytes':len(registers)*8+16+(8 if lexical and stack_objects else 0)+(16+stack_objects*8 if stack_objects else 0),
                'coalesced_stack_moves':coalesced_moves,
                **({'stack_object_slots':stack_objects,'stack_object_region':stack_region} if stack_objects else {}),
                'expanded_conditional_branches':layout['expanded_conditional_branches'],
                'literal_islands':layout['literal_islands'],
                'far_branches':layout.get('far_branches',0),
                'literal_pool_and_alignment_bytes':len(data)-instruction_bytes,
                'minimum_32_byte_cache_lines':(len(data)+31)//32}

def bind_native_functions(doc,names,bindings):
    """Replace original function objects with named native counterparts (AI-assisted).

    Only for original functions that cannot be compiled (the kernel's x86/ARM64
    assembly primitives). Every use of the original function object becomes an
    import of the native counterpart, so the original (define name ...) and
    method-set! forms bind it under the original name. A bound original must not
    also be compiled, and must exist in the module.
    """
    if not bindings: return doc
    present={f['name'] for f in doc['functions']}
    for original,native in bindings.items():
        if original in names: raise ValueError('natively bound function is also compiled: '+original)
        if original not in present: raise ValueError('native binding for an absent original function: '+original)
        if not native or not isinstance(native,str): raise ValueError('invalid native counterpart for '+original)
    functions=[]
    for f in doc['functions']:
        ops=[dict(o,kind='native_function',symbol=bindings[o['function']])
             if o['kind']=='function_address' and o.get('function') in bindings else o for o in f['instructions']]
        functions.append(dict(f,instructions=ops))
    return dict(doc,functions=functions)

def compile_module(doc,names,runtime=False,optimize=True,initializer=None,native_bindings=None):
    if native_bindings and not runtime: raise ValueError('native bindings require the native runtime profile')
    names=list(names)  # may be a one-shot iterator
    doc=bind_native_functions(doc,names,native_bindings or {})
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

def module_defined_types(doc):
    """Types the module's own top level defines: calls of type's method 0 (new,
    offset 16) whose first argument is a quoted symbol (AI-assisted)."""
    defined=set()
    for f in doc.get('functions',[]):
        if not f.get('top_level'): continue
        ops=f['instructions']; writer={}
        for op in ops:
            if op['kind']!='nop' and len(op.get('writes',[]))==1: writer.setdefault(op['writes'][0],[]).append(op)
        def origin(reg):
            seen=set()
            while reg not in seen and len(writer.get(reg,[]))==1:
                seen.add(reg); op=writer[reg][0]
                if op['kind']!='move': return op
                reg=op['reads'][0]
            return None
        for op in ops:
            if op['kind']!='call' or len(op['reads'])<2: continue
            target,first=origin(op['reads'][0]),origin(op['reads'][1])
            if not target or target['kind']!='load_memory' or target.get('offset')!=16: continue
            base=origin(target['reads'][0])
            if base and base['kind']=='symbol_value' and base['symbol']=='type' and \
               first and first['kind']=='symbol_pointer':
                defined.add(first['symbol'])
    return defined

def module_data(doc,objects):
    referenced={r['symbol'][5:] for _,report in objects.values() for r in report['relocations']
                if r['symbol'].startswith('data:')}
    statics={s['id']:s for s in doc.get('statics',[])}
    functions={report['name'] for _,report in objects.values()}
    defined=module_defined_types(doc)
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
        # Static data may reference a type its own module deftypes later at startup.
        relocations=[dict(r,symbol='deftype:'+r['symbol'][5:]) if r['symbol'].startswith('type:') and
                     r['symbol'][5:] in defined else r for r in relocations]
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
    parser.add_argument('--native-binding',action='append',default=[],metavar='ORIGINAL=NATIVE',
                        help='Use a loaded native counterpart for an uncompilable original function')
    parser.add_argument('--all-functions',action='store_true',
                        help='Compile every non-top-level function of the IR except natively bound ones')
    args=parser.parse_args()
    if not args.function and args.initializer is None and not args.all_functions:
        parser.error('select at least one function or an initializer')
    bindings={}
    for item in args.native_binding:
        original,sep,native=item.partition('=')
        if not sep or not original or not native or original in bindings: parser.error('invalid --native-binding '+item)
        bindings[original]=native
    try:
        doc=json.loads(args.input.read_text())
        if args.all_functions:
            if args.function: parser.error('--all-functions cannot be combined with --function')
            args.function=[f['name'] for f in doc['functions'] if not f.get('top_level') and f['name'] not in bindings]
        objects=compile_module(doc,args.function,args.runtime,not args.no_optimize,args.initializer,bindings)
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
