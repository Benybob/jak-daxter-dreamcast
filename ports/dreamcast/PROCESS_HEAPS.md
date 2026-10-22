# Bounded original process-heap allocation

2026-10-21. (AI-assisted). Native basic.new and object.new now accept the
original quote-process selector when an explicit process reservation is bound.
No process target means an error; there is no fallback to global allocation.
The previous explicit global allocation path remains available.

goal_process_heap.c implements the original kscheme.cpp process path: round
positive sizes to sixteen bytes, clear the aligned span and update the original
heap-cur field only when allocation-end is **strictly less** than heap-top.
Exact fits fail, matching the original. Basic allocations preserve the original
four-byte type-header offset and enforce their declared minimum size.

The C caller supplies the known full reservation (112-byte base process plus
its dynamic bytes). The view validates the installed original process schema,
exact owner type, arena span, allocated-length, dynamic base at process+108,
aligned cursor and bounded top on every allocation. The caller must actually
own the supplied reservation; these checks do not discover allocation ownership
or track lifetimes. This bounded implementation supports unmoved base processes,
not actor-subtype layouts, GC relocation, arbitrary native pointers or threads.

dc_goal_bind_process_heap binds this allocation target for the active native
runtime. A failed same-runtime bind clears the old target. Explicit clearing
and core-bridge cleanup also remove the target. These are allocation-context
operations; they install no implicit pp/self register ABI, process activation
or scheduler/thread context switching.

One new host test executes 102 shared C guard checks; the same checks run as
actual SH-4 C code in Flycast. A new frontend test verifies real typed process
selector calls and the 64-bit record field layout. Seventy-nine additional
native integration checks use original-constructed process owners and compiled
GOAL allocation:

- strict exact-fit/rounding boundaries, zeroing and unaligned exclusive ends;
- changed metadata/schema, invalid sizes and nonmutating failure paths;
- three 64-bit records in one process, accepted by the unchanged original valid?;
- the unchanged original tree constructor allocating inside a different process;
- independent cursors/data while rebinding targets, preserved tail bytes and
  failure/clearing paths rejecting allocation without consuming global arena;
- explicit global allocation still working afterward and owners remaining dead.

All **165 host tests, 11,701 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. Diagnostic GOAL code totals 119,414 bytes; arena use is
415,520 / 1,048,576 bytes with 591 symbols, excluding KOS/executable/stacks/
renderer. Diagnostic ABI/far-branch/shift probes are included. The shared guard
suite temporarily creates a separate 512 KiB arena. Whole-game resource fit is
unproven.

Reproduce after building the original typed frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

The constructors and record initialization assume a fitting allocation, so
error cases call allocation bridges directly. Exception/failure unwinding,
full shared actor heaps, lifecycle/GC support and remaining platform/ABI work
remain unfinished. Physical Dreamcast is available later; no CDs are prepared/
burned, no physical hardware result is claimed, and full gameplay is unfinished.
