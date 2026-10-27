# Contributor task board

2026-10-21. (AI-assisted). These tasks are open invitations, not assigned work.
Start a discussion on this fork or submit a focused human-reviewed change.
Most compiler/runtime work uses synthetic fixtures and needs no original assets.

| Task | Useful experience | Concrete acceptance target |
| --- | --- | --- |
| Remaining full-kernel target configuration | GOAL, platform runtime | Scalar validator uses explicit stock-RAM/table limits; the kernel branch/scratchpad/MMIO inventory is in TARGET_CONFIG_AUDIT.md. Choose Dreamcast values for its 6 roots and provide SH-4 paths for its 8 inline-asm functions before original startup |
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

## Completed: Exact loader-installed native function entries

The module loader records exact function entries, byte lengths and owning symbols after cache synchronization. Explicit scalar/void initializers now reject fabricated function objects and stale entries. A bounded read-only query checks strict ordering and current ownership; 271 shared host/SH4 checks cover corruption, duplicates, replacement, no-write queries and partial-registration OOM rollback. Grok's review prompted the duplicate-entry regression. This is initial-entry provenance; continuation validation, restoration, stack switching and gameplay remain unfinished. (AI-assisted)
See [CODE_ENTRY_PROVENANCE.md](CODE_ENTRY_PROVENANCE.md). (AI-assisted).

## Completed: Registered calls on an owned alternate stack

A validated synchronous native primitive calls a registered zero-argument scalar entry on a separate owned stack and returns to the C caller. Grok supplied and reviewed the 84-byte raw assembly; independent tests add 488 native checks, including varying 64-bit results, target FPU modes, guards, rejection, nesting and unchanged original remove-exit with explicit process binding. The original behavior now runs on its constructed CPU thread's stack. This is call/return; suspension, continuation restoration, scheduling and gameplay remain unfinished. (AI-assisted)
See [STACK_CALL.md](STACK_CALL.md). (AI-assisted).

## Completed: Cooperative GOAL suspension with shared stack images

A guarded native SH-4 fiber engine now suspends and resumes registered GOAL entries while preserving callee registers and exact stack images. Two GOAL contexts share one deliberately destroyed working stack and call unchanged original remove-exit under explicit process binding. Grok supplied the isolated assembly and reviewed the engine; Codex fixed two word-push boundary gaps and independently verified 385 new native checks. Original scheduling, activation, GC, failure unwinding and gameplay remain unfinished. (AI-assisted)
See [COOPERATIVE_FIBERS.md](COOPERATIVE_FIBERS.md). (AI-assisted).

## Completed: Guarded unchanged original process activation

Unchanged original process.activate now assigns a PID, initializes its main CPU thread and attaches a fresh base process to an empty tree on native SH-4. Read-only preflight rejects unsupported/stale state before body writes; original mem-set32!, CPU construction and change-parent remain the executed dependencies. Grok audited the original contract and reviewed the wrapper; Codex independently verifies 331 new native checks. Processes become ready without running game code. Scheduler/argument/cleanup integration, GC, unwinding and gameplay remain unfinished. (AI-assisted)
See [PROCESS_ACTIVATION.md](PROCESS_ACTIVATION.md). (AI-assisted).

## Completed: Explicit scalar startup arguments across native yields

Native fibers now copy and deliver zero through eight explicit 64-bit scalar arguments while preserving the zero-argument API. Six/eight-argument GOAL routines survive nested yields, overwritten input arrays and a destroyed shared working stack, and call unchanged original remove-exit under explicit process binding. Grok supplied the isolated 400-byte SH-4 C adapter; Codex independently verifies 527 new native checks plus 93 lifecycle and 438 C ABI host checks. Original set-to-run/scheduling/cleanup, GC, unwinding and gameplay remain unfinished. (AI-assisted)
See [SCALAR_FIBER_ARGUMENTS.md](SCALAR_FIBER_ARGUMENTS.md). (AI-assisted).

## Completed: Original main-thread backup sizing and startup chain

The guarded unchanged original void stack-size-set! method now resizes a real activated main CPU thread's backup. Original activation, resizing, six scalar startup arguments, yield, poisoned-stack restoration and completion work together under an explicit C driver and process binding. Grok supplied the read-only checker; Codex corrected its false-symbol marker and independently verifies 256 new native checks, including stale handles, active mutation and canonical original-export identity. Original scheduler/arming/deactivation, GC, unwinding and gameplay remain unfinished. (AI-assisted)
See [THREAD_BACKUP_RESIZE.md](THREAD_BACKUP_RESIZE.md). (AI-assisted).

## Completed: Measured native suspension image budgets

Native fibers retain validated suspension image requirements, including backup OOM. Six-argument and nested eight-argument GOAL fixtures measure312B/432B on both8KiB and16KiB working stacks. Original activated CPUs with default256-byte backups fail without partial copy; fresh pure fixture calls with measured512-byte backups yield, survive poisoned stacks and complete. Grok independently reviewed design and final source. Original scheduler/arming/cleanup and gameplay remain unfinished. (AI-assisted)
See [SUSPENSION_BUDGETS.md](SUSPENSION_BUDGETS.md). (AI-assisted).

## Completed: Original main-thread arming and timer-safe KOS stacks

Unchanged original set-to-run arms a guarded main CPU thread; an explicit C adapter imports its target and six scalar words into native fibers. Both alternate-stack paths now preserve KOS stack ownership across timer interrupts, with forced IRQ and cross-OS-thread rejection oracles. Grok assisted isolated gate design and review; Codex independently verified execution. Original scheduler/bootstrap execution, cleanup, GC and gameplay remain unfinished. (AI-assisted)
See [ORIGINAL_THREAD_START.md](ORIGINAL_THREAD_START.md). (AI-assisted).

## Completed: SH4ZAM batched textured viewer projection

The textured viewer now uses the external upstream SH4ZAM library for FTRV batch projection with exact XMTRX preservation and integer-only FPU mode rejection. 2382 projection checks and the 7784-triangle viewer pass on stock16MiBFlycast. Grok review also led to fail-closed masked-yield and full-current-stack overlap regressions, with 22469 runtime checks passing. Physical hardware performance and full gameplay remain unverified. (AI-assisted)
See [SH4ZAM.md](SH4ZAM.md). (AI-assisted).

## Completed: Imported original main-thread completion receipts

Pinned C receipts bind imported original main threads to their process PID, reject stale/copy restoration and permit guarded C-only completion release. 199 host tests,22649 runtime and39 numeric checks pass on stock16MiBFlycast. Original deactivation and full gameplay remain unfinished. Human-approved next priority is native walking/running/jumping with real Jak on a test map. (AI-assisted)
See [MAIN_THREAD_COMPLETION.md](MAIN_THREAD_COMPLETION.md). (AI-assisted).

## Completed: Playable native Jak walk run jump test map

Jak walks, runs and jumps around a bounded native test map with original locally baked model/animations, follow camera, floor and platform collision. Both controller trajectories, visual captures and318 native movement checks pass on stock16MiBFlycast. Source only is public; assets and playable embedding ELF stay local. Native C prototype physics, not original GOAL gameplay; full game remains unfinished. (AI-assisted)
See [PLAYABLE_SLICE.md](PLAYABLE_SLICE.md). (AI-assisted).

## Completed: Native capsule movement on original training collision

A native capsule controller now walks and jumps against a576-triangle original training collision patch with retained PAT modes/events. 203 host tests,789 synthetic native checks and2063 combined original-patch checks pass on stock16MiBFlycast. Existing visible test-map controls/platforms still pass. Original GOAL physics and full gameplay remain unfinished; real-terrain renderer remains a private integration prototype. (AI-assisted)
See [WORLD_COLLISION.md](WORLD_COLLISION.md). (AI-assisted).

## Completed: Native Jak movement on original training terrain

A separate native training target now combines real Jak and six original clips with 2,973 original terrain triangles and a 576-face collision patch. 212 host tests, 416 native bundle checks, 261 host GPU-layer checks and 17 integrated stock16MiB Flycast checks pass; the 2,063-check original collision regression still passes. Captured idle and airborne frames were visually reviewed. Strict transform/file binding and reproducible private conversion are published; full original GOAL gameplay remains unfinished. (AI-assisted)
See [TRAINING_SLICE.md](TRAINING_SLICE.md). (AI-assisted).

## Completed: Guarded SH4ZAM view cache and near-plane clipping

The native training renderer retains visible polygons across the near plane with view-space UV/color interpolation, a guarded SH4ZAM view/projected cache and bounded aligned submission. 215 host tests,4,967 native view/clip checks (including285 analytic cases),10 original-free raster checks and18 integrated original-terrain checks pass in stock16MiBFlycast; 2,382 legacy projection checks still pass. Per-vertex clearing and out-of-line libm clipping calls were removed; no hardware speed claim. Full game and camera obstruction remain unfinished. (AI-assisted)
See [NEAR_CLIPPING.md](NEAR_CLIPPING.md). (AI-assisted).

## Completed: Native collision camera on original training geometry

The native training camera now stops against original terrain, shortens immediately and eases extension below each fresh collision limit. A complete 2,032-face collider and WLD3 coverage metadata prevent queries into missing geometry. 218 host tests, 1,205 native camera checks, 454 manifest checks, 20 movement checks and 11 orbit checks pass in stock 16 MiB Flycast. Captured orbit and airborne frames were visually reviewed. The query began as a Grok proposal and was independently verified before integration. Full gameplay remains unfinished. (AI-assisted)
See [CAMERA_COLLISION.md](CAMERA_COLLISION.md). (AI-assisted).

## Completed: Full training collision partition and committed-air foundation

The offline converter preserves all 61,576 original training collision faces across 572 sectors. Independent sampled coverage and repeatability audits pass. Native collision distinguishes known air from missing data; 249 host tests, 81 native air checks and rebuilt camera, collision and movement regressions pass. The transactional native loader is still under development with Grok; render streaming and full gameplay remain unfinished. (AI-assisted)
See [COLLISION_SECTORS.md](COLLISION_SECTORS.md). (AI-assisted).

## Completed: Verified native collision sector loading

A transactional native C loader now streams bounded regions from the complete training collision package. Stock 16 MiB Flycast passes 4,020 checks across 572 windows, exercising all 61,576 source faces with matching rebased payload CRCs. 269 host tests pass; forced allocation/read/close failures preserve the prior cache. The largest sampled cache holds 3,286 faces with 889,764 bytes of tracked geometry allocations at peak. The game target still uses its fixed patch; render streaming and gameplay integration remain unfinished. (AI-assisted)
See [SECTOR_CACHE.md](SECTOR_CACHE.md). (AI-assisted).

## Completed: Replica validation and swept geometry bounds

The collision loader now validates filtered-first replicas in a second bounded pass, rejects coordinate-scaled width slack and pins binary64 cell arithmetic. A helper includes capsule radius and solver skin in coverage bounds. Independent Grok findings were adjudicated and tested: 272 host tests and 5,164 stock 16 MiB Flycast checks pass across 572 original training windows. Peak tracked geometry remains 889,764 bytes. Render streaming and full gameplay remain unfinished. (AI-assisted)
See [SECTOR_CACHE.md](SECTOR_CACHE.md). (AI-assisted).

## Completed: Bounded exact native compressed blocks

A staged zlib decoder now enforces exact input/output lengths, checksums, disjoint buffers and a 64 KiB workspace cap. 273 host tests and 138 native Flycast checks pass, including maximum 512 KiB output, framing/canaries and allocation failures. Measured native decode workspace peaks at 7,132 bytes for the synthetic fixtures. Build documentation now records the zlib dependency. Render-sector and full-game integration remain unfinished. (AI-assisted)
See [SECTOR_INFLATE.md](SECTOR_INFLATE.md). (AI-assisted).

## Completed: Original background LODs in the native training prototype

A standalone host helper now selects original tfrag/tie LODs and calls the unchanged OpenGOAL exporter. Highest-detail re-export matches the original GLB byte for byte. The lower-LOD training patch uses 1,305 terrain triangles instead of 2,973; 20 native movement checks and 11 camera-orbit checks pass in stock 16 MiB Flycast, with reviewed captures. 274 host tests pass. Full render streaming and original GOAL gameplay remain unfinished. (AI-assisted)
See [ORIGINAL_LODS.md](ORIGINAL_LODS.md). (AI-assisted).

## Completed: Exact empty compressed render buckets

The bounded decoder now validates zero-output zlib streams through a separate entry point, supporting empty global buckets without exposing an active output buffer. 274 host tests and 158 native Flycast checks pass, including stored/compressed empty streams, truncation, trailing data and allocation failure. The source and README are updated. Material-preserving render-sector conversion and full gameplay remain unfinished. (AI-assisted)
See [SECTOR_INFLATE.md](SECTOR_INFLATE.md). (AI-assisted).

## Completed: Original material-preserving render sectors and native CPU reader

The full original lower-LOD training background now converts into bounded render files while retaining all 140,166 triangles, reflection maps, strength bytes, normals and sampler/material state. The SH-4 CPU reader passes 9,578 stock 16 MiB Flycast checks over every sector; 88 synthetic-only native checks and 344 host tests also pass. Independent Grok audit findings were reproduced and corrected. Native render-window caching, GPU uploads and full gameplay remain unfinished. (AI-assisted)
See [RENDER_SECTORS.md](RENDER_SECTORS.md). (AI-assisted).

## Completed: Resident original background colors with staged VRAM ownership

All 70 original lower-LOD background color maps now upload and read back exactly in PowerVR VRAM, with texture ownership independent of future geometry-window swaps. 370 original-catalog and 30 synthetic native Flycast checks pass; every staged allocation failure preserves the active owner. Peak requested VRAM for old plus replacement colors is 784,384 bytes. 346 host tests pass. Make also refreshes private ROM disks when switching to older asset directories. Geometry-cache, reflection shader and full gameplay integration remain unfinished. (AI-assisted)
See [RESIDENT_TEXTURES.md](RESIDENT_TEXTURES.md). (AI-assisted).

## Completed: Bounded transactional original background geometry windows

The transactional CPU render cache now passes 2,813 native checks across all 572 original training geometry windows in stock 16 MiB Flycast. Exact face payloads and staged failure rollback are verified, including overfetch and both-filtered replica mismatches. 354 host tests pass. Geometry drawing, streamed playable integration and the full game remain unfinished. (AI-assisted)
See [RENDER_CACHE.md](RENDER_CACHE.md). (AI-assisted).

## Completed: Cached original terrain base pass with SH4ZAM and viewport clipping

The cached renderer now draws a 3,547-face original training window using resident PowerVR colors, packed SH4ZAM world transforms and full viewport clipping. Original and procedural base-pass runs each pass 42 native ownership/readiness checks; six framebuffer samples check masks, depth and sidedness. 367 host tests, 164,852 native world-view checks and 12,620 viewport checks pass. Controller streaming, reflection/TOD shading and full original GOAL gameplay remain unfinished. (AI-assisted)
See [BASE_RENDERING.md](BASE_RENDERING.md). (AI-assisted).

## Completed: Playable cached original terrain with resident PowerVR textures

The native walking prototype now uses cached original terrain, resident PowerVR textures and SH4ZAM transforms. 26 integrated stock-16-MiB Flycast checks pass, including multiple exact-source render windows and stable texture ownership; 93 original-free native failure/reuse/cleanup checks and 371 host tests pass. The earlier prototype retains its 20 passing checks. Captured movement/jump/camera frames were visually reviewed. The region and native C controller remain temporary; full original GOAL gameplay and reflection/TOD fidelity are unfinished. (AI-assisted)
See [TRAINING_STREAM.md](TRAINING_STREAM.md). (AI-assisted).

## Completed: Bounded decoded terrain blocks in native movement

The native training target now retains validated immutable terrain blocks under a 2 MiB raw-byte cap. The same scripted stock-16-MiB Flycast route performs 10 block reads/decodes instead of 200, with matching source windows and payload CRCs. 379 host tests,126 native owner/budget checks and26 integrated movement/stream checks pass. Independent allocation instrumentation observes a 6,477,168-byte live peak without failed allocations. The explicit sampled live-heap gate is now6MiB; the earlier target keeps its4MiB default. Original GOAL/full gameplay, reflection/TOD and physical validation remain unfinished. (AI-assisted)
See [DECODED_BLOCK_CACHE.md](DECODED_BLOCK_CACHE.md). (AI-assisted).

## Completed: Consistent decoded terrain root paths

Roots ending in a slash now work through the decoded terrain cache and training constructor. Grok identified the path-join mismatch; Codex reproduced failures for slash/backslash roots, corrected the joins and independently verified them. 380 host tests,170 native owner/budget/root checks and26 integrated movement/stream checks pass in stock16MiB Flycast. The allocation peak remains6,477,168 bytes with no failed allocations. Full original GOAL gameplay remains unfinished. (AI-assisted)
See [DECODED_BLOCK_CACHE.md](DECODED_BLOCK_CACHE.md). (AI-assisted).

## Completed: Original fixed-size GOAL process pools

Two unchanged original GOAL methods now construct fixed-size process pools and return members through typed method dispatch. 121 new native checks verify exact allocations, finite member chains, owning-pool links and allocation-free reparenting. 382 host tests and 22,770 runtime checks pass in stock 16 MiB Flycast. Returning a member does not deactivate it or clear its PID; full cleanup, scheduler and original gameplay remain unfinished. (AI-assisted)
See [ORIGINAL_DEAD_POOLS.md](ORIGINAL_DEAD_POOLS.md). (AI-assisted).

## Completed: Original protect-frame construction

The unchanged original protect-frame constructor now runs with its three explicit arguments and an authenticated read-only process context. 79 new native checks cover exact frame/process bytes, fences, stacked arguments, caller preservation, context refusal and original frame-chain popping. 390 host tests,22,849 runtime checks and39 freshly rerun numeric checks pass in stock16MiB Flycast. Mutable lexical pp, original deactivation, nonlocal unwind and full gameplay remain unfinished. (AI-assisted)
See [PROTECT_FRAME_ENTRY.md](PROTECT_FRAME_ENTRY.md). (AI-assisted).

## Completed: Lexical process scopes and native fault restoration

Original protect-frame construction and remove-exit now use an authenticated lexical process distinct from scheduler current-process. Process allocation follows nested lexical scopes and restores the prior domain. Native transfer guards and fixed-site return cleanup preserve faults and reject leaked/corrupt scopes. 394 host tests and 23,056 native runtime checks pass in stock 16 MiB Flycast, including 207 new native checks. The separate 39 numeric checks remain verified. Original deactivation, unwind, startup, scheduler and full gameplay remain unfinished. (AI-assisted)
See [LEXICAL_PP_SCOPES.md](LEXICAL_PP_SCOPES.md). (AI-assisted).

## Completed: Allocator-owned process extents and explicit process selection

Original global process allocations now record their declared extents independently of mutable process fields. Unchanged original symbol/raw constructors and fixed-pool ownership/return are verified; explicit lexical selection uses trusted span lookup without publishing allocation as execution. Full-table allocation fails before committing memory, stale physical generations refuse, and real native fiber lookup/scope/teardown guards pass. 398 host tests and 23,145 native runtime checks pass in stock 16 MiB Flycast, including 101 new portable and 89 new native ownership checks. The separate 39 numeric checks remain verified. Moving pools, mutable GOAL pp, scheduler startup, GC/unwinding and full gameplay remain unfinished. (AI-assisted)
See [PROCESS_OWNERSHIP.md](PROCESS_OWNERSHIP.md). (AI-assisted).

## Completed: Original kernel target-configuration inventory

A strict, host-only GOAL reader now inventories every configuration-dependent site in the eight original jak1 kernel files: 6 global-constant roots plus INSTRUCTION_SET, 24 configuration branches (7 inside macro templates), 27 transitively dependent macros/constants/globals, 39 affected top-level forms, 2 scratchpad literals and 8 inline-asm functions with only x86-64/ARM64 bodies. Committed evidence pins LF-normalized source hashes, and a drift check fails when the original source changes. Ten new host tests pass without ISO, SDK or emulator; the same reader parses all 533 jak1 sources. This is an audit, not target configuration: no original values or startup were changed. The dead-pool schema test now skips like other frontend tests when the frontend is not built. (AI-assisted, Claude Code)
See [TARGET_CONFIG_AUDIT.md](TARGET_CONFIG_AUDIT.md). (AI-assisted).

## Completed: Compiled original lexical pp brackets and send-event-function

The unchanged original send-event-function, the dispatcher behind every original send-event, now lowers and executes natively. The exporter tags a written process-typed rlet pp as process_lexical. The lowerer proves one single-entry/single-exit save/assign/restore bracket and emits guarded scope enter/leave bridges, and a call fault inside the bracket restores the saved process. 61 new native checks cover dispatch under the target pp, nested dispatch, original valid?/PID/hook filters, unowned-target refusal and call-fault restore. 23,206 runtime checks, 39 numeric checks and 418 host tests (1 optional skip) pass in stock 16 MiB Flycast. deactivate (r15 asm return), run-function-in-process (catch/throw), scheduler, GC and gameplay remain unfinished. (AI-assisted, Claude Code)
See [LEXICAL_PP_BRACKETS.md](LEXICAL_PP_BRACKETS.md). (AI-assisted).

## Completed: Unchanged original process deactivation

The unchanged original process deactivate now executes natively. Protect-frame exits run newest-first under the dying process as lexical pp, children deactivate recursively through original method slot ten, and original return-process restores pool membership with status, PID, state and entity cleared. The x86 return-from-thread push/ret idiom is recognized exactly and becomes an explicit native transfer. On a running native fiber, the new dc_goal_fiber_abandon completes the context to its kernel root after the original cleanup. With no such kernel continuation it reports DC_GOAL_UNSUPPORTED_TRANSFER, and the initialize throw faults explicitly. 87 new native checks; 23,293 runtime checks, 39 numeric checks and 422 host tests pass in stock 16 MiB Flycast. Next: native throw/catch and original scheduler dispatch. (AI-assisted, Claude Code)
See [ORIGINAL_DEACTIVATE.md](ORIGINAL_DEACTIVATE.md). (AI-assisted).

## Completed: Original suspend on native fibers

The original suspend macro, used by every original process loop, now lowers to a native fiber yield. A read-only x86 r15 memory base is the native constant zero and a reset rsp reads the native SP, so the original debug stack check measures real stack use. The exact top-thread/suspend-hook sequence becomes one dc-c-suspend operation, and each pp reset re-authenticates the process. The unchanged original looping-code suspends a real fiber four times across a destroyed working stack and is then killed by original deactivate. 66 new native checks; 23,359 runtime checks, 39 numeric checks and 425 host tests pass in stock 16 MiB Flycast. Next: enter-state, throw/catch and the original kernel-dispatcher. (AI-assisted, Claude Code)
See [ORIGINAL_SUSPEND.md](ORIGINAL_SUSPEND.md). (AI-assisted).

## Completed: Original enter-state on native fibers

The unchanged original enter-state, the transition behind every go, now executes natively, and all six gstate functions lower. Explicit x86 .mov/.add asm ops normalize to 64-bit moves and adds. The exact enter-state jump window becomes one state_code_transfer, and the new dc_goal_fiber_restart re-boots the fiber at the state code on the reset stack, deactivating the process when the code returns. 63 new native checks cover state installation, the enter/trans calls, code arguments and pp, the stack reset, a body that never resumes and return-through-deactivate. 23,422 runtime checks, 39 numeric checks and 426 host tests pass in stock 16 MiB Flycast. Next: throw/catch and the original kernel-dispatcher. (AI-assisted, Claude Code)
See [ORIGINAL_ENTER_STATE.md](ORIGINAL_ENTER_STATE.md). (AI-assisted).

## Completed: Original kernel-dispatcher scheduling native fibers

The unchanged original kernel-dispatcher now schedules a real process. A first explicit Dreamcast target choice, dreamcast_profiler_context.gc (PC_PROFILER_ENABLE #f), removes the desktop pc-prof calls without changing original bodies. The new native dc-c-thread-resume is the original thread resume-hook: it steps a registered fiber under an explicit ownership-checked binding, and native suspend marks the process suspended like original thread-suspend. Over four frames the original tree walk and lambda resume an original looping-code process, which suspends each time across a destroyed working stack. Ready, unregistered and dead processes are handled. 63 new native checks; 23,485 runtime checks, 39 numeric checks and 428 host tests pass in stock 16 MiB Flycast. Next: native reset-and-call for trans/post, throw/catch and a per-frame native main loop. (AI-assisted, Claude Code)
See [ORIGINAL_DISPATCHER.md](ORIGINAL_DISPATCHER.md). (AI-assisted).

## Measured: original kernel lowering coverage

survey_kernel_lowering.py lowers every original jak1 kernel function with the native runtime's ordered contexts, including the Dreamcast profiler context: gcommon 57/65, gkernel 65/75, gstate 6/6, gstring 38/38, 166 of 184 in total. Eight refusals are the kernel's x86/ARM64 assembly thread primitives, whose native counterparts are the fiber engine bridges and are not compiled. The rest are four 128-bit/vector functions, two vec4s printers, two mixed float variadic printers, one dead-pool-heap inspector, and run-function-in-process (catch/throw). Compilation only; evidence/kernel-lowering-survey.json is checked by a host test. (AI-assisted, Claude Code)

## Open: native catch/throw and process initialization (measured blockers)

Original run-function-in-process (the initializer behind every process-spawn) is blocked first by stack allocation, before catch/throw. It builds its parameter array with (new 'stack-no-clear 'array 'uint64 6) and its catch frame with (new 'stack 'catch-frame ...), which the typed exporter currently emits as unsupported "mov reg, stack-slot-N" operations (IR 10 and 28). Order of work: (1) export stack-slot addresses with their size and alignment; (2) give each slot a fixed, non-reused frame region in the SH-4 lowerer; (3) provide a native catch-frame constructor (method 0, x86/ARM64 assembly in the original) that links a catch frame into the lexical process's stack-frame-top, calls the function with its 8 parameters, and unlinks on normal return; (4) a native throw/throw-dispatch, which needs a new closed SH-4 continuation that restores the catch site's callee-saved state after running intervening protect-frame exits. Until (4), deactivate and enter-state initialize paths fault explicitly through the unbound throw, as they do now. (AI-assisted, Claude Code)

## Completed: Kernel fiber physical lifetime guards

Claude Code's original dispatcher, trans/post hooks, state transitions, suspension and deactivation are integrated and independently verified. Native fibers now capture the physical process allocation generation; stale continuations and registered scheduler hooks refuse before restoring stacks or changing process fields. The original same-address suspended-resume defect was reproduced and corrected. 430 host tests, 23,556 native runtime checks (35 new lifetime/layout checks) and a freshly rebuilt 39-check numeric regression pass in stock 16 MiB Flycast. Original stack objects/catch/throw, full kernel boot, moving heaps/GC and full gameplay remain unfinished. (AI-assisted, Codex with human-authorized Grok review; prior kernel work by Claude Code)
See [KERNEL_OWNERSHIP_LIFETIMES.md](KERNEL_OWNERSHIP_LIFETIMES.md). (AI-assisted).

## Completed: Native kernel stack objects

Original frontend stack arrays and boxed stack objects now execute on SH-4 in both compiler modes, including calls, lexical cleanup and suspension after destroyed working stacks. The unchanged original run-function-in-process now compiles in both modes; native catch/throw execution remains next. 443 host tests, 23,639 native runtime checks (83 new) and 39 freshly rebuilt numeric checks pass in stock 16 MiB Flycast. Full kernel boot and gameplay remain unfinished. (AI-assisted: Codex with reviewed human-authorized Grok proposal; prior kernel process-system work by Claude Code)
See [KERNEL_STACK_OBJECTS.md](KERNEL_STACK_OBJECTS.md). (AI-assisted).

## Completed: Native same-stack catch return

A private SH-4 invoke/escape primitive now restores the exact caller stack, fixed return site, saved integer/floating registers, FPUL and FPSCR across normal and nested nonlocal returns. An independent clobber oracle passes 802 new native checks: 24,441 total runtime checks and 445 host tests pass in stock 16 MiB Flycast. The unchanged numeric binary matches its prior sealed 39-check execution. This is a trusted-C dependency, not an installed GOAL catch/throw implementation; original catch integration, unwinding, full kernel boot and gameplay remain unfinished. (AI-assisted: Codex; human-authorized isolated Grok review requested separately)
See [NATIVE_CATCH_RETURN.md](NATIVE_CATCH_RETURN.md). (AI-assisted).

## Completed: Original root process initialization

Unchanged original run-function-in-process, throw and protect construction now execute on SH-4 through guarded native catch boundaries. Verified paths include full six-argument results, named and nested throws, extra lexical scope unwinding, original protect exits, and initialize-dead with original deactivation and fixed-pool return. 447 host tests and 24,793 native runtime checks (352 new) pass in stock 16 MiB Flycast. The separate numeric ELF matches its prior sealed 39-check execution. This first catch adapter requires the root C stack and an owned lexical process scope; fiber catch lifetime, cross-stack unwinding, moving heaps/GC, full kernel boot and gameplay remain unfinished. (AI-assisted: Codex; separately requested human-authorized Grok review remains pending)
See [ORIGINAL_INITIALIZATION.md](ORIGINAL_INITIALIZATION.md). (AI-assisted).

## Completed: complete original kernel compilation

176/184 original kernel functions compile to SH-4 and the other 8 assembly primitives bind to named native counterparts. 128-bit values, the variadic integer-slot convention and --native-binding are documented in [KERNEL_COMPILATION.md](KERNEL_COMPILATION.md). 24,846 runtime, 39 numeric and 452 host checks pass. (AI-assisted, Claude Code)

## Next: native boot of the original kernel startup (measured plan)

Each whole-file startup now lowers (gcommon, gstring, gkernel with the 8 native bindings, gstate). The only desktop C++-kernel services those startups call are method-set! (58 calls), malloc (2, in gkernel) and _format (from inspect/print, including gkernel's startup (inspect *test-vec4s*)). dgo-load is referenced by load-package but not called at startup. Steps:
1. Native bridges bound as method-set!, malloc and _format. A bounded _format implements the directives the kernel uses (~A ~S ~D ~X ~x ~F ~f ~% ~T, widths, #t/#f/0 destinations) and reads variadic floats as bit patterns in integer slots.
2. A Dreamcast memory context (BIG_MEMORY #f: PROCESS_HEAP_MULT 1, about 984 KiB nk heap) plus *debug-segment* #f set by the native kernel driver, as the C++ kernel does.
3. A separate boot ELF with a 3-4 MiB arena (the startup allocates the 32 KiB DRAM stack, the 128 KiB fake scratchpad, the nk heap, the fixed pools and the listener process), loading gcommon, gstring, gkernel and gstate with their initializers in order.
4. Assert the resulting globals: *kernel-context*, *dram-stack*, *listener-process*, *null-process*, every dead pool and *dead-pool-list*, *active-pool* and *display-pool* trees. Then spawn a process through the original pools and run it under the original kernel-dispatcher.
(AI-assisted, Claude Code)

All eight original KERNEL.CGO startups now run natively, unchanged and in original order: 50 of 50 boot checks pass in stock 16 MiB Flycast. Scalar bitfields such as handle now use the integer ABI, and _format prints symbols by name. Help wanted next: spawn a process through the original pools inside the boot image and run it under the original kernel-dispatcher. See KERNEL_BOOT.md. (AI-assisted)

A process started the original way (get-process from *8k-dead-pool*, activate, set-to-run with looping-code) now runs three frames under the original kernel-dispatcher inside the boot image, then dies through original deactivate and returns to its pool: 87 of 87 boot checks pass. dreamcast_thread_context.gc raises PROCESS_STACK_SAVE_SIZE to 512, because a suspended native main thread needs 348 bytes. Help wanted next: an original GOAL caller for the fiber import, then DGO loading. (AI-assisted)

The kernel now also boots from /rd/KERNEL.CGO: an original-layout DGO container of DCSH native objects, loaded by goal_dgo.c (native load_and_link_dgo: link, then run each startup in order). 90 of 90 checks pass, with code bytes, arena use and symbols identical to the compiled-in boot; compressed and CRC-damaged DGOs refuse without linking. Help wanted next: compile and load GAME.CGO's engine files the same way. (AI-assisted)

GAME.CGO has started: build_game_cgo.py compiles the original engine files in game.gd order on top of the kernel. types-h, vu1-macros and math load from /rd/GAME.CGO after KERNEL.CGO and run their startups (98 of 98 boot checks). Help wanted next: VU0 vector register (vf, 128-bit) lowering to SH-4, which blocks vector-h and most of the engine; fipr/ftrv and sh4zam are the natural targets. (AI-assisted)

Codex completed the independent [VU0 oracle and GAME.CGO survey](VU0_REFERENCE.md): 17 new host tests, including a separate SSE comparison, and all 346 archive entries inventoried. Help wanted: compare emitted SH-4 bit patterns against the declared finite-input desktop profile, then load original vector-h; Claude owns the lowering/boot files. PS2-exact exceptional arithmetic and gameplay are not covered. (AI-assisted)

VU0 vector registers now lower to SH-4 (VU0_LOWERING.md): vf arithmetic, masks, broadcasts, multiply-add, blend/swizzle/splat, itof/ftoi, sqrt, div, max/min and vf moves, with static vector loads. vector-h and the next files compile; the DGO boot loads 13 GAME.CGO files and 141 of 141 vector cases match ChatGPT/Codex's independent oracle bit for bit (112 of 112 boot checks; 24,846 runtime, 39 numeric and 478 host tests pass). Help wanted next: bounding-box (unsupported tagged/scalar parameter), a larger boot arena (97% of 4 MiB used), and an sh4zam-style FIPR/FTRV speed pass checked against the same cases. (AI-assisted)

GAME.CGO now compiles in game.gp dependency order: one exporter --batch run covers all 331 files of its dependency graph. 92 of 326 code files lower to SH-4 and 48 load and start in Flycast (148 of 148 boot checks; 24,846 runtime, 39 numeric and 491 host tests pass). Added: native MMI ops (checked against an independent x86 model), structure allocation, linker type placeholders and the original cpad-open/cpad-get-data on the Dreamcast controller. Help wanted next: a Dreamcast memory budget (display's PS2 DMA buffers need 5.2 MiB), stack homes for `&` locals (res), and rlet register aliases in the exporter (54 of 56 export refusals). See GAME_CGO.md. (AI-assisted)

Dreamcast memory budget (MEMORY_BUDGET.md): the memory context never took effect (defglobalconstant stores PC_BIG_MEMORY unevaluated, so the list form read as true), so the kernel ran with the PC 3x actor heap. Fixed: KERNEL.CGO startup is down from 3.92 to 1.89 MB. Budget values substituted at read time by the exporter (DMA_BUFFER_GLOBAL_SIZE = PS2 1,712 KiB), plus native kmalloc, scf-get-time and a fault-on-call __pc-get-mips2c. All 92 compiled GAME.CGO files now load and start (192 of 192 boot checks; 24,846 runtime, 39 numeric and 491 host tests pass). Help wanted: SH-4 mips2c implementations (80 lookups), a level-heap streaming design (LEVEL_HEAP_SIZE is 10.4 MiB per level), and `&` locals (res). (AI-assisted)

mips2c probe: probe_mips2c_sh4.py syntax-checks all 25 jak1 game/mips2c C++ files for SH-4 with sh-elf-g++. All 25 parse with only a scalar stand-in for 8 SSE intrinsics and a non-Linux REALLY_INLINE. Parsing only; linking, the GOAL-address memory model and execution are untested. Help wanted: build one (draw_string or texture) into the DGO boot behind __pc-get-mips2c and check it against desktop results. (AI-assisted)

`(& var)` of register locals now lowers (res): the exporter emits a typed register_address op and the lowerer pins that variable's stack slot. The runtime gained the kscheme asize-of/copy defaults for object and basic (surface-h's `copy`). 97 of 326 GAME.CGO code files lower and all 97 load and start in Flycast (197 of 197 boot checks; 24,847 runtime, 39 numeric and 502 host tests pass). Help wanted next: `go` from a non-behavior method (fact-h's fact-info new), calling GOAL methods from C (copy of a basic with its own asize-of), SH-4 mips2c implementations, and level-heap streaming. See GAME_CGO.md. (AI-assisted)
