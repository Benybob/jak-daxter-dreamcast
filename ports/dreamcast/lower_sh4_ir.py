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
    if not floating and result>=2: return None  # stack argument needs the incoming frame
    e=Emitter()
    if floating:
        e.word(0x000b); e.fmov(0,FLOAT_ARGUMENT_REGISTERS[result])
    else:
        e.mov(0,4+result*2); e.word(0x000b); e.mov(1,5+result*2)
    data,relocations=e.finish()
    return data,{'name':function['name'],'bytes':len(data),'stack_bytes':0,
                 'relocations':relocations,'optimization':'coalesced_argument_return',
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

def compile_function(function,module=None,runtime=False,optimize=True,statics=None):
    if runtime:
        # Preserve semantic types in exported IR, use original-type-system ABI
        # classifications locally rather than guessing from user type names.
        function=dict(function,
            return_type=function.get('return_abi_type',function.get('return_type')),
            registers=[dict(r,type=r.get('abi_type',r['type'])) for r in function['registers']],
            parameters=[dict(p,type=p.get('abi_type',p['type'])) for p in function['parameters']])
    if function.get('asm'): raise ValueError('inline/assembly functions unsupported')
    if function.get('implicit_parameters'): raise ValueError('implicit process/self entry ABI not implemented')
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
    fixed={}
    for binding in function.get('fixed_registers',[]):
        index=binding['register']
        if index in fixed or index not in registers: raise ValueError('invalid fixed register metadata')
        if not runtime: raise ValueError('fixed runtime register requires native runtime profile')
        if binding.get('frontend_register')!='r14' or binding.get('role')!='symbol_table':
            raise ValueError('fixed runtime register role not implemented')
        if registers[index]['class']!='gpr64' or registers[index]['type'] not in ('uint','int','integer'):
            raise ValueError('fixed symbol-table register requires an integer container')
        fixed[index]=binding
    translated=[]
    for op in function['instructions']:
        if op['kind']=='fixed_register_reset':
            if op['reads'] or len(op['writes'])!=1 or len(op.get('bindings',[]))!=1:
                raise ValueError('unsupported fixed register reset layout')
            index=op['writes'][0]
            if index not in fixed or op['bindings'][0]!=fixed[index]:
                raise ValueError('fixed register reset does not match its declaration')
            op=dict(op,kind='symbol_pointer',symbol='#f')
        elif set(op['writes'])&fixed.keys():
            raise ValueError('fixed symbol-table register mutation is unsupported')
        translated.append(op)
    function=dict(function,instructions=translated)
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
    if len({kind=='float' for kind in parameter_types})>1:
        raise ValueError('mixed integer/float argument ABI not verified')
    for kind in parameter_types:
        if kind not in value_types|{'float'}:
            raise ValueError('bootstrap ABI currently accepts numeric parameters only' if not runtime else 'unsupported tagged/scalar parameter')
    if len(parameter_types)>8: raise ValueError('more than eight arguments unsupported')
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
    # Preserve GCC callee-saved address scratch registers and PR.
    e.word(0x4f22)
    for reg in (14,8,9): e.rr(0x2006,15,reg)
    adjust_stack(-frame); e.mov(14,15)
    gpr_args=float_args=0
    if len({p['type']=='float' for p in function['parameters']})>1:
        raise ValueError('mixed integer/float argument ABI not verified')
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
    used={value for op in instructions for value in op['reads']}
    for position,op in enumerate(instructions):
        e.label('ir_'+str(position))
        kind=op['kind']; reads=op['reads']; writes=op['writes']
        try:
            if kind=='nop': continue
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
                if not item or item['kind'] not in ('string','float','pair'):
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
                if len(kinds)>1: raise ValueError('mixed integer/float call ABI not verified')
                outgoing=max(0,len(parameters)-2)*8 if kinds=={False} else 0
                adjust_stack(-outgoing)
                for index,(argument,parameter) in enumerate(zip(args,parameters)):
                    if parameter['type']=='float':
                        if index>=8 or registers[argument]['type']!='float': raise ValueError('unsupported float call argument')
                        native=FLOAT_ARGUMENT_REGISTERS[index]
                        if registers[argument]['class']=='float': load_float(argument,native)
                        else:
                            load64(argument); e.word(0x405a); e.word(0xf00d|(native<<8))
                    else:
                        if scalar_kind(parameter['type'],runtime) not in value_types or scalar_kind(registers[argument]['type'],runtime) not in value_types:
                            raise ValueError('unsupported integer call argument')
                        if index<2: load64(argument,4+index*2,5+index*2)
                        else:
                            load64(argument); e.mov(2,15); e.add_immediate(2,(index-2)*8)
                            e.store(2,0); e.add_immediate(2,4); e.store(2,1)
                load64(reads[0]); e.word(0x400b); e.nop()  # jsr @r0, PR saved in prologue
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
    e.label('epilogue'); e.mov(15,14); adjust_stack(frame)
    for reg in (9,8,14): e.rr(0x6006,reg,15)
    e.word(0x4f26); e.ret()
    data,relocations,layout=e.finish_with_stats(relax=True,literal_islands=True)
    instruction_bytes=layout['instruction_bytes']
    return data,{'name':function['name'],'bytes':len(data),'stack_bytes':frame+16,'relocations':relocations,
                'optimization':'stack_slot_reuse' if optimize else 'stack_baseline','instruction_stream_bytes':instruction_bytes,
                'stack_slot_count':frame//8,'virtual_register_count':len(registers),
                'baseline_stack_bytes':len(registers)*8+16,'coalesced_stack_moves':coalesced_moves,
                'expanded_conditional_branches':layout['expanded_conditional_branches'],
                'literal_islands':layout['literal_islands'],
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
