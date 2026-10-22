# Contributor task board

2026-10-21. (AI-assisted). These tasks are open invitations, not assigned work.
Start a discussion on this fork or submit a focused human-reviewed change.
Most compiler/runtime work uses synthetic fixtures and needs no original assets.

| Task | Useful experience | Concrete acceptance target |
| --- | --- | --- |
| Remaining full-kernel target configuration | GOAL, platform runtime | Scalar validator now uses explicit stock-RAM/table limits; audit remaining profiler/PC branches, scratchpad and MMIO before original startup |
| Remaining variadic and vector ABI | SH-4 GCC, GOAL | Fixed scalar mixed calls through eight arguments now pass, including integer stack spills; independently verify mixed variadic and vector boundaries |
| Remaining process lifecycle and kernel startup | GOAL, runtime systems | Original context/globals/tree/constructors and guarded zero-argument self entry execute; extend ABI/context support, failure unwinding, GC and thread startup |
| PowerVR scene pipeline | KOS, PVR, graphics | Measured culling/material deduplication and bounded residency on synthetic fixtures before expanding game scenes |
| Hardware validation | Dreamcast development | Reproduce asset-free native diagnostics on hardware with toolchain, load method, RAM use and failures documented |

## Completed: named-local argument export

The local-vars regression is fixed and protected by eight integration tests and
32 native checks. Arguments come from the original constrained entry values;
named locals and fixed process/self entries remain separate. Complete gcommon
exports 66 functions, without claiming full lowering or execution. See
[LOCAL_VARIABLES.md](LOCAL_VARIABLES.md). Expert review is still welcome.

## Completed: shared early returns

The exporter now retains the original outer return destination, assigns its
normal result and returns at its final shared label. Seven frontend tests and
74 native checks cover initialized paths, nested exits, raw float results,
side effects, stack arguments and original type/method functions. Existing
initialization guards remain strict. See [SHARED_EXITS.md](SHARED_EXITS.md).
Upstream rejects `(return (none))` before native export; no void ABI is invented.

## Ready-to-run regression examples

Run `python ports/dreamcast/repros/reproduce.py` after building the frontend.
Both named-local and early-return examples now lower. The latter returns 77
for input 1 and 99 otherwise; expanded native edge cases run via
build_runtime_goal.py. Neither reproduction is a gameplay test.

## Other measured gaps

The initial inventory (before the named-local fix) found that of 47 isolated
original gcommon defuns, 31 lowered, six failed lowering and ten failed
frontend export. Some failures are missing dependencies caused by isolation.
These are compile-only results, not link/execution or full-file coverage.
Original sort now executes on SH-4. Original valid? now executes with an explicit Dreamcast scalar memory context;
its original body is unchanged. Full-kernel target configuration remains open;
see SCALAR_VALIDATION.md. Qmem-copy variants need explicit
128-bit/vector work. Full gcommon now exports 66 typed functions; its lowering/execution remains
open. The latest full-export standalone lowering inventory is 57 of 66; this
does not link or execute the whole module. Explicit ordered context now exports original gkernel/gstate with 76/7 typed
functions; 54/3 now lower individually under unchanged desktop configuration
after declared scalar enum ABI classification (see ENUM_ABI.md).
These modules are not linked/executed on Dreamcast; see SOURCE_CONTEXT.md.

Full-file target setup must also address upstream PC_PORT/BIG_MEMORY settings,
MMIO, profiler hooks and memory limits. Do not execute desktop-configured
startup under the assumption that passing scalar probes validates it.

The first integrated gameplay target is a bounded Geyser Rock slice with movement,
jumping, collision and camera. Complete gameplay, streaming, audio, saves,
progression, disc packaging and hardware support remain much larger milestones.

## Completed: SH-4 literal islands and original list sort

Bounded literal pools now share layout with branch relaxation while protecting delay slots. Original sort runs with numeric/boolean callbacks, duplicates, empty/singleton and dotted lists; 105 new native checks pass. Original sort and mem-print now lower; standalone full-gcommon coverage is 56 of 66. Mem-print is compile-only. Next: remaining metadata, stable identifiers and explicit original kernel startup dependencies.
See [LITERAL_POOLS.md](LITERAL_POOLS.md). (AI-assisted).

## Completed: Distinct original GOAL operator bindings

Original 1/, +, -, *, /, mod and rem now coexist in one native module without C-label collisions. Three host tests and 59 new native checks cover naming, signed integer operations and original reciprocal saturation behavior. Runtime bindings preserve exact GOAL symbol names. Next: explicit fixed-register metadata and original kernel dependency setup; whole-game playability remains unfinished.
See [C_IDENTIFIERS.md](C_IDENTIFIERS.md). (AI-assisted).

## Completed: Explicit native symbol-table register role

Original identical rlet aliases now export once with fixed-register roles and reset metadata. Supported symbol-table resets map to native s7; missing initialization, unknown roles and mutation still reject. Six host tests and 15 new native checks pass. Original valid? advances to a long-branch limit and still needs target memory constants; it is not executed. Full kernel/gameplay remains unfinished.
See [FIXED_REGISTERS.md](FIXED_REGISTERS.md). (AI-assisted).

## Completed: Native SH-4 far branches

Relative far jumps now preserve both return halves, native stack, PR and T flags while sharing layout with literal pools. Five GNU assembler checks and 40 new native cases pass. Original valid? now lowers to 8,248 bytes, but is compile-only pending Dreamcast target constants. Standalone full-gcommon lowering reaches 57 of 66; full kernel/gameplay remains unfinished.
See [FAR_BRANCHES.md](FAR_BRANCHES.md). (AI-assisted).

## Completed: Ordered original kernel source context

Explicit ordered source preloads now supply real macros, constants and types without loading native modules or running startup. Original gcommon/gkernel/gstate export 66/76/7 functions; 57/50/3 lower individually under unchanged desktop configuration. Five host tests and 29 new native checks verify separate module loading, explicit initialization and cross-module bindings. Dreamcast target configuration and full kernel/gameplay remain unfinished.
See [SOURCE_CONTEXT.md](SOURCE_CONTEXT.md). (AI-assisted).

## Completed: Mixed scalar register arguments and calls

Fixed mixed integer/float signatures now use independent GCC register streams, with at most two integer/tagged arguments and eight total arguments. An 88-probe GCC oracle, three frontend tests and 63 new native cases pass. Compact integer/float leaves remain six/four bytes. Mixed stack/variadic and vector ABIs remain blocked. Original kernel compilation coverage is unchanged; full kernel/gameplay and target configuration remain unfinished.
See [MIXED_ABI.md](MIXED_ABI.md). (AI-assisted).

## Completed: Explicit Dreamcast scalar memory context and original validator

An explicit, hash-pinned scalar context gives the unchanged original valid? Dreamcast P0 RAM and native symbol-table bounds. Four host tests and 77 new native checks cover selection/provenance, boundary/tag/type checks and both previously excluded hash-bucket ranges. Full-file PC/profiler/MMIO configuration, original kernel startup, process heaps and gameplay remain unfinished.
See [SCALAR_VALIDATION.md](SCALAR_VALIDATION.md). (AI-assisted).

## Completed: Original kernel system-global startup prefix

A provenance-pinned contiguous prefix of original gkernel.gc now initializes its first nine system globals natively, including boxed versions, boot mode and counters. Four host tests and 32 new native checks verify ordering, explicit startup, values and exclusion of later stack/scratchpad setup. The incremental runner dependency is fixed. This is bounded global initialization; full target configuration, process heaps, kernel dispatch and gameplay remain unfinished.
See [KERNEL_GLOBAL_STARTUP.md](KERNEL_GLOBAL_STARTUP.md). (AI-assisted).

## Completed: Declared scalar enum ABI

Original EnumType width and signedness now classify unboxed 8/16/32/64-bit values at scalar ABI boundaries, preserving semantic enum names and original typed loads. Four host tests and 168 new native checks cover signed extremes, high halves, compact leaves, construction, calls and mixed register entry. Four more original gkernel routines lower (54/76), but remain compile-only. Full target setup, process heaps, kernel/gameplay and remaining mixed/vector ABIs are unfinished.
See [ENUM_ABI.md](ENUM_ABI.md). (AI-assisted).

## Completed: Fixed mixed scalar calls with integer stack arguments

Fixed mixed signatures now permit integer stack spills through eight total arguments. An independent GCC oracle verifies all 376 spilled mixed layouts (2,767 return probes); 226 frontend functions and 1,937 new native checks verify entries, calls, payloads, live values and stack restoration. Mixed variadic/vector ABIs remain blocked. The original scalar validator and bounded nine-global startup still pass; full kernel/gameplay and process heaps remain unfinished.
See [MIXED_STACK_ABI.md](MIXED_STACK_ABI.md). (AI-assisted).

## Completed: Six-byte SH-4 stack argument returns

Copy-only integer/tagged stack returns now use two loads and an RTS delay slot: six bytes with no frame. GNU checks cover all six stack positions; existing native boundary cases still pass. The same mixed probe module shrinks by 7,600 bytes (15.3%); whole diagnostic code saves 7,948 bytes and arena saves 7,952 bytes. These are size measurements, not timing benchmarks. Full kernel/gameplay, process heaps and remaining platform/ABI dependencies remain unfinished.
See [STACK_COPY_LEAVES.md](STACK_COPY_LEAVES.md). (AI-assisted).

## Completed: Original process-tree traversal and reparenting

Four unchanged original kernel routines now traverse, filter, search and reparent bounded native process-tree fixtures. Two host tests pin original bodies/imports and field layouts; 604 new native checks cover 128 mask/callback combinations, subtree pruning, mutation-safe sibling traversal, searches, reparenting and stable handle fields. These fixtures have no runnable threads; allocation, heaps, scheduler startup and complete gameplay remain unfinished.
See [PROCESS_TREE_NATIVE.md](PROCESS_TREE_NATIVE.md). (AI-assisted).

## Completed: Original process constructors and bounded sized allocation

Unchanged original process-tree/process constructors now run through compiled GOAL method dispatch and an explicit global-only sized object.new bridge. Two host tests pin original calls/layouts; 303 new native checks cover allocation guards, eight reservation sizes, original state/heap fields, stable handles and integration with original reparent/search. These objects remain dead without threads; complete process heaps, failure unwinding, scheduler and gameplay remain unfinished.
See [PROCESS_CONSTRUCTORS.md](PROCESS_CONSTRUCTORS.md). (AI-assisted).

## Completed: Bounded original process-heap allocation

Original process allocation now rounds/clears spans and advances original heap-cur with its strict end comparison, using explicit known process reservations. Global and process bridge paths remain distinct; missing/invalid targets reject without global fallback. 102 shared host/SH-4 C guards and 79 native GOAL integration checks pass, including original constructors/validator. Implicit pp/self, threads, GC and failure unwinding remain unfinished.
See [PROCESS_HEAPS.md](PROCESS_HEAPS.md). (AI-assisted).

## Completed: Structured static data and original kernel context

Typed structured static images now preserve original bytes and type/symbol/data/function relocations with strict closed-graph validation. An explicit inclusive global range executes the single unchanged original kernel-context definition, preserving flags/PID/false references while excluding later stack/scratchpad setup. Six host tests and 56 native checks pass. Full kernel startup, implicit pp/self, threads, GC, failure unwinding and gameplay remain unfinished.
See [STRUCTURED_STATIC.md](STRUCTURED_STATIC.md). (AI-assisted).

## Completed: Guarded original process self entry

The unchanged original remove-exit behavior now executes through an explicit validated process execution context, distinct from allocation-only binding. Missing or stale context exits through the restored native frame before body memory access. The original kernel current-process field is bound and released deliberately; unsupported/resetless/mutable roles still reject. Seven new host tests and 91 native checks pass. Full kernel startup, thread switching, GC, failure unwinding and gameplay remain unfinished. (AI-assisted)
See [PROCESS_ENTRY.md](PROCESS_ENTRY.md). (AI-assisted).

## Completed: Original process queries and name comparisons

Original process-by-name, process-not-name and process-count now execute with their three original anonymous callbacks, original name= and string=, original traversal, and explicit original null-context/search-global startup slices. Three host tests and 508 native checks verify callback closure, mixed symbol/string comparisons, first-match ordering, counts and unchanged tree/context memory. Full kernel startup, threads, GC, failure unwinding and gameplay remain unfinished. (AI-assisted)
See [PROCESS_QUERIES.md](PROCESS_QUERIES.md). (AI-assisted).

## Completed: Guarded missing native function calls

Native runtime call sites now reject zero/#f targets for live function cells, dynamic arguments and virtual methods. A strict core fault bridge reports BAD_FUNCTION and exits the current caller through its restored frame, defining integer/float error returns and skipping continuation effects. Three host tests and 78 native checks pass, including missing-import rollback and stack/float/void recovery. General error propagation, failure unwinding, threads, GC and gameplay remain unfinished. (AI-assisted)
See [CALL_GUARDS.md](CALL_GUARDS.md). (AI-assisted).

## Completed: Bounded original main CPU-thread construction

An explicit C bootstrap entry validates an owned process reservation and disjoint execution-stack span before calling the unchanged original main CPU-thread constructor. Two host tests and 142 native checks verify original type flags/field offsets, strict heap bounds, no-write failures and three data-only constructions. It does not initialize a runnable PC, activate processes or switch threads. Full platform startup, GC, failure unwinding and gameplay remain unfinished. (AI-assisted)
See [CPU_THREAD_CONSTRUCTION.md](CPU_THREAD_CONSTRUCTION.md). (AI-assisted).

## Completed: Bounded exact CPU-thread stack images

An explicit C helper validates the original allocated CPU-thread and copies an exact bounded execution-stack suffix to and from its backup payload. Independent byte-pattern, guard, metadata and state tests cover every length from zero through 256 bytes, with additional integration checks on threads built by the unchanged original constructor. Grok supplied an isolated proposal; Codex corrected two defects and independently tested the integrated implementation. This milestone does not save registers or switch threads. Full kernel startup, GC, failure unwinding and gameplay remain unfinished. (AI-assisted)
See [THREAD_STACK_IMAGES.md](THREAD_STACK_IMAGES.md). (AI-assisted).

## Completed: Measured SH-4 compiler and context ABI audit

A reproducible compile-only audit checks 24 register-preservation observations and installed SDK context offsets under the current GCC/KOS flags. It establishes the four-byte default ABI stack boundary and documents why a full KOS interrupt context cannot be placed in the original CPU-thread register area. Runtime source/binaries are unchanged; existing host/Flycast totals are preserved verification, not new execution tests. Actual saved-state roundtrips, thread switching and gameplay remain unfinished. (AI-assisted)
See [CONTEXT_ABI_AUDIT.md](CONTEXT_ABI_AUDIT.md). (AI-assisted).

## Completed: Native same-stack register snapshot

An 80-byte native SH-4 leaf snapshots selected GPR/FPU/control registers into a separate 60-byte caller buffer, rejecting null or misaligned pointers and unsupported FPSCR modes before stores. Grok supplied and reviewed the isolated proposal; independent Codex assembly/C tests add 3,034 native checks covering 108 bit-pattern, status, guard and rejection scenarios. Two host target-tool tests verify C99/C++11 layout and real C linkage. This is observation only; register restoration, process activation, stack switching and gameplay remain unfinished. (AI-assisted)
See [REGISTER_SNAPSHOT.md](REGISTER_SNAPSHOT.md). (AI-assisted).
