# Independent vector oracle and GAME.CGO inventory (AI-assisted)

2026-10-26. This completes the two independent support tasks in
[CODEX_VU0_HANDOFF.md](CODEX_VU0_HANDOFF.md). It adds no SH-4 lowering, kernel
startup or gameplay behavior. Claude's lowering/boot files are unchanged.

## Reference profile

`vu0_reference.VU0` stores 32 architectural vectors and ACC as four uint32 lane
patterns each. `bits`/`set_bits` and the memory/GPR paths preserve signed zero,
subnormals and arbitrary NaN payloads without a Python float conversion. The
architectural `vf0` is always `(0,0,0,1)`; GOAL virtual register names must be
mapped by the caller, since a source variable named vf0 need not be architectural.

Arithmetic follows the desktop OpenGOAL operations in
`goalc/compiler/compilation/Asm.cpp`, with lane masks from
`goal_src/common/lib/vf-macros.gc`: x is bit 0. Binary32 results round to nearest,
ties to even, with gradual underflow. Multiply-add rounds the product before
adding, matching the compiler's two separate IR operations. It is not fused.
The fourth argument of active `.add.mul.*.vf` supplies the accumulator explicitly;
historical `.vmadd*` spellings use the model's ACC.

This is a **finite-input desktop arithmetic reference**, not a cycle-accurate PS2
VU model. It rejects non-finite arithmetic and overflowing multiply-add
intermediates, without partial state changes. Single add/sub/mul overflow produces
IEEE signed infinity. Non-finite raw transfers and XOR remain supported. It does
not assert SSE NaN propagation rules, exceptions, MXCSR FTZ/DAZ settings, PS2
exponent-255 finite numbers, saturation, truncation, guard bits or VU pipeline
hazards. Those require separate profiles and hardware evidence. See the
[OpenGOAL instruction reference](https://opengoal.dev/docs/reference/lib/) and
[PCSX2's VU implementation](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/VUops.cpp).

Supported operations include `.lvf`, `.svf`, `.lqc2`, `.sqc2`, `.qmfc2[.i]`,
`.qmtc2`, `.mov.vf`, `.blend.vf`, `.xor.vf`, add/sub/mul and explicit multiply-add,
all four broadcast fields and destination masks. The historical `.vmul`,
`.vmulax`, `.vmadday`, `.vmaddaz`, `.vmaddw`, `.vaddw`, `.vaddy` and `.vaddz`
families are aliases under the same documented desktop arithmetic profile.
Memory uses an explicit bytearray with aligned, bounded 16-byte transfers.
Instructions are API calls, not executable GOAL source strings.

```python
from vu0_reference import VU0
v = VU0()
v.set_floats('vf1', (1, 2, 3, 4))
v.set_floats('vf2', (5, 6, 7, 8))
v.execute('.mul.vf.xyz', 'vf3', 'vf1', 'vf2')
assert v.floats('vf3') == (5, 12, 21, 0)
```

Hand-checked tests cover every required operation, all 16 numeric masks, all
15 nonempty destination suffixes, broadcasts, aliasing, accumulator preservation,
rounding ties, underflow, bitwise payloads and refusal behavior. An independent
SSE helper compares 2,048 four-lane vectors across add/sub/mul/multiply-add with
explicit RN-even/gradual settings; at least 24,576 lane comparisons run, with
overflowing multiply-add cases excluded from the declared profile. That helper
is host test code; no embedding binary is published.

## Measured source inventory

`survey_vu0_usage.py` reads GAME.CGO manifest order and produces
[evidence/vu0-usage.json](evidence/vu0-usage.json). It covers all 346 entries:
326 GOAL source files and 20 data objects, explicitly marked as unsurveyed data.
There are 222 syntactic rlet blocks declaring vf registers. The first 40 entries
are also presented separately, and the whole archive has an operation frequency
table. Leading forms include `.lvf` (904), `.svf` (450), `.mov` (339),
`.mul.x.vf` (145), `.mul.vf` (116) and `.sub.vf` (111).

The inventory excludes line/block comments and strings, including commented
historical PS2 instructions. It includes all conditional branches and quoted
macro templates without expansion. Thus it measures source occurrences, not
dynamic instruction counts or successful compilation. DMA/VIF/VU1 counts are
symbol indicators (including names and types), not confirmed platform blockers.
Sources missing or ambiguous for a code object refuse. Source and manifest
hashes normalize CRLF to LF so reports reproduce across Windows and Linux.

```text
python ports/dreamcast/survey_vu0_usage.py --check ports/dreamcast/evidence/vu0-usage.json
python -m unittest discover -s ports/dreamcast/tests -p "test_vu0*.py" -v
python -m unittest discover -s ports/dreamcast/tests
```

Next: compare emitted SH-4 lane bit patterns against this independent reference,
then test original vector-h through the native GAME.CGO loader. Host validation
of this oracle does not establish Dreamcast arithmetic or gameplay correctness.

Validation receipt: [evidence/vu0-reference-verification.json](evidence/vu0-reference-verification.json).
The full host suite discovered 471 tests: 470 passed, one optional C++ background
exporter test skipped. All 17 new tests ran. All 326 source hashes were independently
checked against Git blobs. The updated exporter was built as a separate executable
using the unchanged original compiler/dependency libraries; the existing frontend
and local initialization reproduction were preserved.
