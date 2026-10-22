Current extension: fixed mixed signatures now permit integer stack spills
through eight total arguments; mixed variadic/vector support remains open.
See [MIXED_STACK_ABI.md](MIXED_STACK_ABI.md). The milestone figures below are
historical. (AI-assisted).

# Fixed-signature mixed scalar register ABI

2026-10-21. (AI-assisted). Mixed integer/float signatures previously rejected
before code generation. Native entry and call lowering now use separate GPR
pair and float-register positions, matching SH-4 GCC15.1 -m4-single -ml.

The newly verified subset permits at most two 64-bit integer/tagged arguments
and eight total arguments. Mixed stack arguments and mixed variadic calls stay
rejected. Existing homogeneous integer stack calls and eight-float calls retain
their previous layout. No vector/128-bit ABI or process/thread entry is added.

Copy-only mixed returns also select their position within the correct register
stream. Integer leaves remain six bytes and float leaves four bytes, using the
RTS delay slot without a frame. Shift-leaf shortcuts reject mixed signatures
and use ordinary lowering instead. Duplicate incoming register IDs reject.

The independent GCC oracle compiles 88 return probes across eleven argument
layouts, placing integer arguments in different positions among floats. Three
new original-frontend tests cover real mixed functions/calls, compact leaves,
and rejection of mixed stack and variadic cases. Sixty-three new native checks
exercise C-to-GOAL and GOAL-to-GOAL calls, 64-bit halves, float results, live
values across calls and eight-argument register selection.

All **142 host tests, 8,399 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. Diagnostic code totals 59,584 bytes, including 26,574
bytes of deliberate far-branch probes/padding and a 3,244-byte shift baseline.
Arena use is 336,704 / 1,048,576 bytes with 267 symbols. These exclude the
executable, KOS, stacks and renderer and do not establish whole-game fit.

The unchanged original kernel compilation inventory remains: gcommon 66 typed
functions / 57 lowering individually, gkernel 76 / 50, gstate 7 / 3. Their
desktop-configured startup is not linked or executed. Explicit Dreamcast target
constants, remaining fixed/process roles, heap/scheduler/platform bridges,
vector/mixed-stack/variadic ABIs and gameplay remain unfinished. Physical
Dreamcast is available later; no CDs are prepared or burned now.

Reproduce from the repository root after building the original frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

The host ABI oracle needs SH-4 GCC; frontend integration needs the built typed
exporter. Relevant tests skip if tools are absent. Build one SH-4 target at a
time. Full mixed/variadic ABI support must not be inferred from this subset.
