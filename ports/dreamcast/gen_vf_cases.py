"""Expected lanes for the VU0 vf lowering checks (AI-assisted).

Writes build/vf_cases.h for kernel_boot_cases.c. The expected values never come
from the SH-4 lowering:

- add/sub/mul, masks, broadcasts, multiply-add, mov, xor and blend come from the
  independent vu0_reference.py oracle (written by ChatGPT/Codex);
- max/min/div/sqrt/ftoi/itof/outer-product come from the GOAL macro definitions
  in goalc/compiler/compilation/Asm.cpp and the x86 instructions they emit
  (maxps/minps operand order, cvttps2dq, cvtdq2ps), evaluated in IEEE binary32;
- the original vector-h functions replay their unchanged source instruction
  sequences through the oracle.

Inputs are finite normal binary32 values: the SH-4 FPU flushes denormals, which
the oracle profile treats gradually. The ftoi set adds NaN/infinity/2^31 cases.
"""
import argparse
import math
from pathlib import Path
import struct
from vu0_reference import VU0, float_bits, bits_float

F = float_bits

# (a, b, c) as floats. a.z >= 0 so the sqrt case stays in range.
GENERAL = [
    ((1.0, 2.0, 3.0, 4.0), (5.0, 6.0, 7.0, 8.0), (9.0, 10.0, 11.0, 12.0)),
    ((-1.5, 0.25, 2.0, -8.0), (3.0, -0.5, 0.125, 2.0), (100.0, -100.0, 0.5, -0.5)),
    ((0.1, -0.2, 0.3, 1e5), (0.7, 0.11, -1e-3, -3.25), (-7.0, 6.5, 1e4, 1e-4)),
    ((-0.0, 0.0, 16.0, -0.0), (0.0, -0.25, 4.0, 0.0), (1.0, -1.0, 2.0, -2.0)),
    ((123.456, -987.654, 0.5, 3.0), (-2.0, 3.5, 2.0, -0.0), (0.0, 1.0, 0.0, 1.0)),
    ((7.0, 7.0, 7.0, -7.0), (7.0, -7.0, 7.0, -7.0), (0.3, 0.6, 0.9, 1.2)),
]
FTOI = [
    (float('nan'), float('inf'), float('-inf'), 2147483648.0),
    (-2147483648.0, -2147483904.0, 2147483520.0, -2.5),
    (2.9999998, -0.9999999, 1e10, -1e10),
]


def lanes(values):
    return tuple(F(v) for v in values)


def oracle(op, a, b, c, *, extra=None):
    """Run one fixture form on the oracle: v1=a v2=b v3=c v4=c, result v4."""
    v = VU0()
    for reg, value in (('vf1', a), ('vf2', b), ('vf3', c), ('vf4', c)):
        v.set_bits(reg, value)
    regs = {'add': ('vf4', 'vf1', 'vf2'), 'madd': ('vf4', 'vf1', 'vf2', 'vf3'), 'mov': ('vf4', 'vf1')}
    v.execute(op, *regs[extra or 'add'])
    return v.bits('vf4')


def blend(a, b, mask):
    return tuple(b[i] if mask >> i & 1 else a[i] for i in range(4))


def ieee(a, b, kind):
    fa = [bits_float(x) for x in a]
    fb = [bits_float(x) for x in b]
    if kind == 'max':   # maxps dst, src1, src2: src1 if src1 > src2 else src2
        return tuple(a[i] if fa[i] > fb[i] else b[i] for i in range(4))
    if kind == 'min':   # minps: src1 if src1 < src2 else src2
        return tuple(a[i] if fa[i] < fb[i] else b[i] for i in range(4))
    if kind == 'div':   # splat a.x, splat b.y, divps
        return (F(fa[0] / fb[1]),) * 4
    if kind == 'sqrt':  # splat a.z, sqrtps
        return (F(math.sqrt(fa[2])),) * 4
    if kind == 'outer':  # swizzle 0b00001001 / 0b00010010, mul, blend(t1, t1)
        sa = (fa[1], fa[2], fa[0], fa[0])
        sb = (fb[2], fb[0], fb[1], fb[0])
        return tuple(F(x * y) for x, y in zip(sa, sb))  # exact double product, one rounding
    raise ValueError(kind)


def ftoi(a):
    out = []
    for x in (bits_float(v) for v in a):
        if math.isnan(x) or not -2147483648.0 <= x < 2147483648.0:
            out.append(0x80000000)
        else:
            out.append(int(x) & 0xffffffff)  # truncate toward zero
    return tuple(out)


def itof(a):
    return tuple(F(float(v - (1 << 32) if v & 0x80000000 else v)) for v in a)


def vector_dot_vu(a, b):
    v = VU0(); v.set_bits('vf1', a); v.set_bits('vf2', b)
    v.execute('.mul.vf', 'vf1', 'vf1', 'vf2')
    v.execute('.add.y.vf.x', 'vf1', 'vf1', 'vf1')
    v.execute('.add.z.vf.x', 'vf1', 'vf1', 'vf1')
    return v.bits('vf1')[0]


def vector4_dot_vu(a, b):
    # GOAL's rlet vf0 is an ordinary register loaded with <0,0,0,1>: use vf10.
    v = VU0(); v.set_floats('vf10', (0.0, 0.0, 0.0, 1.0))
    v.set_bits('vf1', a); v.set_bits('vf2', b); v.set_bits('vf3', (0, 0, 0, 0))
    v.execute('.mul.vf', 'vf1', 'vf1', 'vf2')
    v.execute('.xor.vf', 'vf3', 'vf3', 'vf3')
    v.execute('.add.w.vf.x', 'vf3', 'vf10', 'vf10')
    v.execute('.mul.x.vf.x', 'acc', 'vf3', 'vf1')
    v.execute('.add.mul.y.vf.x', 'acc', 'vf3', 'vf1', 'acc')
    v.execute('.add.mul.z.vf.x', 'acc', 'vf3', 'vf1', 'acc')
    v.execute('.add.mul.w.vf.x', 'vf1', 'vf3', 'vf1', 'acc')
    return v.bits('vf1')[0]


def vector_add(a, b):
    v = VU0(); v.set_floats('vf10', (0.0, 0.0, 0.0, 1.0)); v.set_bits('vf2', a); v.set_bits('vf3', b)
    v.set_bits('vf1', (0, 0, 0, 0))
    v.execute('.add.vf', 'vf1', 'vf2', 'vf3')
    v.execute('.blend.vf.w', 'vf1', 'vf1', 'vf10')
    return v.bits('vf1')


# Fixture functions: (name, expectation(a, b, c) -> 4 lanes)
FIXTURE = [
    ('dc-vf-add', lambda a, b, c: oracle('.add.vf', a, b, c)),
    ('dc-vf-sub', lambda a, b, c: oracle('.sub.vf', a, b, c)),
    ('dc-vf-mul', lambda a, b, c: oracle('.mul.vf', a, b, c)),
    ('dc-vf-add-xz', lambda a, b, c: oracle('.add.vf.xz', a, b, c)),
    ('dc-vf-mul-y', lambda a, b, c: oracle('.mul.y.vf', a, b, c)),
    ('dc-vf-sub-w-xy', lambda a, b, c: oracle('.sub.w.vf.xy', a, b, c)),
    ('dc-vf-madd-z-xyw', lambda a, b, c: oracle('.add.mul.z.vf.xyw', a, b, c, extra='madd')),
    ('dc-vf-mov-w', lambda a, b, c: oracle('.mov.vf.w', a, b, c, extra='mov')),
    ('dc-vf-xor', lambda a, b, c: oracle('.xor.vf', a, b, c)),
    ('dc-vf-max', lambda a, b, c: ieee(a, b, 'max')),
    ('dc-vf-min', lambda a, b, c: ieee(a, b, 'min')),
    ('dc-vf-div-x-y', lambda a, b, c: ieee(a, b, 'div')),
    ('dc-vf-sqrt-z', lambda a, b, c: ieee(a, b, 'sqrt')),
    ('dc-vf-ftoi', lambda a, b, c: ftoi(a)),
    ('dc-vf-itof', lambda a, b, c: itof(a)),
    ('dc-vf-outer-a', lambda a, b, c: ieee(a, b, 'outer')),
    ('dc-vf-blend-0101', lambda a, b, c: blend(a, b, 0b0101)),
]
# Original vector-h functions: (name, call kind, expectation)
# kinds: 1 float f(a,b) -> lane 0; 2 f(dst,a,b) -> 4 lanes; 3 f(dst) -> 4 lanes; 4 float f(a)
ORIGINAL = [
    ('vector-dot-vu', 1, lambda a, b, c: (vector_dot_vu(a, b), 0, 0, 0)),
    ('vector4-dot-vu', 1, lambda a, b, c: (vector4_dot_vu(a, b), 0, 0, 0)),
    ('vector+!', 2, lambda a, b, c: vector_add(a, b)),
    ('vector-zero!', 3, lambda a, b, c: (0, 0, 0, 0)),
    ('vector-reset!', 3, lambda a, b, c: lanes((0.0, 0.0, 0.0, 1.0))),
    ('dc-vf-lane-x', 4, lambda a, b, c: (a[0], 0, 0, 0)),
]


def build():
    sets = [tuple(lanes(v) for v in s) for s in GENERAL]
    general = list(range(len(sets)))
    for values in FTOI:
        sets.append((lanes(values), lanes((1.0, 1.0, 1.0, 1.0)), lanes((0.0, 0.0, 0.0, 0.0))))
    ftoi_sets = list(range(len(GENERAL), len(sets)))
    cases = []
    for name, expect in FIXTURE:
        for s in general + (ftoi_sets if name == 'dc-vf-ftoi' else []):
            cases.append((name, 0, s, expect(*sets[s])))
    for name, kind, expect in ORIGINAL:
        for s in general:
            cases.append((name, kind, s, tuple(expect(*sets[s]))))
    return sets, cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parent / 'build/vf_cases.h')
    args = parser.parse_args()
    sets, cases = build()
    lines = ['/* Generated by gen_vf_cases.py (AI-assisted). Expected lanes are independent',
             ' * of the SH-4 lowering: vu0_reference.py and IEEE binary32. */',
             '#include <stdint.h>',
             f'#define DC_VF_SETS {len(sets)}u', f'#define DC_VF_CASES {len(cases)}u',
             'static const uint32_t dc_vf_inputs[DC_VF_SETS][3][4]={']
    for s in sets:
        lines.append('  {' + ','.join('{' + ','.join(f'0x{x:08x}u' for x in v) + '}' for v in s) + '},')
    lines.append('};')
    lines.append('static const struct {const char *name;uint8_t kind,set;uint32_t expect[4];} dc_vf_cases[DC_VF_CASES]={')
    for name, kind, s, expect in cases:
        lines.append(f'  {{"{name}",{kind},{s},{{' + ','.join(f'0x{x:08x}u' for x in expect) + '}},')
    lines.append('};')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines) + '\n')
    print(f'{len(cases)} cases over {len(sets)} input sets -> {args.output}')


if __name__ == '__main__':
    main()
