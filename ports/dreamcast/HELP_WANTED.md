# Contributor task board

2026-10-21. (AI-assisted). These tasks are open invitations, not assigned work.
Start a discussion on this fork or submit a focused human-reviewed change.
Most compiler/runtime work uses synthetic fixtures and needs no original assets.

| Task | Useful experience | Concrete acceptance target |
| --- | --- | --- |
| Remaining full-kernel target configuration | GOAL, platform runtime | Scalar validator now uses explicit stock-RAM/table limits; audit remaining profiler/PC branches, scratchpad and MMIO before original startup |
| Vector and remaining mixed ABI | SH-4 GCC, GOAL | Register-only fixed mixed signatures now pass; independently verify mixed stack, variadic and vector boundaries before enabling them |
| Remaining kernel sequencing and heaps | GOAL, runtime systems | Nine original system globals now initialize explicitly; implement measured native process heaps, context/thread setup and remaining original module dependencies |
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
functions; 50/3 lower individually under unchanged desktop configuration.
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
