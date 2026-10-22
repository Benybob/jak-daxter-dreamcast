# Jak and Daxter: Dreamcast port workbench

**Contributors wanted: SH-4 compiler, GOAL runtime, KallistiOS and PowerVR.**

This is an independent, experimental fork of [OpenGOAL](https://github.com/open-goal/jak-project)
working toward a native Dreamcast adaptation of **Jak and Daxter: The Precursor Legacy**.
**It is not playable. There is no full kernel boot, player movement or release build.**
It is not an official OpenGOAL, Sony or Naughty Dog project.

The Dreamcast-specific implementation and documentation were developed extensively
with **OpenAI Codex (AI-assisted)**. Local automated checks provide evidence for
the supported subset; expert human review is still needed. No claim of complete
correctness, full-game feasibility or physical-hardware performance is made.

## Help us reach the first playable slice

The first gameplay milestone is a small Geyser Rock area with a controllable
player, jumping, collision and a working camera. Before that, we need broader
original kernel startup, process/heap support and module sequencing.

Start with [CONTRIBUTING.md](CONTRIBUTING.md) and the
[contributor task board](ports/dreamcast/HELP_WANTED.md). Resolved named-local and early-return
regressions are included; most compiler/host work needs no game ISO.
Discuss scope on **this fork**, and keep changes small enough to review.

## Current evidence — 2026-10-21

| Area | Locally verified scope |
| --- | --- |
| Host | 171 tests pass with the complete local SDK/oracle setup |
| Native runtime | 11,757 checks in Flycast, stock 16 MiB RAM |
| Original data | 182 native checks; all 668 V4 data objects across 36 local archives validate |
| Numeric regression | 39 native checks |
| GOAL backend | Original typed frontend to native SH-4; scalar calls, classes/methods, void startup, private data, 64-bit shifts and 32-bit division |
| Module loading | Bounded native code/data files, transactional linking, explicit startup and KOS file-span loading |
| Graphics | One real Geyser Rock grass material subset: 7,784 triangles, indexed geometry and native PowerVR VQ |

These counts include many arithmetic vectors and metadata assertions. They do
not measure a percentage of game completion. Original PS2 MIPS code is never
executed. Host and renderer diagnostics are separate programs.

SH-4 compaction reduces the same 20 shift probes from 3,244 to 836 emitted
bytes. The real-scene VQ texture occupies 6,144 rather than 32,768 bytes,
freeing exactly 26,624 VRAM bytes in the local test. These are size measurements,
not physical-console speed claims. Flycast is our current testing platform;
retail Dreamcast hardware remains untested.

See [verification-summary.json](ports/dreamcast/evidence/verification-summary.json),
[RUNTIME.md](ports/dreamcast/RUNTIME.md), [NATIVE_MODULES.md](ports/dreamcast/NATIVE_MODULES.md)
and [TEXTURES.md](ports/dreamcast/TEXTURES.md). The summary records results from
the local checkpoint, not a GitHub Actions run.

## Latest completed contributor task

Typed structured static images now preserve original bytes and type/symbol/data/function relocations with strict closed-graph validation. An explicit inclusive global range executes the single unchanged original kernel-context definition, preserving flags/PID/false references while excluding later stack/scratchpad setup. Six host tests and 56 native checks pass. Full kernel startup, implicit pp/self, threads, GC, failure unwinding and gameplay remain unfinished.

See [STRUCTURED_STATIC.md](ports/dreamcast/STRUCTURED_STATIC.md). Full kernel/gameplay remains unfinished.

## Build and orientation

Source is in `ports/dreamcast/`. Follow [BUILDING.md](ports/dreamcast/BUILDING.md).
The verified baseline is OpenGOAL `6076aab5a95955443a9e1ed8f72a25bc0e2fcf9d`;
the latest locally verified port checkpoint is
`025fa01fdab0f3316291ae003e2daf1d5daea12c`.

No ISO, BIOS, extracted textures/models, converted game scenes or ELF binaries
embedding game assets are uploaded. Use assets extracted locally from your own
game copy. Synthetic parser/runtime fixtures are included. Please never attach
game assets to commits, discussions or bug reports.

## What remains

Full kernel boot, general vector/mixed ABIs, process scheduling and heaps,
bounded world streaming, player animation/collision, enemies, UI, audio,
save/load, progression, disc packaging and real hardware validation remain open.
Allocation currently uses a bounded bump arena; this is not a complete original
heap or garbage collector. See the task board for concrete next gates.

## Attribution and license

OpenGOAL and the port source retain the [ISC license](LICENSE) and existing
copyright notices; bundled dependencies retain their own licenses. The original
project overview is preserved in [README_UPSTREAM.md](README_UPSTREAM.md).
This fork's AI disclosure is in [AI_POLICY.md](AI_POLICY.md); the upstream
policy is preserved separately in [AI_POLICY_UPSTREAM.md](AI_POLICY_UPSTREAM.md).
