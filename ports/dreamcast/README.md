# Jak 1 on Dreamcast: experimental port workbench

**Codex support handoff completed (AI-assisted):** [VU0_REFERENCE.md](VU0_REFERENCE.md)
documents the independent finite-input desktop vector oracle, SSE comparison tests
and inventory of all 346 GAME.CGO entries. Claude Code owns vf lowering in
lower_sh4_ir.py; this support change leaves the lowering/boot files alone.

**2026-10-27 (AI-assisted):** [GAME.CGO](GAME_CGO.md) compiles in game.gp dependency
order in one frontend run; all 331 files of its dependency graph export. 222 of 326 code
files lower to SH-4 (275 of 326 now), and the first 207 load and start in Flycast (307 of 307 boot checks) under a [memory budget](MEMORY_BUDGET.md). Native MMI ops, structure allocation, linker type
placeholders and the original pad services on the Dreamcast controller. All 25 jak1
mips2c C++ files pass an SH-4 syntax check ([MEMORY_BUDGET.md](MEMORY_BUDGET.md)). `(& var)` locals
and the kscheme asize-of/copy defaults are native too, as are `go` from a method and the
float ABI for `meters`/`degrees`, behaviors with arguments, and a zstd-streamed GAME.CGO.
Next: process-spawn from a startup (time-of-day), compiling mips2c natively, and level heaps.
Spare VRAM as a data pool is a design note only ([VRAM_POOL.md](VRAM_POOL.md)).

**2026-10-27 (AI-assisted):** [VU0 vector registers](VU0_LOWERING.md) now lower to
SH-4. The original vector-h compiles, and the DGO boot loads 13 GAME.CGO files.
141 of 141 vector cases match the independent oracle bit for bit (112 of 112 boot checks).

**2026-10-26 (AI-assisted):** [Original kernel boot](KERNEL_BOOT.md) runs the
top-level startup of all eight original KERNEL.CGO files natively, unchanged and in
original order. A process started the original way then runs under the original
kernel-dispatcher. 87 of 87 boot checks, 24,846 runtime checks, 452 host tests and 39
numeric checks pass in stock 16 MiB Flycast. The kernel also boots from a KERNEL.CGO DGO
of native DCSH objects and loads the compiled GAME.CGO prefix. GC and gameplay remain
unfinished.

**2026-10-26 (AI-assisted):** [Original root initialization](ORIGINAL_INITIALIZATION.md)
now executes unchanged `run-function-in-process`, `throw` and protect construction
through guarded native catch boundaries. Tests cover nested throws, lexical
unwinding, initialize-go and initialize-dead with original pool return. 447 host
tests and 24,793 runtime checks pass in stock 16 MiB Flycast. Fiber catch lifetime,
cross-stack unwinding and complete kernel boot remain unfinished.

**2026-10-26 (AI-assisted):** [Native catch return](NATIVE_CATCH_RETURN.md)
adds a private SH-4 invoke/escape primitive with fixed return sites. Nested
escapes restore native registers and stack under an independent clobber oracle:
445 host tests and 24,441 runtime checks pass in stock 16 MiB Flycast. Original
catch/throw integration, unwinding and complete kernel boot remain unfinished.

**2026-10-26 (AI-assisted):** [Kernel stack objects](KERNEL_STACK_OBJECTS.md)
now survive calls, lexical process scopes and original suspension in both
compiler modes. The unchanged original `run-function-in-process` now compiles;
native catch/throw execution is the next dependency. 443 host tests, 23,639
runtime checks and 39 numeric checks pass in stock 16 MiB Flycast. The kernel
lowering survey reaches 167/184 functions; full kernel boot remains unfinished.

**2026-10-26 (AI-assisted):** [Kernel lifetime guards](KERNEL_OWNERSHIP_LIFETIMES.md)
extend Claude Code's original dispatcher, state and deactivation work. Suspended
fibers and scheduler hooks now refuse a reclaimed process generation before
restoring its stack or changing process fields. The integrated suite passes
430 host tests, 23,556 native runtime checks and 39 numeric checks. Original
stack objects/catch/throw and complete kernel boot remain unfinished.

**2026-10-26 (AI-assisted):** [Process ownership](PROCESS_OWNERSHIP.md) now
captures original global process allocations, including original fixed-pool
members, and supports explicit selection using their known extents. Raw buffers
require their trusted owner's actual size; original pool return preserves
ownership. The native runtime passes 23,145 checks in stock 16 MiB Flycast;
398 host tests pass.
Moving pools, original scheduler startup and full gameplay remain unfinished.

**2026-10-25 (AI-assisted):** [Cached base rendering](BASE_RENDERING.md) now
draws an original 3,547-face training window through SH4ZAM, full viewport
clipping and resident PowerVR colors. Original and procedural frames each pass
42 native ownership checks; procedural pixels check masks, depth and sidedness.
The host suite passes 367 tests. Walking-prototype integration, reflections
and the full game remain unfinished.

**2026-10-25 (AI-assisted):** [Render windows](RENDER_CACHE.md) adds a bounded
transactional CPU cache with complete replica checks and old-window rollback.
All 572 original training geometry windows match independent payload oracles;
2,813 native checks pass across five bounded stock 16 MiB Flycast runs, and
354 host tests pass. [Resident colors](RESIDENT_TEXTURES.md) separately verifies
70 original maps in VRAM. Streamed drawing and full-game integration remain open.

**2026-10-24 (AI-assisted):** [Original-terrain movement](TRAINING_SLICE.md) now
has a separate reproducible native target: real Jak and original clips, walking,
running and jumping on a bounded training-island render/collision patch. It
passes 20 integrated checks in stock 16 MiB Flycast, plus 454 native bundle
checks and 261 host GPU-layer checks. The full host suite passes 218 tests.
[Camera obstruction](CAMERA_COLLISION.md) adds 1,205 native query/rig checks,
WLD3 coverage, and an 11-check original-terrain camera orbit.
[Near clipping](NEAR_CLIPPING.md) adds 4,967 native view/clip checks and a
10-check synthetic raster oracle, retaining the visible part of crossing triangles.
The separate GOAL runtime remains at its verified 22,649 checks and numeric
regression at 39. The full game remains unfinished. [SH4ZAM](SH4ZAM.md) drives
camera projection; install the upstream KOS library before building renderers.

**Current continuation (2026-10-20, AI-assisted):** native Windows SH-4/KOS
builds and an indexed, textured real Geyser Rock mesh run in Flycast with
16 MiB RAM. The instruction/ABI/cache bootstrap passes 17 target checks.
[GOAL_SH4.md](GOAL_SH4.md) tracks the separate, incomplete compiler/runtime
work. [MILESTONE_2.md](MILESTONE_2.md) preserves the earlier synthetic viewer.

Started 2026-10-20 against OpenGOAL commit
`6076aab5a95955443a9e1ed8f72a25bc0e2fcf9d`. AI-assisted implementation.

This workbench targets a native Dreamcast port of **Jak and Daxter: The
Precursor Legacy**. A small native movement prototype is playable in Flycast;
the full GOAL compiler, runtime and gameplay are unfinished. Source contains no original
game assets, ISO or BIOS; use assets from your own PS2 copy.

## What is implemented

- A portable C99 DGO/CGO container reader, shared by the host and Dreamcast
  programs, with a fixed 4,096-byte payload buffer and no parser heap allocation.
  KOS, stdio, the stack and the executable use additional memory.
- Sequential object scanning, 16-byte alignment handling, CRC32 checksums,
  object sizes and offsets, and strict malformed/truncated input checks.
- A host CLI producing JSON lines, plus an archive-size audit tool that reports
  objects exceeding a configurable hypothetical staging budget.
- KallistiOS target source with screen status, debug-console object checksums,
  controller exit, CD archive selection and a synthetic ROM-disk fallback.
- Color and indexed/textured PowerVR viewers and static GLB converters. The
  textured path supports opaque/masked materials and twiddled ARGB1555 textures.
  Static viewers omit skins/animation; the separate playable targets use offline
  baked character poses. Blended materials remain unsupported.
- Shared host/target validators, negative asset tests, GNU assembler comparisons
  for emitted SH-4 code, and memory-based Flycast control/exit checks.
- A native code emitter with integer/float ABI, relocations and code-cache
  probes. The original typed GOAL frontend and strict SH-4 subset now execute
  numeric functions, loops and linked calls. This is not a complete game compiler.

The original disc data was validated locally and 24 backgrounds exported.
The verified 7,784-triangle training grass mesh uses 6,934 indexed vertices,
213,120 geometry bytes and one shared 32,768-byte texture. Orbit, zoom, cleanup
and malformed-texture rejection pass target checks. This is one material mesh,
not the whole island. **Physical Dreamcast hardware is untested.** The current
user test target is Flycast only; no playable game or bootable disc is supplied.

## Use on a PC

On Windows, use `build_host_windows.ps1` for native host checks and
`setup_windows_sdk.py` / `build_dreamcast.py` for SH-4 builds without WSL.
On Linux, use Python 3, GNU Make and a C compiler. From the repository root:

```sh
cd ports/dreamcast
make test fixture
build/dgo-probe romdisk/probe.dgo
```

The known CRC32 of the synthetic `crc-vector` object is `cbf43926`.
To measure archives from an existing OpenGOAL Jak 1 build:

```sh
python3 audit_assets.py ../../out/jak1/iso --output asset-report.json
```

You can also supply individual paths, such as `KERNEL.CGO` or `BEA.DGO`.
The default 4 MiB staging budget is an experiment parameter, not a measured
requirement. Change it using `--staging-mib`. The audit returns nonzero if any
archive fails validation; do not treat a partial report as a successful scan.

**Scope:** uncompressed containers only. Compressed `oZlB` files are unsupported;
use uncompressed output generated by OpenGOAL's DGO writer, or explicitly
decompress your local source data first. Upstream `tools/dgo_unpacker` can unpack
compressed archives and `tools/dgo_packer` can repack them. Do not rename a
compressed file and expect decompression. This strict parser also rejects the
short final-object cases that upstream `DgoReader.cpp` sometimes tolerates.
Payload object versions and CPU architectures are not checked, because this
program never executes them.

On success, host output ends with an `archive` summary. Earlier object records
are provisional until that summary and a zero exit status are received.
CRC32 permits comparing host and target reads; it is not an authenticity check.
The parser caps the declared count at 65,536 objects as a diagnostic guard.

## Build the Dreamcast diagnostic

Install [KallistiOS](https://github.com/KallistiOS/KallistiOS), including its
SH-4 cross toolchain, and source your actual KOS `environ.sh`. A pinned Linux
x86_64 prebuilt setup script is provided; see `MILESTONE_2.md`. Then:

```sh
cd ports/dreamcast
make
```

This produces `jak-dc-probe.elf` and `jak-dc-scene.elf` with generated synthetic
assets embedded in their ROM disk. Load an ELF through your established KOS loader or
Dreamcast emulator setup. It is not itself a self-booting CDI.

On boot, it first attempts `/cd/JAKPROBE.DGO`. If opening that file fails,
including when no CD filesystem is available, it attempts `/rd/probe.dgo` and
labels the source as synthetic. An invalid external archive produces a failure,
without silently retrying the synthetic fixture. The external file must be an
uncompressed DGO container; a `.CGO` may be copied with that filename because
the container layout is the same. Game code is never run.

For target acceptance, record the KOS commit/toolchain version and:

1. Cross-compile and link the ELF without warnings.
2. Boot the synthetic fixture: expect two objects and CRC32 `cbf43926` for the
   second object in the debug log. Confirm START exits.
3. Put the same uncompressed local archive on the test disc as `JAKPROBE.DGO`.
   Compare every object's size/CRC against the host probe.
4. Test a truncated external archive: expect FAIL.
5. Measure actual executable, stack, heap and filesystem overhead before
   assigning the remaining RAM to game data.

## Why this is a large port

The inspected checkout has x86 and ARM64 emitter targets, but **no SH-4 backend**.
Its CMake build also applies desktop instruction flags and pulls in desktop
dependencies. Building the PC runtime with a different compiler is insufficient.

The desktop runtime defines a 128 MiB EE address space, maps it above 32-bit
address range, and uses enlarged heap layouts. This is an address-space and
layout observation, **not a measurement that Jak requires 128 MiB resident**.
Retail Dreamcast has 16 MiB system RAM, 8 MiB video RAM and 2 MiB sound RAM;
these are separate pools. Existing GOAL execution conventions, graphics and
streaming assumptions need new implementations and profiling.

| Area | Required work | Relevant upstream source |
| --- | --- | --- |
| GOAL compilation | SH-4 instruction emission, register allocation, branch/literal pools, software handling of required 64-bit and vector operations | `goalc/emitter/InstructionSet.h`, `goalc/compiler/IR.cpp`, `goalc/compiler/CodeGenerator.cpp` |
| Runtime and linker | Define a 32-bit SH-4 calling convention, relocations, GOAL/C bridges, context switches and instruction-cache synchronization | `game/kernel/common/kscheme.cpp`, `common/link_types.h`, `game/kernel/jak1/klink.cpp` |
| RAM | Design a bounded heap layout and measure real residency; changing one memory constant will not fix it | `common/goal_constants.h`, `game/kernel/common/memory_layout.h`, `goal_src/jak1/kernel/gcommon.gc` |
| Graphics | Offline conversion for PVR-friendly geometry/textures; new PowerVR renderer; SH-4 geometry/animation work replacing PS2 vector-unit paths | `game/graphics/`, `goal_src/jak1/engine/gfx/`, `game/mips2c/jak1_functions/` |
| Streaming and sound | Bounded disc reads, level/object lifetime management and an AICA audio backend | `game/overlord/jak1/`, `game/sound/` |
| Input and saves | Maple controller mapping, camera controls for one stick, VMU saves | `game/sce/`, `game/kernel/common/kmemcard.cpp` |

For a feasibility build, investigate 320x240 at 30 fps, lower texture sizes,
reduced geometry and effects, and smaller streaming units. These are proposed
tradeoffs; no performance or full-game feasibility has been demonstrated.
A 32 MiB RAM modification would not remove the CPU, graphics or ABI work.

## Next milestones and acceptance gates

| Order | Deliverable | Pass condition |
| --- | --- | --- |
| 1 | Target asset probe, cross-built and emulator-tested | Hardware verification remains; emulated archive checksums match |
| 2 | PowerVR geometry viewer and converter, cross-built and emulator-tested with synthetic terrain | Real extracted geometry/textures and retail-hardware measurements remain |
| 3 | Minimal GOAL SH-4 compiler/runtime slice | Arithmetic, branches, calls, tagging, symbols and relocation tests execute on SH-4 and match host results |
| 4 | Animated Jak and movement in one bounded scene | Controller movement, animation and collision work inside the measured memory budget |
| 5 | Small playable area | Scene streaming, camera, interactions, sound and VMU save/load work together |
| 6 | Broader Jak 1 port | Remaining gameplay, content conversion, memory tuning and compatibility are completed and tested |

The static scene is an early feasibility test before investing in a complete
compiler backend. Container scanning does not demonstrate that the scene fits
after linking, decoding, animation and collision data are resident.

## Sources and provenance

- [OpenGOAL](https://github.com/open-goal/jak-project), pinned above.
- `common/link_types.h`, `common/util/DgoReader.cpp` and
  `common/util/DgoWriter.cpp` define the container layout followed by this reader.
- `common/util/FileUtil.cpp` defines upstream compressed-DGO handling.
- [KOS Makefile example](https://github.com/KallistiOS/KallistiOS/blob/master/examples/dreamcast/hello/Makefile)
  and [rules](https://github.com/KallistiOS/KallistiOS/blob/master/Makefile.rules).
- [KOS BIOS-font API](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/include/dc/biosfont.h).
- [KOS system memory capacities](https://kos-docs.dreamcast.wiki/group__hw__memsizes.html)
  and [hardware overview in KOS source](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/hardware/asic.c).

The new work is covered by the repository's ISC license. See `LICENSE` at the
repository root. No upstream issue or pull request has been opened.
# Windows continuation (2026-10-20, AI-assisted)

Host tools can be built and tested with `./build_host_windows.ps1`. It adds the
compiler DLL directory to PATH for the current process; no system settings
are changed. Use `-Compiler` for a different MinGW GCC installation.

Actual OpenGOAL v0.3.9 background GLBs contain unnamed meshes. Select them with
`convert_scene.py input.glb output.dcm --mesh-index 123` (zero-based index).
Transforms on unselected branches are skipped; selected ancestor transforms
still apply. Some exported wind-instance matrices are unsupported, so full
level conversion may still require choosing static subsets.

`embed_scene.py base.elf scene.dcm output.elf` makes an asset-only repack of the
checksum-pinned milestone viewer with exactly 512 triangles. It preserves ELF
size, addresses and executable code, updates ROMFS metadata/checksums and names
the replacement `/rd/scene.dcm`. This is useful without a Linux SH-4 toolchain;
it is not a fresh cross-build and does not allow larger embedded scenes.

`verify_windows_smoke.py flycast.exe viewer.elf --output build/windows-check`
captures actual emulated VRAM and checks renderer progression and 16 MiB RAM.
It uses an isolated copy of portable Flycast. The Linux `verify_flycast.py`
continues to provide stronger serial, clean-exit and controller assertions.
