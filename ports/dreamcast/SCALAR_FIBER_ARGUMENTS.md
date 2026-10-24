# Explicit scalar arguments into native fibers

2026-10-22. (AI-assisted). Native cooperative fibers now accept zero through
eight explicit `uint64` scalar words. The original zero-argument initializer
remains available. This supplies a calling primitive needed by original process
startup's six arguments; original set-to-run, scheduler/status transitions and
deactivation are still unfinished.

`dc_goal_fiber_init_words` copies the caller's input vector into owned C metadata
before clearing the old inactive context. The caller can then change or discard
the input array. A rebind can safely use that old context's owned argument array.
Unused slots are zeroed. Invalid counts or missing required input invalidate
inactive owned metadata. Active initialization rejects before reading input or
disturbing the running anchor; runtime/arena aliases reject before clearing.
Null-runtime failure does not read the argument vector.

The caller supplies an entry with the exact corresponding scalar prototype.
Neither a pointer nor an argument count proves its signature. Mixed floats,
structures, variadic calls and hidden parameters are unsupported. Existing
compiler restrictions on implicit self with explicit parameters remain in place.
The ordinary argument-taking fixtures call unchanged zero-argument `remove-exit`
through an explicit alias and deliberately bound process identity.

Grok supplied the isolated private C arity adapter. It checks count/null arguments,
calls one correctly typed nonvariadic prototype for each arity and writes output
after the target returns. It does not perform entry provenance or runtime
validation itself. The fiber engine performs those checks and keeps its argument
metadata pinned outside the runtime arena and working stack. The adapter's output
local lives on the fiber stack throughout a blocked target call, including any
yield. Each such C call returns once on its own restored stack.

Stored counts above eight fault before entry or stack restoration. Repairing a
faulted count cannot resurrect a continuation. These are structural checks under
the existing immutable caller-owned metadata contract, not arbitrary corruption
or signature protection. Root/continuation PC guards, exact stack images, default
incoming C FPU modes, explicit identity and all other fiber lifetime restrictions
remain as documented in [COOPERATIVE_FIBERS.md](COOPERATIVE_FIBERS.md).

Verification: 195 host tests, 21,849 native runtime checks and 39 numeric checks
pass in stock 16 MiB Flycast. This milestone adds 527 native checks: 438 shared
actual C ABI checks, 80 argument-fiber checks and nine active-rebind checks. The
host lifecycle checker now passes 93 checks; the separate C ABI checker passes
438, exercising every arity with varied full-width values and exactly-once calls.

Six- and eight-argument GOAL bodies share one deliberately destroyed 16 KiB stack
with separate original 8192-byte backups. They consume every argument's high and
low halves, nest a six-argument call inside the eight-argument body, yield and
resume with independent 64-bit values, and call original process-frame pop under
the matching explicit owner. Root GPR/FPR/FPUL/FPSCR/canaries, CPU headers,
process status, fences, no allocation and no interning remain checked. The fixture
uses `imul64` for full-width products; ordinary GOAL `*` retains its original
32-bit integer multiplication semantics.

The installed SH-4 adapter occupies 400 bytes. The original milestone's target C
context size was 292 bytes, including eight copied words; its two register images
remain 60 bytes each. The later [suspension budget measurement](SUSPENSION_BUDGETS.md)
adds one four-byte observation at the end, bringing the current context to 296 bytes.
Compiled GOAL fixtures occupy 1124 and 732 bytes with 72- and 88-byte frames.
Diagnostic GOAL/boxed code totals 139,902 bytes; main arena use is
568,080 / 1,048,576 bytes with 679 symbols. These exclude executable/KOS, C
metadata/stacks and renderer; they do not establish a full-game budget.
Full gameplay and physical Dreamcast validation remain unfinished. No discs are
prepared or burned.
