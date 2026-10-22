# Bounded CPU-thread stack images (AI-assisted)

A portable C helper now backs up and restores an exact execution-stack suffix
inside the original CPU-thread backup payload. This is a data operation, with
no register save, PC/SP metadata update, status transition or context switch.
Grok supplied the initial implementation in an isolated proposal folder;
Codex reviewed it, corrected the CPU-type lookup and empty-span validation,
and supplied independent host/native tests before integration.

## Contract

The caller owns an unmoved, quiescent process reservation, its allocated CPU
thread and a disjoint execution-stack span. The view is separate stable C
metadata storage, without aliases to the runtime or managed memory being
validated/copied. Restore must run from outside the destination execution stack.
These are caller preconditions; there is no ownership/lifetime registry or
native current-SP detection in this data-only component.

Opening validates the original process heap, exact CPU-thread type/schema,
owner/top-thread links, actual allocated thread extent, stack top and backup
capacity. The original CPU type has raw size128 and12methods, inheriting thread;
the backup begins at object-reference offset124 (raw128). The fixed parent
thread type has size40 and is distinct from the CPU-thread type.

Backup accepts `sp` within the inclusive owned stack bounds, computes the exact
byte length `top-sp`, and requires it to fit the pinned backup capacity. It
copies only those bytes to the backup payload. Every byte length is supported,
including zero and odd/unaligned positions. This makes no claim about the ABI
alignment required by a future executable thread. It does not round up a copy
to the original platform's eight-byte loop size.

The C view records the saved range and one pending image. Restore verifies that
range again, copies exactly back to the execution suffix, then consumes the
image. Duplicate backup/restore, changed owner/top/capacity, stale allocations,
wrong types, overflow, overlap and inconsistent image state reject. All relevant
metadata and extents are revalidated on each operation before any buffer write.
Failure invalidates the view and leaves the managed process/thread/stack bytes
unchanged. Repairing fields alone does not resurrect an invalidated view.
Explicit reopening is required; clear releases only the C view.

The helper never modifies original thread PC/SP/hooks/saved register/FP fields,
process heap cursor, execution identity or status. It never reads a PC to jump.
Nonzero nonfalse code pointer validation, saved CPU state, actual SP switching,
failure unwinding, nested scopes, temporary threads and GC remain future work.

## Independent verification

4,216 shared C checks pass on both host and SH-4. They test every exact image
depth0..256 against independently indexed byte patterns; prefix/tail/canary
preservation; the complete original header/register/FP block; original process
fields and cursor; out-of-range or over-capacity SP; empty/mismatched/overflowing
and overlapping spans; wrong tags/schema; stale ownership/capacity/allocation;
pending-image state guards, invalidation and explicit reopening. No image
operation allocates memory. The shared fixture temporarily allocates512KiB for
its separate layout runtime; it is freed and is outside the main arena metric.

21 additional native integration checks use CPU threads made by the unchanged
original constructor, including zero-capacity backup and an odd17-byte image.
The data roundtrip leaves the process dead and all original CPU metadata
unchanged. No runnable context or game execution is inferred from these copies.

All 187 host tests (including the shared C checks), 16,813 native runtime checks
and39numeric checks pass in stock16MiBFlycast. Diagnostic GOAL code remains
134,526B, main arena438,592/1,048,576B,650symbols. Native helper/test code,
executable/KOS/stacks/renderer and temporary shared-fixture memory are excluded
from that main arena number. Whole-game fit/performance and physical Dreamcast
remain unvalidated. Full kernel/gameplay is unfinished.
