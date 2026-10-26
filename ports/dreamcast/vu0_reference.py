"""Independent vector arithmetic oracle (AI-assisted).

Profile: OpenGOAL desktop operations, IEEE binary32 round-to-nearest/even,
gradual underflow, separate rounded multiply then add (NOT fused). Masks use
x=bit0, as in goalc/compiler/compilation/Asm.cpp and common/lib/vf-macros.gc.
Historical PS2 spellings are aliases with an implicit ACC; active OpenGOAL
.add.mul.*.vf forms take an explicit fourth source. This is not a GOAL reader.

This does NOT emulate PS2 VU exponent-255 finite numbers, saturation, denormal
handling, guard bits or truncation. Nor does it model MXCSR FTZ/DAZ or exception
flags. Non-finite arithmetic is rejected rather than claiming portable SSE NaN
payload behavior. Raw moves, transfers, XOR and memory preserve every bit.
vf0 is architectural and immutable here; a GOAL rlet variable named vf0 can be
an ordinary allocated register, so callers must map virtual names explicitly.

Sources: local Asm.cpp, vf-macros.gc; https://opengoal.dev/docs/reference/lib/;
https://github.com/PCSX2/pcsx2/blob/master/pcsx2/VUops.cpp (PS2 distinctions).
"""
import math
import re
import struct


def float_bits(value):
    """Round a Python number to binary32, with IEEE signed overflow."""
    try:
        return struct.unpack('<I', struct.pack('<f', value))[0]
    except OverflowError:
        return 0xff800000 if value < 0 else 0x7f800000


def bits_float(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


def _mask(value):
    if isinstance(value, str):
        value = int(value[2:], 2) if value.startswith('#b') else int(value, 0)
    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 15:
        raise ValueError('mask must be a four-bit integer')
    return value


def _lanes(values):
    values = tuple(values)
    if len(values) != 4 or any(type(x) is not int or not 0 <= x <= 0xffffffff for x in values):
        raise ValueError('expected four uint32 lane bit patterns')
    return values


class VU0:
    """32 architectural vectors, ACC, little-endian byte memory, raw GPRs.

    execute(op, dest, src1, src2[, src3], mask=15) handles register operations.
    Memory: .lvf dest,address; .svf address,src; .lqc2 dest,offset,base;
    .sqc2 src,offset,base. Transfers: .qmfc2[.i] gpr,vf; .qmtc2 vf,gpr.
    Addresses are byte offsets into caller-provided writable bytearray memory;
    transfers require alignment and bounds. No pointer dereference or emulated
    game execution occurs. All rejected operations leave state unchanged.
    """
    def __init__(self, memory=None):
        if memory is not None and not isinstance(memory, bytearray):
            raise ValueError('memory must be a bytearray')
        self.memory = bytearray() if memory is None else memory
        self._vf = [(0, 0, 0, 0) for _ in range(32)]
        self._acc = (0, 0, 0, 0)
        self.gpr = {}

    @staticmethod
    def _index(reg):
        if isinstance(reg, str) and re.fullmatch(r'vf\d+', reg):
            reg = int(reg[2:])
        if type(reg) is not int or not 0 <= reg < 32:
            raise ValueError('expected vf0..vf31')
        return reg

    def bits(self, reg):
        if reg == 'acc':
            return self._acc
        i = self._index(reg)
        return (0, 0, 0, 0x3f800000) if i == 0 else self._vf[i]

    def floats(self, reg):
        return tuple(map(bits_float, self.bits(reg)))

    def set_bits(self, reg, values, mask=15):
        values, mask = _lanes(values), _mask(mask)
        old = self.bits(reg)
        result = tuple(values[i] if mask & (1 << i) else old[i] for i in range(4))
        if reg == 'acc':
            self._acc = result
        elif self._index(reg) != 0:
            self._vf[self._index(reg)] = result

    def set_floats(self, reg, values, mask=15):
        self.set_bits(reg, tuple(map(float_bits, values)), mask)

    def _address(self, address, offset):
        if type(address) is not int or type(offset) is not int:
            raise ValueError('address and offset must be integers')
        address += offset
        if address < 0 or address % 16 or address + 16 > len(self.memory):
            raise ValueError('unaligned or out-of-bounds vector address')
        return address

    def execute(self, op, *args, mask=15, offset=0):
        mask = _mask(mask)
        if not isinstance(op, str):
            raise ValueError('instruction spelling must be a string')
        if op in ('.lvf', '.svf', '.lqc2', '.sqc2'):
            if mask != 15:
                raise ValueError('memory transfers do not accept masks')
            if op in ('.lvf', '.svf') and len(args) == 2:
                reg, address = args if op == '.lvf' else (args[1], args[0])
            elif op in ('.lqc2', '.sqc2') and len(args) == 3 and offset == 0:
                reg, offset, address = args
            else:
                raise ValueError('invalid memory operands')
            self.bits(reg)  # validate before mutation
            address = self._address(address, offset)
            if op in ('.lvf', '.lqc2'):
                self.set_bits(reg, struct.unpack_from('<4I', self.memory, address))
            else:
                struct.pack_into('<4I', self.memory, address, *self.bits(reg))
            return
        if offset != 0:
            raise ValueError('offset only applies to memory instructions')
        if op in ('.qmfc2', '.qmfc2.i', '.qmtc2'):
            if len(args) != 2 or mask != 15:
                raise ValueError('invalid transfer operands')
            dest, src = args
            if op == '.qmtc2':
                if src not in self.gpr:
                    raise ValueError('uninitialized GPR')
                value = self.gpr[src]
                if type(value) is not int or not 0 <= value < (1 << 128):
                    raise ValueError('GPR transfer requires a uint128 bit pattern')
                self.set_bits(dest, tuple((value >> (32*i)) & 0xffffffff for i in range(4)))
            else:
                value = sum(x << (32*i) for i, x in enumerate(self.bits(src)))
                if not isinstance(dest, str):
                    raise ValueError('GPR name must be a string')
                self.gpr[dest] = value
            return
        # Active OpenGOAL spelling: .add[.x].vf[.xyz], .add.mul[.y].vf[.x].
        active = re.fullmatch(r'\.(mov|blend|xor|add|sub|mul|add\.mul)(?:\.([xyzw]))?\.vf(?:\.([xyzw]+))?', op)
        # Historical VU spelling: .vmulax.x acc,vf1,vf2 etc.
        legacy = re.fullmatch(r'\.v(add|sub|mul|madd|mula|madda)([xyzw])?(?:\.([xyzw]+))?', op)
        if not active and not legacy:
            raise ValueError('unsupported vector instruction: ' + op)
        kind, broadcast, suffix = (active or legacy).groups()
        if suffix:
            if len(set(suffix)) != len(suffix) or ''.join(c for c in 'xyzw' if c in suffix) != suffix:
                raise ValueError('invalid destination field order')
            if mask != 15:
                raise ValueError('use either suffix or mask, not both')
            mask = sum(1 << 'xyzw'.index(c) for c in suffix)
        if broadcast and kind in ('mov', 'blend', 'xor'):
            raise ValueError('unsupported broadcast')
        count = 2 if kind == 'mov' else 4 if kind == 'add.mul' else 3
        if len(args) != count:
            raise ValueError('invalid register operand count')
        dest = args[0]
        if kind in ('mula', 'madda') and dest != 'acc':
            raise ValueError('accumulator instruction requires ACC destination')
        self.bits(dest)
        a = self.bits(args[1])
        if kind == 'mov':
            self.set_bits(dest, a, mask)
            return
        b = self.bits(args[2])
        if broadcast:
            b = (b['xyzw'.index(broadcast)],) * 4
        c = self.bits(args[3]) if kind == 'add.mul' else self.bits('acc')
        if kind == 'blend':
            # Unselected fields come from src1, NOT old destination.
            self.set_bits(dest, tuple(b[i] if mask & (1 << i) else a[i] for i in range(4)))
            return
        result = list(self.bits(dest))
        for i in range(4):
            if not mask & (1 << i):
                continue
            if kind == 'xor':
                result[i] = a[i] ^ b[i]
                continue
            x, y = bits_float(a[i]), bits_float(b[i])
            if not math.isfinite(x) or not math.isfinite(y):
                raise ValueError('non-finite arithmetic outside oracle profile')
            if kind == 'add':
                result[i] = float_bits(x + y)
            elif kind == 'sub':
                result[i] = float_bits(x - y)
            else:
                product = float_bits(x * y)
                if kind in ('madd', 'madda', 'add.mul'):
                    z = bits_float(c[i])
                    if not math.isfinite(z) or not math.isfinite(bits_float(product)):
                        raise ValueError('non-finite multiply-add outside oracle profile')
                    product = float_bits(bits_float(product) + z)
                result[i] = product
        self.set_bits(dest, result, mask)
