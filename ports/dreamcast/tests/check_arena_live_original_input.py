"""Replay original typed input IR on the HOST, never SH-4. (AI-assisted), ISC.

This diagnostic reads existing frontend output; it does not emulate a target
or run Flycast. It separates the skipped debug default from input dead zones.
"""
import json
from pathlib import Path
import struct

PORT = Path(__file__).resolve().parents[1]

def bits(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]

def real(b):
    return struct.unpack('<f', struct.pack('<I', b & 0xffffffff))[0]

def replay(layout, *, circle=False, l2=False, right=128, left=128, ratio=4):
    document = json.loads((PORT / 'build/game_cam_states_dbg.ir.json').read_text())
    code = next(f['instructions'] for f in document['functions'] if f['name'] == 'cam-free-floating-input')
    false, true = 0x100004, 0x10000c
    pointers = {'#f': false, '#t': true, 'menu': 0x100014}
    symbols = {'*master-mode*': 0x10001c, '*cam-layout*': layout,
               '*camera-read-buttons*': true, '*camera-read-analog*': true,
               '*cam-free-move-along-z*': false, '*display-load-boundaries*': false,
               '*cpad-list*': 0x100, '*CAM_FREE-bank*': 0x300, '*display*': 0x400,
               '*pc-settings*': 0x900, 'analog-input': 0x123456, 'vector-float*!': 0x123457}
    memory = bytearray(4096)
    def put(at, value, size=4):
        memory[at:at+size] = value.to_bytes(size, 'little')
    put(0x104, 0x200)
    put(0x228, (8192 if circle else 0) | (256 if l2 else 0))
    for offset,value in ((4,right),(5,128),(6,left),(7,128)):
        put(0x200+offset,value,1)
    put(0x300, bits(1600)); put(0x304, bits(65536*.6/360))
    put(0x400+916, bits(ratio))
    # PC first-person settings change sign only; both flags are false.
    regs = {0: 0x800, 1: 0x810, 2: false, 3: 0}
    pc, steps = 0, 0
    while pc < len(code):
        op = code[pc]; kind = op['kind']; pc += 1; steps += 1
        assert steps < 5000
        args = [regs[r] for r in op['reads']] if kind != 'nop' else []
        value = None
        if kind == 'nop': continue
        if kind == 'move': value = args[0]
        elif kind == 'symbol_value': value = symbols[op['symbol']]
        elif kind == 'symbol_pointer': value = pointers[op['symbol']]
        elif kind == 'constant': value = op['value']
        elif kind == 'float_constant': value = op['value_bits']
        elif kind == 'shl64': value = args[0] << op['shift']
        elif kind == 'add64': value = sum(args)
        elif kind == 'and64': value = args[0] & args[1]
        elif kind == 'xor64': value = args[0] ^ args[1]
        elif kind == 'load_memory':
            at = args[0]+op['offset']
            value = int.from_bytes(memory[at:at+op['size']], 'little', signed=op['signed'])
        elif kind == 'store_memory': put(args[1]+op['offset'], args[0], op['size'])
        elif kind == 'int_to_float': value = bits(args[0])
        elif kind in ('add_float','sub_float','mul_float','div_float'):
            a,b = map(real,args)
            value = bits({'add_float': lambda:a+b, 'sub_float':lambda:a-b,
                          'mul_float':lambda:a*b, 'div_float':lambda:a/b}[kind]())
        elif kind == 'branch':
            a,b = map(real,args) if op['float'] else args
            if {'eq':lambda:a==b, 'ne':lambda:a!=b, 'lt':lambda:a<b,
                'ge':lambda:a>=b, 'le':lambda:a<=b, 'gt':lambda:a>b}[op['condition']](): pc=op['target']
        elif kind == 'goto': pc=op['target']
        elif kind == 'call':
            if args[0] == symbols['vector-float*!']:
                for lane in range(3):
                    old = struct.unpack_from('<f',memory,args[2]+lane*4)[0]
                    put(args[1]+lane*4,bits(old*real(args[3])))
                regs[op['writes'][0]]=args[1]
                continue
            assert args[0] == symbols['analog-input']
            raw,offset,dead,maximum,scale=args[1],*map(real,args[2:])
            magnitude=max(0,abs(raw-offset)-dead)
            if raw < offset: scale=-scale
            value=bits(min(magnitude/(maximum-dead),1)*scale)
        elif kind == 'return': break
        else: raise AssertionError(op)
        if value is not None: regs[op['writes'][0]]=value
    return struct.unpack_from('<3f',memory,0x800),struct.unpack_from('<3f',memory,0x810)


def main():
    false=0x100004
    rows=[]
    for layout,label in ((0,'skipped default (truthy zero)'),(false,'initialized GOAL false')):
        rv,tv=replay(layout,circle=True,l2=True)
        expected=0 if layout==0 else -436.9066666667
        assert abs(rv[1]-expected)<.0001 and tv==(0.,0.,0.)
        strafe=replay(layout,left=180)[1]
        assert abs(strafe[0]+825.8064516)<.0002
        rows.append(dict(layout=label,circle_l2_rv=list(rv),tv=list(tv),strafe_tv=list(strafe)))
    for ratio in (1,1.2,4,4.8):
        rv,tv=replay(false,right=207,ratio=ratio)
        assert abs(abs(rv[1])-65536*.6/360*ratio)<.0001 and tv==(0.,0.,0.)
    # Centered analog SUBTRACTS from rv; it does not erase digital yaw.
    assert replay(false,circle=True)[0] == replay(false,circle=True,l2=True)[0]
    # Holding L2 with a left stick legally moves as well as turning.
    rv,tv=replay(false,circle=True,l2=True,left=180)
    assert rv[1]<0 and tv[0]<0
    report=dict(ai_assisted=True,license='ISC',host_typed_ir_only=True,cases=rows)
    (PORT/'build/arena-live-validation/original-input.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__': main()
