# Original function early returns on SH-4

2026-10-21. (AI-assisted). The original frontend's IR_Return assigns a result
register. Early `(return value)` paths copy to the outer function block's
shared register and jump to its final null label. Treating IR_Return as an
immediate machine return left that final label without a native return.

The exporter now recognizes the outer function block by its actual parent,
keeps every original instruction index, exports its normal return assignment
as a move, and turns its final null into the single native return. The result
register receives the original frontend's common result type. Existing CFG,
liveness, definite assignment and SH-4 return ABI code handle all paths.
Normal leaf functions keep their existing layout: identity remains six bytes.
Nested block exits retain their own destinations. No game bodies were changed.

Seven new original-frontend integration tests cover the shared exit, original
kernel functions, nested/stack arguments, float raw-bit copies and rejection of
uninitialized paths. The native suite adds 74 checks: signed 64-bit results,
loops, tagged values, eight-argument GCC stack positions, skipped side effects,
float signed zeros/NaN payloads, and unchanged original basic-type?, type-type?
and find-parent-method execution with valid type/method inputs.

All **113 host tests, 8,088 runtime checks and 39 numeric checks pass** in
Flycast with stock 16 MiB RAM. Runtime emitted code is 27,482 bytes (including
the retained 3,244-byte shift baseline), arena use 302,448 / 1,048,576 bytes,
and 232 installed symbols. These separate diagnostics exclude the executable,
KOS, stacks and renderer. They do not establish whole-game fit or performance.

Original GOAL until loops execute the body before testing; these functions are
tested within their original valid-input contracts. No artificial zero-input
safety or method-table bounds were added. Float conditions preserve the pinned
original frontend's SSE unordered comparison contract, including unordered LT.
The upstream reader rejects `(return (none))` before export. Value-only early
returns without a normal result declaration remain explicitly rejected rather
than silently inventing a different function ABI. Process/self and vector or
mixed argument ABIs remain unsupported where documented.

Complete original gcommon still exports 66 typed functions. Per-function
lowering with standalone context succeeds for 54 and rejects 12, including
unprovided top-level function objects, literal pool limits in sort/mem-print,
mixed/vector ABIs and duplicate register metadata in valid?. This inventory is
not whole-module linking, startup or execution. Full original kernel boot,
process heaps/scheduling, gameplay, sound, saves and progression remain open.
Physical Dreamcast testing is available later; no CDs are needed at this stage.

Reproduce from the repository root after building the original frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

The host frontend integration tests skip when that executable is missing.
Build it before claiming their coverage. Compile only one SH-4 target at a
time, and do not relink the Windows frontend while compiler clients run.
