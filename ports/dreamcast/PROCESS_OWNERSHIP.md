# Allocator ownership and explicit process selection

2026-10-26. (AI-assisted). Native Jak Dreamcast runtime dependency; full game
startup and gameplay remain unfinished.

`process_spans.c` records the declared physical extent of pinned process
allocations independently of their mutable GOAL fields. The native object
allocation bridges capture process-family allocations made on the global heap
after a caller configures external storage with
`dc_goal_register_process_spans`. Capacity is supplied by the actual C owner;
there is no eviction or allocation-size inference from process fields.

The original symbol-path `process.new` requests `112 + stack-size` bytes from
the object allocator. This is recorded before its fields are initialized.
The raw-address constructor calls no allocator, so its C buffer owner must
explicitly call `dc_goal_note_process_span` using the actual pinned buffer
extent. Being inside the arena does not itself prove ownership: this API is
a trusted C assertion, not a GOAL import. Moving-pool subspan capture remains
unfinished. Nested process allocations on a process heap refuse before bumping
that heap until the parent/subspan lifetime protocol exists.

Registering an allocation never binds execution or publishes `pp`.
`dc_goal_enter_known_scope(rt, process)` performs a lookup for an explicit
selection request, then uses the existing authenticated lexical-scope entry.
It does not trust `allocated-length`, type size, PID or heap cursors for the
extent. `dc_goal_leave_known_scope(rt, previous_process)` is a distinct restore
operation that must match the saved process. No target-value heuristic decides
whether an operation is an entry or a restore.

When configured, both legacy explicit-byte binding APIs and lexical entry must
match the known extent. With no ledger, their existing trusted-C explicit-byte
contract remains available. Allocation ownership does not bypass the separate
heap/type/PID checks. Derived-process physical extents can be recorded, but
derived heap activation and execution are still unsupported by those checks.

Each sorted row has an exact process address, declared bytes and nonzero
64-bit physical ownership generation. Lookup is binary search. New notes can
arrive while another process runs; existing rows are never silently replaced.
An exact duplicate preserves its generation. A full table refuses a fresh
global process allocation before committing arena bytes; unrelated allocations
can continue. Persistent metadata uses caller storage outside the arena and
runtime object, with no per-entry malloc.

The measured SH4 layout is 32 bytes for the descriptor and 16 bytes per row:
eight rows require 160 bytes of caller storage, plus fixed module state.
The capacity is a configurable ownership budget, not a measured whole-game
process count. Lookup output can be temporary C storage on the current native
fiber's working stack; guest heap storage and inactive-stack outputs refuse.

Physical lifetime differs from process instance/PID lifetime. Original fixed
pool construction automatically records each member. Original `return-process`
only reparents it and preserves the same ownership generation. Retagging also
preserves physical extent. `dc_goal_process_span_retire` forgets authority only;
it neither frees bytes nor implements pool return. It requires the expected
generation and global runtime quiescence. Reclaiming the same address creates
a fresh generation, including across detach/reinitialization. Generation
exhaustion fails closed. Finer moving/GC ownership and per-object borrowing are
still unfinished.

Quiescence here requires no execution binding, allocation binding, live lexical
scope or current native fiber. Core teardown refuses a current fiber even
after it leaves its scope, and detaches the ledger only after safe cleanup.
Native operations require the registered KOS owner. Separate registration
anchors prevent a foreign runtime query or corrupt descriptor runtime field
from dropping the true owner's table.

Verification adds four portable host test groups with 101 checks and 89 native
checks. The native oracle executes the unchanged original symbol and raw
constructors, compares all 272 raw-buffer bytes including fences and untouched
fields, captures both members of an original fixed pool, and verifies ownership
survives original pool return. It also executes known-span lookup/selection on
a real native fiber, checking integer/FP state, stack canaries, scheduler
separation, retained ownership, capacity failure before allocation, generation
reuse and teardown refusal. The raw oracle was corrected to expect the `dead`
symbol pointer rather than that symbol's value; original code was unchanged.

The host suite passes **398 tests**, including the four new ownership groups.
The full native runtime suite passes **23,145 checks in stock 16 MiB Flycast**.
The diagnostic uses 698,592 bytes of its 1 MiB arena, including diagnostic
working stacks. KOS, executable/root C stacks and the renderer are separate;
this is not evidence that the combined game fits. No physical hardware test
or disc image is supplied. Full GOAL mutable-pp lowering, abandon/throw,
scheduler startup, moving pools, GC and full gameplay remain unfinished.
