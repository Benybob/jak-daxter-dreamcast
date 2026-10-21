# Native 64-bit shifts and compact SH-4 leaves

2026-10-21. AI-assisted implementation. The complete compiler/kernel/game is unfinished.

The original typed integer IR now lowers constant and variable left, logical
right and arithmetic right shifts. Values remain 64-bit containers represented
by SH-4 register halves. Cross-half bits and the 32..63 count paths are explicit.
Counts follow the pinned x86 backend's six-bit mask, including negative and
large count containers. No interpreter, C translation or runtime math helper
is used for the emitted shifts.

Constants select only their relevant instruction path. Pure argument/copy/shift
returns use the GCC registers directly, with no stack frame. Zero shifts become
six-byte argument returns. Left/right shifts by 32 use six bytes, including a
useful RTS delay slot; arithmetic right by 32 uses ten. General dynamic left
and arithmetic-right return leaves use 50 and 56 bytes. Stack-passed operands,
calls and more complex functions retain the normal validated frame path.

The twenty-function shift fixture uses **836 code bytes optimized versus 3,244
with optimizations disabled**, a 74.2% reduction for that same source/module.
For example left-by-32 is six bytes/no frame, versus the earlier 76-byte/
24-byte-frame path. These are measured code/stack sizes, not hardware timing or
cache-miss results. Per-function manifests retain the details.

The unchanged original `ash` definition is selected directly from
`goal_src/jak1/kernel/gcommon.gc`, including its signed direction branch. Its
native body is 330 bytes with a 40-byte frame. The target checks every count
0..63 and fifteen negative/large-count cases over seven bit-pattern values.
It checks all three primitive shifts, original ash, self-count aliases, linked
call chains with live values and constant boundary cases. It then loads an
unoptimized module and checks both native versions against each other.

The independent Windows x86-64 oracle executes actual SHL/SHR/SAR instructions
and passes **1,659 cases** against the portable expected-bit calculation. GNU
SH-4 assembler verifies the new instruction sequences and delay-slot leaves.
The broader host suite passes **90 tests**. Flycast with stock 16 MiB passes
**5,980 runtime checks**, including the preceding 775 checks and the expanded
shift matrices. The count reflects many shift vectors, not complete game coverage.

The combined diagnostic emits 19,742 code bytes, **including the separate
3,244-byte baseline module**. Arena use is 292,512 / 1,048,576 bytes with 188
installed symbols. These are diagnostic footprints, excluding C executable,
KOS, ordinary stacks/allocator and the separate renderer.

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
```

Primary contracts: `goalc/compiler/IR.cpp` shift emitters, the original `ash`
body and GNU SH-4 assembler. SHAD/SHLD behavior is also specified in the
[Renesas SH-4 software manual](https://www.renesas.com/en/document/mas/sh-4-software-manual).
Mixed/vector ABIs, other unsupported integer operations and broad kernel/game
execution remain future work. Physical Dreamcast performance is unverified.
