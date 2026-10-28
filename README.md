# Jak and Daxter: Dreamcast port workbench

**Contributors wanted: KallistiOS/PowerVR, SH-4, asset conversion and GOAL runtime.**

This is an independent, experimental fork of [OpenGOAL](https://github.com/open-goal/jak-project)
working toward a native Dreamcast adaptation of **Jak and Daxter: The Precursor Legacy**.
**A small native movement prototype works in Flycast. The full game is not playable.**
It is not an official OpenGOAL, Sony or Naughty Dog project.

The Dreamcast implementation and documentation are extensively **AI-assisted**,
using OpenAI Codex, human-authorized Grok Build collaboration and, for the
2026-10-26 kernel process-system milestones, Claude Code (Anthropic). Proposals are
reviewed and independently tested before integration. Local automated checks
cover the supported subset; expert human review is still needed.

## Kernel handoff status

**2026-10-26 (AI-assisted, Claude Code): the [handoff](ports/dreamcast/CLAUDE_KERNEL_HANDOFF.md) blocker is resolved.**
The initialize-to-state experiment now runs end to end in the default build:
1. unchanged original root initialization runs `enter-state` inside its catch;
2. original `set-to-run` arms the main thread, and the throw returns `#t`;
3. the native main import accepts the armed image;
4. the state's enter and code run on the reset stack;
5. original `deactivate` returns the process to its pool.

The wrong `set-to-run` argument was `#f`. The test harness bypassed original
`activate`, which is what stores `main-thread`; no compiler change was needed.
**447 host tests, 24,844 Flycast checks and 39 numeric checks pass.**
Details are in [ORIGINAL_INITIALIZATION.md](ports/dreamcast/ORIGINAL_INITIALIZATION.md).

**Kernel boot (2026-10-26, AI-assisted):** the top-level startup of every original
KERNEL.CGO file now runs natively, unchanged and in original order, in stock 16 MiB
Flycast. A process then starts the original way (get-process, activate,
run-next-time-in-process) and runs under the original kernel-dispatcher:
**87 of 87 boot checks pass.** The same kernel also boots from a KERNEL.CGO DGO
file of native objects on the romdisk. Details are in
[KERNEL_BOOT.md](ports/dreamcast/KERNEL_BOOT.md).

**VU0 vector registers (2026-10-27, AI-assisted):** the SH-4 lowerer now handles
GOAL's 128-bit `vf` registers, so the original vector-h and the files after it
compile. The DGO boot now loads the first **13** original GAME.CGO files (through
transformq-h). **141 of 141 vector cases match bit for bit** expectations computed
without the lowering, by ChatGPT/Codex's independent oracle and IEEE binary32.
**112 of 112 boot checks pass.** Details are in
[VU0_LOWERING.md](ports/dreamcast/VU0_LOWERING.md).

**GAME.CGO (2026-10-27, AI-assisted):** files now compile in the original game.gp
dependency order, in one frontend run covering all 331 files of the GAME.CGO
dependency graph, and **all 331 now export**. **275 of GAME.CGO's 326 code files** lower to SH-4,
and **the first 237 load and run their original startups** in Flycast: **347 of 347 boot checks pass**, including a
process spawned from a file's startup (time-of-day) that then runs and suspends under the original dispatcher. New native
pieces:
- PS2 MMI integer ops;
- `structure` allocation;
- linker type placeholders;
- the original `cpad-open`/`cpad-get-data` on the Dreamcast controller;
- `(& var)` of register locals (res) and the kscheme asize-of/copy defaults (surface-h);
- `(go ...)` from a method (fact-h) and the float ABI for `meters`/`degrees` (subdivide-h);
- behaviors with arguments and process-subtype `self` (navigate-h), and GAME.CGO streamed
  from a zstd romdisk copy (4.9 MB → 0.66 MB) so a 12 MiB arena fits;
- shared 128-bit stack slots (font), constant-branch folding, sized forward type references
  (`type-ref ... :method-count`) and a reviewed list of never-assigned PC-port locals;
- one register record per frontend alias (behaviors' `self`/`pp`), `(suspend)` in behaviors, and
  no debug segment, as a retail kernel (915 debug functions dropped, references null);
- MIPS `$zero` locals, `r15` resets, 128-bit `#f`, integer value types (`seconds`) and reviewed
  stale locals from documented original compiler bugs.

A Dreamcast memory budget ([MEMORY_BUDGET.md](ports/dreamcast/MEMORY_BUDGET.md)) fixed the
kernel's PC-sized actor heap (the target context never took effect before) and gives
display PS2-sized DMA buffers. mips2c functions are fault-on-call stubs for now, but all
25 jak1 mips2c C++ files already pass an SH-4 syntax check (`probe_mips2c_sh4.py`), so
compiling them natively looks viable. 627 host tests pass.
Details are in [GAME_CGO.md](ports/dreamcast/GAME_CGO.md).
GC, the rest of the engine and gameplay remain unfinished.

**Codex support handoff completed (AI-assisted):** the independent
[vector oracle and usage survey](ports/dreamcast/VU0_REFERENCE.md) cover desktop
finite-input arithmetic and all 346 GAME.CGO entries (326 source files, 20 data
objects). The oracle has hand-checked and independent SSE tests. Claude Code owns
the separate vf lowering; these host tools do not add native gameplay behavior.
Host validation: 470 tests passed, one optional exporter test skipped (471 total).

## Progress — 2026-10-26

| Milestone | Status |
| --- | --- |
| Native test-map prototype | Source published: real Jak model and six original animation clips, analog walk/run, jump, solid platforms, follow/orbit camera and clean exit |
| Original training-island collision | Source published: bounded capsule movement against a 576-triangle patch with retained surface metadata |
| Original training-island rendering + movement | Source published: separate native target, private asset conversion, transform/file binding; 20 integrated checks pass, including boundary respawn |
| Collision sector loading | Source published: 572 native original-training window loads verified; game/render streaming integration pending |
| Original render package | Source published: all 140,166 original lower-LOD triangles/material inputs retained; native reader, cache and base-pass drawing verified; bounded native movement integrated; original GOAL/reflections pending |
| Cached world base pass | Source published: original 3,547-face window rendered with resident colors; bounded controller streaming verified; reflections pending |
| Decoded terrain blocks | Source published: explicit immutable-file cache, 2 MiB raw cap/128 LRU slots; scripted reads/decodes reduced from 200 to 10 |
| CPU render windows | Source published: all 572 original windows verified, transactional rollback; bounded playable base-pass integration verified |
| Resident color textures | Source published: 70 original color maps verified in VRAM; staged failure preserves old owner; base-pass geometry integrated in bounded native movement |
| Original background LODs | Source published: original LOD selection; reduced training patch passes movement and camera checks |
| Original kernel process system | Source published: unchanged original `send-event-function`, `deactivate`, `(suspend)`, `enter-state` and `kernel-dispatcher` (including trans/post hooks) run on native SH-4 fibers |
| Original kernel compilation | Complete: 176 of 184 original kernel functions compile to SH-4, and the other 8 (x86/ARM64 assembly thread primitives) bind to named native counterparts; whole-file startups lower; see [KERNEL_COMPILATION.md](ports/dreamcast/KERNEL_COMPILATION.md) |
| Original kernel boot | Verified: all 8 KERNEL.CGO startups run natively in original order, and an originally spawned process runs under the original dispatcher (90/90 checks); also boots from a native KERNEL.CGO DGO and loads the first 237 GAME.CGO files (347/347); see [KERNEL_BOOT.md](ports/dreamcast/KERNEL_BOOT.md) |
| Original process initialization | Root C-stack path verified: unchanged initializer, throw, protect exits, native catches and original deactivate/pool return; initialization to a scheduled running state verified end to end; fiber catches pending |
| VU0 vector registers | Verified: vf ops lower to SH-4; 141/141 lane-exact cases against an independent oracle; see [VU0_LOWERING.md](ports/dreamcast/VU0_LOWERING.md) |
| Original GAME.CGO | In progress: 275/326 code files lower to SH-4 and the first 237 load and start in Flycast under a Dreamcast memory budget; the arena (full at ambient), mips2c and an AICA sound driver next; see [GAME_CGO.md](ports/dreamcast/GAME_CGO.md) |
| Full original game | Unfinished: fiber catch lifetime, cross-stack unwinding, the rest of GAME.CGO, memory budget, GC, general gameplay, progression or release build |

The playable prototypes use a **native C controller**, as a temporary milestone.
They do not execute original GOAL movement. The real-terrain target and local
conversion/build instructions are now published in
[TRAINING_SLICE.md](ports/dreamcast/TRAINING_SLICE.md); converted assets stay private.

See [PLAYABLE_SLICE.md](ports/dreamcast/PLAYABLE_SLICE.md) for the published
test-map source, controls, local asset preparation and limitations, and
[WORLD_COLLISION.md](ports/dreamcast/WORLD_COLLISION.md) for original collision.
Analog stick moves, A jumps, L/R orbit the camera, and START exits.

See [NEAR_CLIPPING.md](ports/dreamcast/NEAR_CLIPPING.md) for the new guarded
SH4ZAM view cache, clipped terrain and original-free raster evidence.
[CAMERA_COLLISION.md](ports/dreamcast/CAMERA_COLLISION.md) covers the new checked
camera boom, expanded 2,032-face collider and loaded-coverage requirements.
[SECTOR_CACHE.md](ports/dreamcast/SECTOR_CACHE.md) covers verified native collision
sector loading, bounded memory, private oracle preparation and pending game integration.
[ORIGINAL_LODS.md](ports/dreamcast/ORIGINAL_LODS.md) covers original LOD export and
the verified reduced training scene; distance-based LOD and streaming remain open.
[RENDER_SECTORS.md](ports/dreamcast/RENDER_SECTORS.md) covers full original lower-LOD
material preservation, bounded CPU decoding and verified storage/window measurements.
[RESIDENT_TEXTURES.md](ports/dreamcast/RESIDENT_TEXTURES.md) covers resident VRAM colors,
staged replacement and independent geometry/texture ownership.
[RENDER_CACHE.md](ports/dreamcast/RENDER_CACHE.md) covers bounded transactional geometry
windows, full replica checks and bounded ownership.
[BASE_RENDERING.md](ports/dreamcast/BASE_RENDERING.md) covers the integrated cached
world-view/clip/base-pass diagnostic.
[TRAINING_STREAM.md](ports/dreamcast/TRAINING_STREAM.md) covers the new bounded
playable integration and 26 movement/stream checks.
[DECODED_BLOCK_CACHE.md](ports/dreamcast/DECODED_BLOCK_CACHE.md) covers the bounded
immutable-file cache, 170 owner/budget/root checks, revised live-heap gate and allocation peak.
[TARGET_CONFIG_AUDIT.md](ports/dreamcast/TARGET_CONFIG_AUDIT.md) inventories the original
kernel's PC/profiler/VM/instruction-set branches, scratchpad literals and 8 inline-asm
functions that need Dreamcast decisions before original startup (host-only source audit).

The original kernel process system is documented milestone by milestone in
[LEXICAL_PP_BRACKETS.md](ports/dreamcast/LEXICAL_PP_BRACKETS.md) (events),
[ORIGINAL_DEACTIVATE.md](ports/dreamcast/ORIGINAL_DEACTIVATE.md) (process death),
[ORIGINAL_SUSPEND.md](ports/dreamcast/ORIGINAL_SUSPEND.md) (per-frame suspension),
[ORIGINAL_ENTER_STATE.md](ports/dreamcast/ORIGINAL_ENTER_STATE.md) (state transitions) and
[ORIGINAL_DISPATCHER.md](ports/dreamcast/ORIGINAL_DISPATCHER.md) (the per-frame scheduler,
trans/post hooks and the first explicit Dreamcast target context).
`survey_kernel_lowering.py` reports per-function kernel compilation coverage.

## Current verification

All native results below are local **Flycast tests with stock 16 MiB RAM**.
Physical Dreamcast hardware remains untested. These counts are separate suites
and checkpoints, not a percentage of game completion or a single full-game run.

| Area | Locally verified scope |
| --- | --- |
| Host | 447 tests pass with the complete local SDK/oracle setup |
| GOAL runtime | 24,853 native checks at its latest verified checkpoint |
| Numeric regression | 39 native checks |
| Original kernel processes | Events, deactivation (including self-deactivation on a fiber), suspension across destroyed stacks, state transitions and four-frame scheduling with trans/post hooks, and initialization through to a scheduled running state, inside the 24,844 runtime checks |
| Kernel compilation coverage | 166/184 original kernel functions lower; remaining refusals named in `evidence/kernel-lowering-survey.json` |
| Prototype movement | 318 native checks; visible test-map controller and platform paths pass |
| Original collision | 789 synthetic native checks; 2,063 combined checks including the original 576-triangle patch |
| Collision file loader | 147 host checks |
| Sector loading | 5,164 native cache/IO/rollback checks across 572 windows; all 61,576 original faces exercised |
| Compressed blocks | 158 native exact-framing, workspace and canary checks; used by the new CPU render reader |
| Cached base-pass raster | Original/procedural frames each pass 42 native owner/readiness checks; six pixel samples verify masks, depth and sidedness |
| Packed world view | 164,852 native SH4ZAM precision/layout/mode/state checks; maximum 24,576 vertices without XYZ staging |
| Viewport clipping | 12,620 native checks against 256 exact rational hulls, including eight-vertex and long-face cases |
| CPU render cache | 2,813 native checks across 572 original windows in five bounded runs; exact payload CRC and failure rollback |
| Resident color catalog | 370 original / 30 synthetic native upload, exact VRAM readback and staged ownership checks; 392,192 resident color bytes |
| Render package IO | 9,578 native checks across 3,158 original local files plus global geometry; 88 synthetic native checks; base-pass drawing verified; bounded native movement integrated; original GOAL/reflections pending |
| Near-plane rendering | 4,967 native view/clip checks (including 285 analytic clip cases), plus 10 synthetic raster checks |
| Collision camera | 1,205 native query/rig checks; 454 WLD2/3 manifest checks; 11 original-terrain orbit checks |
| Decoded block workload | Same scripted route: 10 full reads/decodes versus 200; exact window CRCs retained, stock 16 MiB RAM |
| Cached playable terrain | 26 integrated movement/stream checks; exact-source windows, resident texture reuse and cleanup; captured idle/air/camera/boundary frames reviewed |
| Combined render owner | 170 original-free native source/failure/rollback/reuse/known-empty/cleanup and block-budget/bypass/eviction checks |
| Original-terrain prototype | 20 integrated checks: movement, jump edges, landing, camera, boundary respawn, memory and exit; captured idle/air frames visually reviewed |

The earlier fixed-patch original-LOD training prototype renders 3,593 character triangles and
1,305 terrain triangles. Its measured live heap at readiness is 2,470,144 bytes;
this excludes embedded asset data, static storage, KOS and stacks. It reports
5,302,696 bytes of free VRAM. These are emulator measurements, not hardware
speed or full-game memory claims.

The renderer uses native PowerVR textures and upstream **SH4ZAM** projection
with preserved SH-4 matrix state. Earlier isolated diagnostics verified a
6,144-byte VQ texture against a 32,768-byte uncompressed version, and reduced
the same 20 shift probes from 3,244 to 836 emitted bytes. Those measurements
do not establish a general frame-rate improvement.

See [verification-summary.json](ports/dreamcast/evidence/verification-summary.json),
[RUNTIME.md](ports/dreamcast/RUNTIME.md), [NATIVE_MODULES.md](ports/dreamcast/NATIVE_MODULES.md)
and [TEXTURES.md](ports/dreamcast/TEXTURES.md). The summary records local
checkpoint evidence, not a GitHub Actions run. Original PS2 MIPS code is
not executed by these native tests; runtime and renderer are separate programs.

## Latest completed contributor task

Unchanged original run-function-in-process, throw and protect construction now execute on SH-4 through guarded native catch boundaries. Verified paths include full six-argument results, named and nested throws, extra lexical scope unwinding, original protect exits, and initialize-dead with original deactivation and fixed-pool return. 447 host tests and 24,793 native runtime checks (352 new) pass in stock 16 MiB Flycast. The separate numeric ELF matches its prior sealed 39-check execution. This first catch adapter requires the root C stack and an owned lexical process scope; fiber catch lifetime, cross-stack unwinding, moving heaps/GC, full kernel boot and gameplay remain unfinished. (AI-assisted: Codex; separately requested human-authorized Grok review remains pending)

See [ORIGINAL_INITIALIZATION.md](ports/dreamcast/ORIGINAL_INITIALIZATION.md). Full kernel/gameplay remains unfinished.

### Previous: native same-stack catch return

A private SH-4 invoke/escape primitive now restores the exact caller stack, fixed return site, saved integer/floating registers, FPUL and FPSCR across normal and nested nonlocal returns. An independent clobber oracle passes 802 new native checks: 24,441 total runtime checks and 445 host tests pass in stock 16 MiB Flycast. The unchanged numeric binary matches its prior sealed 39-check execution. This is a trusted-C dependency, not an installed GOAL catch/throw implementation; original catch integration, unwinding, full kernel boot and gameplay remain unfinished. (AI-assisted: Codex; human-authorized isolated Grok review requested separately)

See [NATIVE_CATCH_RETURN.md](ports/dreamcast/NATIVE_CATCH_RETURN.md). Full kernel/gameplay remains unfinished.

### Previous: native kernel stack objects

Original frontend stack arrays and boxed stack objects now execute on SH-4 in both compiler modes, including calls, lexical cleanup and suspension after destroyed working stacks. The unchanged original run-function-in-process now compiles in both modes; native catch/throw execution remains next. 443 host tests, 23,639 native runtime checks (83 new) and 39 freshly rebuilt numeric checks pass in stock 16 MiB Flycast. Full kernel boot and gameplay remain unfinished. (AI-assisted: Codex with reviewed human-authorized Grok proposal; prior kernel process-system work by Claude Code)

See [KERNEL_STACK_OBJECTS.md](ports/dreamcast/KERNEL_STACK_OBJECTS.md). Full kernel/gameplay remains unfinished.

### Previous: kernel lifetime guards

Claude Code's original dispatcher, trans/post hooks, state transitions, suspension and deactivation are integrated and independently verified. Native fibers now capture the physical process allocation generation; stale continuations and registered scheduler hooks refuse before restoring stacks or changing process fields. The original same-address suspended-resume defect was reproduced and corrected. 430 host tests, 23,556 native runtime checks (35 new lifetime/layout checks) and a freshly rebuilt 39-check numeric regression pass in stock 16 MiB Flycast. Original stack objects/catch/throw, full kernel boot, moving heaps/GC and full gameplay remain unfinished. (AI-assisted, Codex with human-authorized Grok review; prior kernel work by Claude Code)

See [KERNEL_OWNERSHIP_LIFETIMES.md](ports/dreamcast/KERNEL_OWNERSHIP_LIFETIMES.md). Full kernel/gameplay remains unfinished.

### Previous: original kernel dispatcher

The unchanged original `kernel-dispatcher`, the per-frame scheduler, now runs a real process. Under an explicit Dreamcast target context that removes the desktop profiler, the original tree walk resumes an original `looping-code` process through its thread's resume-hook, now a native fiber step. The process runs until its original `(suspend)`, frame after frame, across a deliberately destroyed working stack. Original trans and post hooks run each frame on their original temporary threads (main stack and DRAM stack) through a native `reset-and-call`. 99 new native checks pass; 23,521 runtime checks, 39 numeric checks and 430 host tests pass in stock 16 MiB Flycast. See [ORIGINAL_DISPATCHER.md](ports/dreamcast/ORIGINAL_DISPATCHER.md). (AI-assisted, Claude Code)

### Previous: original enter-state

The unchanged original `enter-state`, the transition behind every original `go`, now runs natively. It installs the new state's hooks, calls its enter and trans functions, then restarts the process's fiber at the state's code on a reset main-thread stack, and deactivates the process when that code returns, exactly as the original's stack-reset jump does. All six original gstate functions now lower. 63 new native checks pass; 23,422 runtime checks, 39 numeric checks and 426 host tests pass in stock 16 MiB Flycast. See [ORIGINAL_ENTER_STATE.md](ports/dreamcast/ORIGINAL_ENTER_STATE.md). (AI-assisted, Claude Code)

### Previous: original suspend

Original `(suspend)`, which every original process loop uses to wait for the next frame, now yields a native fiber. The unchanged original `looping-code` suspends a real fiber four times across a deliberately destroyed working stack, with its original debug stack check measuring the real native SP, and is then killed by original `deactivate`. 66 new native checks pass; 23,359 runtime checks, 39 numeric checks and 425 host tests pass in stock 16 MiB Flycast. See [ORIGINAL_SUSPEND.md](ports/dreamcast/ORIGINAL_SUSPEND.md). (AI-assisted, Claude Code)

### Previous: process deactivation

The unchanged original process `deactivate`, which is how every original process dies, now executes natively. Protect-frame exits run under the dying process, children deactivate recursively, and processes return to their original pools with status, PID and state cleared. A running process that deactivates itself on a native fiber does the original cleanup and then returns to its kernel root, discarding its stack as the original does. 87 new native checks pass; 23,293 runtime checks, 39 numeric checks and 422 host tests pass in stock 16 MiB Flycast. See [ORIGINAL_DEACTIVATE.md](ports/dreamcast/ORIGINAL_DEACTIVATE.md). (AI-assisted, Claude Code)

### Previous: lexical pp brackets

The unchanged original `send-event-function`, which dispatches every original `send-event`, now executes natively. Its event hook runs under the target process as lexical `pp`, and the caller's `pp` is restored afterwards, including on a call fault. The compiler proves the original save/assign/restore bracket and refuses any other shape. 61 new native checks pass; 23,206 runtime checks, 39 numeric checks and 418 host tests pass in stock 16 MiB Flycast. See [LEXICAL_PP_BRACKETS.md](ports/dreamcast/LEXICAL_PP_BRACKETS.md). Full deactivation, scheduler, GC and gameplay remain unfinished. (AI-assisted, Claude Code)

### Previous: allocator-owned process extents

Original global process allocations now record their declared extents independently of mutable process fields. Unchanged original symbol/raw constructors and fixed-pool ownership/return are verified; explicit lexical selection uses trusted span lookup without publishing allocation as execution. Full-table allocation fails before committing memory, stale physical generations refuse, and real native fiber lookup/scope/teardown guards pass. 398 host tests and 23,145 native runtime checks pass in stock 16 MiB Flycast, including 101 new portable and 89 new native ownership checks. The separate 39 numeric checks remain verified. Moving pools, mutable GOAL pp, scheduler startup, GC/unwinding and full gameplay remain unfinished. (AI-assisted)

See [PROCESS_OWNERSHIP.md](ports/dreamcast/PROCESS_OWNERSHIP.md). Full kernel/gameplay remains unfinished.

## Build and orientation

Source is in `ports/dreamcast/`. Follow [BUILDING.md](ports/dreamcast/BUILDING.md).
The verified baseline is OpenGOAL `6076aab5a95955443a9e1ed8f72a25bc0e2fcf9d`;
the earlier verified port source checkpoint is
`8edc8d0e17cdc9421b1c4d8c17bc84dc669e820e`. The kernel process-system milestones
are verified at the tip of the `dreamcast` branch; each milestone's
`ports/dreamcast/evidence/*-verification.json` records its ELF hash and counts.

On Windows, keep the workspace in a short path (for example `C:\dcw`, with
`jak-project`, `dc-sdk`, `kos-source`, `og-host` and `flycast` side by side).
Under a deep profile path, CMake object paths and IR outputs exceed the
260-character limit. The SH-4 build also invokes `python3`.

No ISO, BIOS, extracted textures/models, converted game scenes or ELF binaries
embedding game assets are uploaded. Prepare assets locally from your own game
copy. Synthetic parser/runtime fixtures are included. Please never attach game
assets to commits, discussions or bug reports.

## Next work and contributing

The immediate kernel gates are process initialization and a native frame loop:
1. stack-allocated objects (`new 'stack`) in the exporter and lowerer;
2. a native `catch-frame` constructor and `throw`, so original
   `run-function-in-process` can initialize processes;
3. a deliberate Dreamcast `PROCESS_STACK_SIZE`;
4. driving the original dispatcher each frame from the native main loop.

The measured blockers are on the
[task board](ports/dreamcast/HELP_WANTED.md). On the rendering side, broader
original-world traversal, animation blending and rendering fidelity remain open.

The full port still needs complete kernel startup, broader ABI coverage,
heaps/GC and failure unwinding; original movement and
collision fidelity; animation transitions, broader camera behavior, world streaming,
enemies, UI, audio, save/load and progression. Packaging and physical hardware
validation come later. Allocation currently uses bounded prototype mechanisms,
not the complete original heap or garbage collector.

Start with [CONTRIBUTING.md](CONTRIBUTING.md) and the
[contributor task board](ports/dreamcast/HELP_WANTED.md). Most compiler/host work
needs no game ISO. Discuss scope on **this fork**, keep changes small enough to
review, and distinguish tested behavior from proposed behavior.

## Attribution and license

OpenGOAL and the port source retain the [ISC license](LICENSE) and existing
copyright notices; dependencies retain their own licenses. The original
project overview is preserved in [README_UPSTREAM.md](README_UPSTREAM.md).
This fork's AI disclosure is in [AI_POLICY.md](AI_POLICY.md); the upstream
policy is preserved separately in [AI_POLICY_UPSTREAM.md](AI_POLICY_UPSTREAM.md).
