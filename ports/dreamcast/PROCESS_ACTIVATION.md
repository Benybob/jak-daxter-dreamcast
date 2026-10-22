# Guarded original base-process activation

2026-10-22. (AI-assisted). The unchanged original `process.activate` now executes
on native SH-4 with its original `mem-set32!`, CPU-thread constructor and
`change-parent` dependencies. Grok audited the original order and reviewed the
candidate; Codex implemented the preflight and independent acceptance oracle.
This creates a ready process and attaches it to a tree. It does not run its code.

`dc_goal_activate_process` accepts an explicit process reservation, empty
destination tree, owned execution-stack span and symbol/string name. Read-only
preflight requires a fresh dead base process (112-byte schema, zero type heap
offset, unused heap cursor, no prior main/top thread or tree attachment), exact
self/ppointer links, an empty base process-tree root with its tree mask, and an
initialized kernel context with neither a current nor relocating process.
The next signed PID must have headroom. Active native fibers are rejected.

Preflight also requires the original 128-byte CPU-thread schema, its explicit
constructor method binding and current loader-registered original entries for
activation, construction, word fill and parent change. Stack base/top must be
16-byte aligned, at least 4096 bytes apart, within the runtime arena and disjoint
from the owner, destination, context, symbol table, relevant schemas and code.
The caller owns all supplied pinned spans and keeps code/data quiescent. These
checks do not establish arbitrary ownership, body stack budget or memory integrity.
Process, tree and context write spans must be mutually disjoint and exclude the
symbol table, relevant schemas and all four dependency code objects too.

Original activation rewinds the heap cursor and the CPU constructor bumps it
directly without checking the heap top or clearing memory. Preflight therefore
requires strictly more than 384 free bytes: a 128-byte header and the original
256-byte backup. Unsupported arguments, stale state, missing entries and exact
fit are rejected before the original status/PID/heap/tree writes. Target effects
are not rolled back if a later body fault occurs. The helper neither installs
nor infers an allocation or execution binding.

On success the original code inherits the destination mask with sleep, sleep-code,
tree and heap-shrunk bits cleared, sets status `ready`, assigns/increments the PID,
sets the name and resets hooks/state/connection fields. It constructs a main CPU
thread, installs main/top-thread and attaches through original ppointers. Pool,
allocated length, heap top, self, ppointer and connection next0/prev0 are preserved.
Kernel current-process remains false. PC and saved registers retain the fresh
process allocator's zeros; activation does not install a runnable continuation.

For this base type, `mem-set32!` receives a zero word count. The unchanged helper
is separately exercised with positive counts, low-32-bit values and surrounding
guards. Its argument order is destination, word count, value. It is trusted
compiled typed memory code, with the same caller-owned memory contract as other
native GOAL routines. No new unrestricted C memory-fill bridge is installed.

The original default backup is only 256 bytes. It is not a measured allowance
for our SH-4 compiler/C frames. Integrating runnable processes will require an
explicit target stack policy plus original set-to-run, argument, status, cleanup
and suspend/resume semantics. The private cooperative fiber engine is not
installed under those original method names. Derived processes, nested/attached
destination trees, temporary threads, scratchpad translation, scheduler dispatch,
relocation/GC and failure unwinding remain unsupported by this entry helper.

Verification: 194 host tests, 21,322 runtime checks and 39 numeric checks pass in
stock 16 MiB Flycast. This milestone adds 331 native checks, including active
fiber rejection, 12 bad-argument cases, 18 stale-state/schema cases, strict heap
fit and two independent original activations with consecutive PIDs. Whole owner,
destination, context and execution spans remain unchanged on preflight rejection.
Positive checks include each original CPU field, exact tree links, connection
reset/preservation, image-helper compatibility and no global arena allocation or
symbol interning during activation. Host tests independently verify original
source hashes, explicit argument contracts and original imports.

Original activation generates 2136 bytes, including 58 bytes of literals/alignment,
and an 88-byte native frame (688 bytes before stack-slot reuse). The original word
fill generates 318 bytes. Diagnostic GOAL/boxed code totals 138,034 bytes; main
arena use is 524,736 / 1,048,576 bytes with 675 symbols. These exclude executable,
KOS, stacks and renderer and do not establish a full-game memory budget.
Full gameplay and physical Dreamcast validation remain unfinished; no discs are
prepared or burned.
