# Original protect-frame construction with explicit arguments

2026-10-26. (AI-assisted). This advances original cleanup-frame construction;
complete process deactivation, exception unwinding and gameplay remain unfinished.

All **390 host tests**, **22,849 native runtime checks** and **39 freshly rerun
numeric checks** pass. This adds 79 native checks to the original pool checkpoint.
The original constructor is 328 bytes with a 56-byte optimized frame. Total
diagnostic GOAL code is 144,770 bytes, arena 661,024 / 1,048,576 bytes and 704
symbols, excluding KOS, executable/static storage, native stacks and rendering.

The unchanged original `(method new protect-frame)` takes allocation, type and
exit-function arguments while reading a typed `process` value from desktop r13.
It has no implicit self parameter, register reset or write to pp. Its body writes
the supplied frame's original type/name/exit/next fields and the process's
stack-frame-top. The caller supplies valid owned storage for the 16-byte physical
frame; the constructor does not allocate that storage or execute its exit callback.

The frontend now explicitly classifies this narrow read-only role as
`process_context_read`. A read, exact process type and absence of all fixed-register
writes are required. Unknown, integer r13, r15, mutable pp and assembly remain
unsupported. The previous parameterless implicit-self/reset ABI stays distinct.

The native backend stores incoming GCC argument words before calling the existing
authenticated process getter. It defines the read-only value at entry for definite
assignment and slot liveness, before any original body memory access or store.
Branch/goto targets retain their original operation identities after that entry
definition is inserted, including loop back edges. Invalid original targets
are rejected before translation. Raw forged getter operations remain rejected.

Missing, allocation-only or stale execution identity returns zero through the
normal PR/stack/callee-save epilogue, with an explicit error and no original frame
or process body writes. Authentication itself validates the owned process type
and heap fields; this is not a claim that the guard reads no process memory.

The reservation remains explicit, unmoved, same-runtime base-process storage.
Allocation binding does not infer execution identity. This gate uses the current
scheduler-linked bounded context. Foreign nested lexical pp, allocation under
another process's pp, nonlocal failures and KOS thread migration need separate
verified semantics; this ABI does not implement them.

Native verification constructs three frames for each of two original processes,
checks complete frame bytes with two 16-byte fences and complete process bytes,
then pops the chain using unchanged original `remove-exit`. A GCC caller keeps
r8-r13 and stack canaries live across the getter and all three arguments, including
the stacked third word. Tests compare full 64-bit returned addresses and assert
no constructor allocation or scheduler identity change. Refusal cases compare
the entire supplied frame and process reservation. A separate typed pp loop
checks zero-trip and repeated back-edge execution with explicit arguments.

Host regressions pin unchanged source hashes, exact role/container metadata,
both slot allocators, CFG target rejection, numeric-profile refusal, ambiguous
roles, parameter overlap, resets, mutations and forged getters. The original
`deactivate` method remains refused because its pp is mutable and its nonlocal
control transfers are unsupported. No source branch was removed to compile it.

Reproduce with the configured frontend and SDK:

```sh
python ports/dreamcast/build_goal_frontend_windows.py
python ports/dreamcast/build_runtime_goal.py --msys /path/to/msys --verify --output /private/protect-check
```

Do not rebuild/relink the frontend while compiler clients execute it. Run one
SH-4 build at a time. Verification uses stock 16 MiB Flycast; physical Dreamcast
performance and hardware behavior have not been established.
