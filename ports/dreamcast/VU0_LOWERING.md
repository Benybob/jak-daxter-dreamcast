# VU0 vector registers on SH-4 (AI-assisted)

2026-10-27, Claude Code. The SH-4 lowerer now handles GOAL's 128-bit VU0 vector
registers (`vf`). The original `engine/math/vector-h.gc` compiles, and GAME.CGO
now compiles and loads its first **13** original files, through transformq-h.
In stock 16 MiB Flycast, **141 of 141 vector cases match bit for bit**
expectations computed independently of the lowering. The DGO boot passes
**112 of 112** checks.

## What the lowerer sees

The pinned OpenGOAL frontend has already expanded every GOAL vector macro
(`.add.y.vf.x`, `.add.mul.z.vf`, `.outer.product.a.vf`, `.div.vf :fsf :ftf`,
...) into a small set of desktop x86 IR operations. The exporter passes these on
only as their printed text. `vector_asm` in `lower_sh4_ir.py` parses that text
strictly:

- colored virtual `ivf-N` operands only, never `:no-color` physical registers;
- operand ids must equal the op's exported read and write lists;
- masks, elements and control bytes must be in range.

Anything else stays refused.

| IR op | SH-4 lowering | Semantics source (x86, `goalc/emitter/IGenX86.cpp`) |
| --- | --- | --- |
| `.add/.sub/.mul/.div.vf` | per-lane `FADD/FSUB/FMUL/FDIV` | `addps/subps/mulps/divps` |
| `.max/.min.vf` | per-lane `FCMP/GT` + select | `maxps/minps`: src1 if src1 > src2 (resp. <), else src2, including NaN and signed-zero choices |
| `.xor.vf` | four 32-bit `XOR` | `vpxor`; `x ^ x` becomes an explicit zero that reads nothing |
| `.sqrt.vf` | per-lane `FSQRT` | `sqrtps` |
| `.itof.vf` | per-lane `LDS FPUL` + `FLOAT` | `cvtdq2ps` |
| `.ftoi.vf` | per-lane `FTRC` with the scalar 2^31 threshold | `cvttps2dq`: truncate; NaN and out-of-range give 0x80000000 |
| `.blend.vf` | 32-bit lane copies | `vblendps`: lane i from src2 when mask bit i is set |
| `.swizzle.vf`, `.splat.vf` | 32-bit lane copies | `vshufps`: lane i = src[(control >> 2i) & 3] |
| vf â†” vf / int128 | 128-bit copy | `movaps` |
| float â†’ vf | lane 0, others kept | `movss` (register form) |
| vf â†’ float | lane 0 | `movss` |
| gpr â†’ vf / vf â†’ gpr | low 64 bits (zero-extended) | `movq` |
| `.lvf/.svf` | four aligned word transfers | 16-byte loads/stores |
| static vector â†’ vf | `static_load`: address + quadword load | `IR_StaticVarLoad` (new exporter kind) |

vf values live in dedicated 16-byte frame slots, like int128 values. They never
cross the call ABI. A vector parameter or return refuses.

**Uninitialized lanes.** Original VU0 code leaves unused lanes undefined; for
example, `vector4-dot-vu`'s `acc` is never initialized. On the PS2 and x86 those
lanes are whatever the register held. Natively, each vf slot is zero-filled in
the prologue and counts as defined on entry, so results are deterministic.
Definite assignment stays strict for every other register class.

**A (none) function returning a (none) value.** transformq-h's top level ends
in a discarded `method-set!` result. A void return of a `none`-typed register
now carries nothing.

## Verification

`vf_fixture.gc` applies 17 original GOAL vector forms in the engine's
rlet/VU0-macro style, plus a vf â†’ fpr lane move. The unchanged vector-h
functions are also checked: `vector-dot-vu`, `vector4-dot-vu`, `vector+!`,
`vector-zero!` and `vector-reset!`. `gen_vf_cases.py` computes every expected
lane without the lowering:

- add/sub/mul, masks, broadcasts, multiply-add, mov, xor and blend come from the
  independent [vu0_reference.py](VU0_REFERENCE.md) oracle, written by
  ChatGPT/Codex;
- max/min/div/sqrt/ftoi/itof/outer product come from the GOAL macro
  definitions in `goalc/compiler/compilation/Asm.cpp` and their x86
  instructions, evaluated in IEEE binary32;
- the original vector-h functions replay their unchanged source instruction
  sequences through the oracle.

The DGO boot runs all 141 cases in Flycast and compares the four lanes bit for
bit. To confirm the comparison can fail, one expected lane of `dc-vf-mul` was
deliberately flipped by one bit. Flycast then reported exactly that case and
its real lanes, and the expectations were regenerated. 7 new host tests cover
the strict parser's acceptances and refusals and the hand-checked expectations.
All suites were re-run after the lowerer change: 24,846 runtime checks, 39 numeric
checks, the 87-check compiled-in boot, the 112-check DGO boot, and 478 host tests
(one optional exporter test skipped).

Two findings from replaying the code rather than the comments:
- `vector+!` sets w to 1.0, from the static `<0,0,0,1>`, although its docstring
  says 0.
- `.outer.product.a.vf` sets w to a.xÂ·b.x, although its comment says w is left alone.

The port matches the code, since that is what the desktop build runs.

**Profile limits.**
- Inputs are finite normal binary32 values. The SH-4 FPU flushes denormals,
  while the oracle models gradual underflow.
- Division by zero is not exercised, because it is an FPU exception case on
  SH-4.
- No PS2 VU-exact behavior (clamping, exponent-255 finites) is claimed. This
  matches the desktop OpenGOAL profile.
- No hardware claims.

## Performance: not yet

This pass is for exactness: every lane is a separate FPU operation through
memory slots. sh4zam's idioms (`FIPR` dot products, `FTRV` matrix transforms
through XMTRX) are the planned speed pass. It will recognize expanded sequences
such as the dot product's mul + splat + add + blend and emit one `FIPR`, checked
against these same 141 cases.

## Next

bounding-box, the 14th GAME.CGO file, refuses with "unsupported tagged/scalar
parameter". The arena is at 97% of 4 MiB after the 13-file prefix, so the boot
arena needs to grow before many more engine files load.
