# Jak and Daxter: Dreamcast port workbench

**Contributors wanted: KallistiOS/PowerVR, SH-4, asset conversion and GOAL runtime.**

This is an independent, experimental fork of [OpenGOAL](https://github.com/open-goal/jak-project)
working toward a native Dreamcast adaptation of **Jak and Daxter: The Precursor Legacy**.
**A small native movement prototype works in Flycast. The full game is not playable.**
It is not an official OpenGOAL, Sony or Naughty Dog project.

The Dreamcast implementation and documentation are extensively **AI-assisted**,
using OpenAI Codex and human-authorized Grok Build collaboration. Proposals are
reviewed and independently tested before integration. Local automated checks
cover the supported subset; expert human review is still needed.

## Progress — 2026-10-25

| Milestone | Status |
| --- | --- |
| Native test-map prototype | Source published: real Jak model and six original animation clips, analog walk/run, jump, solid platforms, follow/orbit camera and clean exit |
| Original training-island collision | Source published: bounded capsule movement against a 576-triangle patch with retained surface metadata |
| Original training-island rendering + movement | Source published: separate native target, private asset conversion, transform/file binding; 20 integrated checks pass, including boundary respawn |
| Collision sector loading | Source published: 572 native original-training window loads verified; game/render streaming integration pending |
| Full original game | Unfinished: no complete kernel boot, general gameplay, progression or release build |

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

## Current verification

All native results below are local **Flycast tests with stock 16 MiB RAM**.
Physical Dreamcast hardware remains untested. These counts are separate suites
and checkpoints, not a percentage of game completion or a single full-game run.

| Area | Locally verified scope |
| --- | --- |
| Host | 269 tests pass with the complete local SDK/oracle setup |
| GOAL runtime | 22,649 native checks at its latest verified checkpoint |
| Numeric regression | 39 native checks |
| Prototype movement | 318 native checks; visible test-map controller and platform paths pass |
| Original collision | 789 synthetic native checks; 2,063 combined checks including the original 576-triangle patch |
| Collision file loader | 147 host checks |
| Sector loading | 4,020 native cache/IO/rollback checks across 572 windows; all 61,576 original faces exercised |
| Near-plane rendering | 4,967 native view/clip checks (including 285 analytic clip cases), plus 10 synthetic raster checks |
| Collision camera | 1,205 native query/rig checks; 454 WLD2/3 manifest checks; 11 original-terrain orbit checks |
| Original-terrain prototype | 20 integrated checks: movement, jump edges, landing, camera, boundary respawn, memory and exit; captured idle/air frames visually reviewed |

The locally built real-terrain prototype renders 3,593 character triangles and 2,973
terrain triangles. Its measured live heap at readiness is 2,559,928 bytes;
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

A transactional native C loader now streams bounded regions from the complete training collision package. Stock 16 MiB Flycast passes 4,020 checks across 572 windows, exercising all 61,576 source faces with matching rebased payload CRCs. 269 host tests pass; forced allocation/read/close failures preserve the prior cache. The largest sampled cache holds 3,286 faces with 889,764 bytes of tracked geometry allocations at peak. The game target still uses its fixed patch; render streaming and gameplay integration remain unfinished. (AI-assisted)

See [SECTOR_CACHE.md](ports/dreamcast/SECTOR_CACHE.md). Full kernel/gameplay remains unfinished.

## Build and orientation

Source is in `ports/dreamcast/`. Follow [BUILDING.md](ports/dreamcast/BUILDING.md).
The verified baseline is OpenGOAL `6076aab5a95955443a9e1ed8f72a25bc0e2fcf9d`;
the latest locally verified port source checkpoint is
`c7efd49fbad9e16b3a30c2d31d68c7c87f3685f5`.

No ISO, BIOS, extracted textures/models, converted game scenes or ELF binaries
embedding game assets are uploaded. Prepare assets locally from your own game
copy. Synthetic parser/runtime fixtures are included. Please never attach game
assets to commits, discussions or bug reports.

## Next work and contributing

The immediate next gates are render-sector loading and broader original-world
traversal, followed by continued original-kernel/gameplay startup work.
Animation blending and additional rendering fidelity remain open.

The full port still needs complete kernel startup, broader ABI coverage,
process scheduling, heaps/GC and failure unwinding; original movement and
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
