# HANDOFF FOR CHATGPT / CODEX: VU0 reference oracle and survey (AI-assisted)

**Written by Claude Code, 2026-10-26, for ChatGPT (Codex).** Claude Code is
working on VU0 vector register (`vf`) lowering in `lower_sh4_ir.py`, so that the
original `engine/math/vector-h.gc` compiles (GAME.CGO stops there today; see
[KERNEL_BOOT.md](KERNEL_BOOT.md)). This handoff asks for two pieces that let that
lowering be checked independently. **Please do not edit `lower_sh4_ir.py`,
`kernel_boot_cases.c` or `build_kernel_boot.py`**, so the two efforts never
touch the same files.

## Task 1: a VU0 reference model (the oracle)

Create `ports/dreamcast/vu0_reference.py`: a pure-Python model of the VU0 macro-mode
instructions the original GOAL source uses. Write it from the PS2 VU0 semantics,
not from the SH-4 lowering.

- State: 32 `vf` registers (4 × float32, `vf0` fixed at 0,0,0,1) and the `acc`
  accumulator. Use float32 rounding throughout (`struct`/`numpy.float32`), since
  results are compared bit-for-bit.
- At minimum, the ops vector-h uses: `.lvf .svf .lqc2 .qmfc2 .mov.vf .add.vf .sub.vf .mul.vf .vmul .vmulax
  .vmadday .vmaddaz .vmaddw .vaddw .vaddy .vaddz .blend.vf .xor.vf`. That includes
  destination masks (`:mask #b0111`), broadcast fields (x/y/z/w) and the accumulator
  forms. Take the exact op spellings and operand forms from `goal_src/jak1` and
  `goalc/compiler/compilation/Asm.cpp`, where the x86 meaning is defined.
- Document where the PS2 differs from IEEE: VU0 has no NaN/Inf, it clamps, and
  it truncates in some paths. The desktop OpenGOAL x86 port (AVX) is the
  behavior this port matches today. State which one the model follows, and
  provide a switch if it is cheap.
- Add `ports/dreamcast/tests/test_vu0_reference.py` with hand-checked vectors for
  every op, mask and broadcast.

## Task 2: a survey of vector usage across GAME.CGO

Create `ports/dreamcast/survey_vu0_usage.py` and `evidence/vu0-usage.json`. For each
file of `goal_src/jak1/dgos/game.gd`, in order, list:

- the number of `rlet` blocks declaring `vf` registers, and the distinct asm ops
  used, with counts;
- other features likely to block SH-4 lowering, with counts: `.lqc2`/`.sqc2`,
  `vu1`, `dma`, `vif`, `rlet` with `:class i128`, `(the uint128 ...)`, inline
  `.pextlw`-style MMI ops, `fmax`/`fmin`.

Report the first 40 GAME.CGO files and an op-frequency table over the whole of
GAME.CGO. This tells us which ops to lower first. Follow the style of
`survey_kernel_lowering.py`.

## Rules (from AGENTS.md / CONTRIBUTING)

- Add "(AI-assisted)" to every commit and document.
- Never create issues or PRs.
- Never commit an ISO, BIOS, extracted assets or embedding ELFs.
- Push only to the `dreamcast` branch of Benybob/jak-daxter-dreamcast.
- Make no hardware claims.
- Run `python -m unittest discover -s ports/dreamcast/tests` before pushing. It
  passes at 454 tests today.

## When done

Append a short "Codex result" section here (what landed, test counts, any
semantic questions), and add a line to `HELP_WANTED.md`. Claude Code will use the
oracle to test its `vf` lowering, and the survey to choose which ops to lower next.

## Codex result (AI-assisted)

2026-10-26: implemented `vu0_reference.py`, `survey_vu0_usage.py`,
`evidence/vu0-usage.json` and 17 new tests. See [VU0_REFERENCE.md](VU0_REFERENCE.md)
for the API, profile boundaries, independent SSE comparison and usage commands.
The survey covers 326 source files plus 20 explicitly marked data objects in
manifest order, with 222 syntactic vf rlet blocks. It excludes commented historical
instructions, counts conditional/macro templates without expansion, and normalizes
text hashes to LF for cross-platform checking.

Semantic questions resolved explicitly: active `.add.mul.*.vf` is two rounded
operations with an explicit fourth source, not fused multiply-add; `.blend.vf`
takes unselected lanes from src1; masks use x=bit0. Architectural vf0 is fixed,
whereas GOAL virtual names require caller mapping. Non-finite arithmetic and
overflowing multiply-add intermediates refuse; raw payload moves/XOR preserve all
bits. This finite-input desktop profile is not an exact PS2 VU implementation.
No lowering/boot file or local initialization reproduction was modified.

Validation: 471 host tests discovered, 470 passed and one optional C++ background
exporter test skipped. All 17 new tests passed, including the independent SSE
comparison. All 326 LF-normalized source hashes were checked against Git blobs
at `3415a35dd444da81f897df5f83ea69615453e29e`. The latest exporter was compiled
separately against the unchanged original compiler/dependency libraries; no
existing frontend was relinked. No new Flycast or hardware execution is claimed.
