# Bounded original main CPU-thread data (AI-assisted)

The unchanged original `(method new cpu-thread)` and `(method asize-of
cpu-thread)` now execute on SH-4 through an explicit bounded C bootstrap entry,
`dc_goal_construct_main_thread`. This creates the first thread's data structure.
It does not initialize a runnable PC, run the thread or switch contexts.

The original constructor accepts six scalar arguments and calls no external
functions. It normally bumps the process heap directly, without checking fit.
The C entry first validates the caller-owned unmoved base-process reservation,
the base 128-byte/12-method CPU-thread schema, a symbol name and an aligned
backup size. Main-thread and top-thread fields must both be false. It validates
the entire aligned caller-owned execution-stack span and rejects overlap with
the process reservation, arithmetic overflow, insufficient backup capacity and
strict process heap exact-fit/overflow before entering the original body.
The original constructor must already be linked as a valid native function.

This entry deliberately rejects temporary threads. The original temporary path
still contains desktop PROCESS_STACK_SIZE and scratchpad assumptions. There is
no new execution-stack configuration or temporary-thread implementation here.
The helper does not change allocation bindings or install execution identity.
Known spans are supplied by the caller; there is no automatic ownership or
lifetime registry. All objects/spans must remain quiescent and unmoved.

## Exact original layout

The original frontend verifies the following offsets. Basic object references
point four bytes after the raw allocation/type tag. Offset assertions in the
original type declaration use raw offsets, which must not be mistaken for
offsets from the object reference.

| Field | Offset from object reference | Raw allocation offset | Bytes |
|---|---:|---:|---:|
| Type tag | -4 | 0 | 4 |
| name/process/previous/hooks/pc/sp/top/stack-size | 0..32, step4 | 4..36 | 36 |
| rreg[0..6] | 36 | 40 | 56 |
| freg[0..7] | 92 | 96 | 32 |
| Dynamic backup stack | 124 | 128 | backup size |

There is no extra four-byte padding at object offset36. The original type flags
are `(12 << 32) | 128`, and asize-of returns 128 plus the backup size.
The runtime fixture manually supplies that exact schema, inheriting the
already-installed original thread type. It does not execute full gkernel-h
startup or install the original constructor as an unguarded GOAL new method.

The original body writes its type/name/process/previous links, stack pointers,
backup capacity and suspend/resume hook values from method slots10/11. It updates
the process heap cursor and top-thread only. The process main-thread remains
false, status remains dead, the execution stack is untouched, and fresh process
register/PC/backup bytes remain zero. Hooks are data, not implemented switching.
Reused storage still needs deliberate PC/register initialization before running.

## Verification

Two additional host tests pin unchanged source/context, the six-argument ABI,
absence of external calls/implicit pp, exact field widths/offsets and original
type flags. 142 additional native checks cover 13 malformed/unsupported span,
name, size and capacity combinations, unchanged process/stack bytes on failure,
existing main/top-thread rejection, wrong schema, absent original constructor,
three independent original-created processes with backup sizes0/64/256, all
initialized fields, original asize-of, saved area/tail preservation and repeated
construction rejection. Constructor preflight failures allocate nothing.

All 186 host tests, 12,576 runtime checks and 39 numeric checks pass in stock
16 MiB Flycast. Diagnostic GOAL code is 134,526 bytes, arena 438,592 / 1,048,576
bytes, 650 symbols, excluding executable/KOS/stacks/renderer. Physical Dreamcast
and whole-game memory/performance remain unvalidated. Thread switching, GC,
general failure unwinding, complete platform startup and gameplay are unfinished.
