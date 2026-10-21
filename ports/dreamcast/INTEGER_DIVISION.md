# Native 32-bit integer division

2026-10-21. AI-assisted implementation. The full native game is unfinished.

Signed/unsigned quotient and remainder IR now emits an exact native SH-4
restoring division loop. Inputs use their low 32 bits; all results are sign
extended to the existing 64-bit container ABI, including unsigned results.
This follows the pinned x86 emitter's explicit instruction contract. No
floating-point approximation, interpreter or C transpiler is involved.

The 32-step loop uses SHLL/ROTCL, unsigned compare/subtract and a native count.
Signed paths normalize magnitudes, restore quotient/remainder signs and detect
the `INT32_MIN / -1` overflow case. Constant/aliased inputs, high input bits,
negative remainders and live values across operations/calls are tested.
The unchanged original `/`, `mod` and `rem` definitions compile and execute.

Division is currently a **runtime-profile operation**. The optional
`dc_goal_install_integer_bridge` installs a boxed native fault entry; modules
with this dependency fail linking atomically when it is missing. Zero divisors
and signed quotient overflow return zero and set `DC_GOAL_DIVIDE_BY_ZERO` or
`DC_GOAL_INTEGER_OVERFLOW`. The caller must inspect/reset that status. These
bootstrap error returns do **not** implement the x86 trap or complete original
exception/process behavior, and do not roll back surrounding side effects.
The numeric-only profile rejects division until a fault runtime is available.

An independent x86-64 oracle executes actual IDIV/DIV instructions on **838
valid operand/mode combinations** and checks the expected low-32/sign-extension
contract. GNU assembler verifies the new instruction encodings. Native cases
cover valid results, original wrappers, linked callers, missing-bridge rollback,
invalid operands/status and recovery after the caller clears the error.

The host suite passes **93 tests**. Flycast with stock 16 MiB passes
**7,932 runtime checks**, including the previous shift matrices and kernel
metadata assertions. This is not full-game coverage. Total emitted GOAL/boxed
bridge code is 22,198 bytes, including the separate 3,244-byte shift baseline.
Diagnostic arena use is 295,424 / 1,048,576 bytes with 200 installed symbols;
executable, KOS, ordinary stacks/allocator and the renderer are excluded.

Reproduce with `build_host_windows.ps1` and `build_runtime_goal.py --verify`.
Primary contracts: `goalc/compiler/IR.cpp` integer division emitters and
`goal_src/jak1/kernel/gcommon.gc`. See `INTEGER_SHIFTS.md` and `RUNTIME.md`.
