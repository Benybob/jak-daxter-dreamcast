# Contributor task board

2026-10-21. (AI-assisted). These tasks are open invitations, not assigned work.
Start a discussion on this fork or submit a focused human-reviewed change.
Most compiler/runtime work uses synthetic fixtures and needs no original assets.

| Task | Useful experience | Concrete acceptance target |
| --- | --- | --- |
| Unified early return lowering | CFGs, SH-4 ABI | Early-return reproduction returns 77 for input 1 and 99 otherwise; real basic-type?/type-type? and float/void cases pass native checks |
| Literal pool islands | SH-4 assembler/linkers | Original sort lowers beyond the terminal pool limit; GNU assembler and native control-flow/relocation checks pass |
| Stable symbol mangling | Python, compiler tooling | 1/, +, - and other colliding names coexist in one generated module with deterministic distinct identifiers |
| Kernel sequencing and heaps | GOAL, runtime systems | Explicit target configuration and original startup/module dependencies execute within measured stock RAM bounds |
| PowerVR scene pipeline | KOS, PVR, graphics | Measured culling/material deduplication and bounded residency on synthetic fixtures before expanding game scenes |
| Hardware validation | Dreamcast development | Reproduce asset-free native diagnostics on hardware with toolchain, load method, RAM use and failures documented |

## Completed: named-local argument export

The local-vars regression is fixed and protected by eight integration tests and
32 native checks. Arguments come from the original constrained entry values;
named locals and fixed process/self entries remain separate. Complete gcommon
exports 66 functions, without claiming full lowering or execution. See
[LOCAL_VARIABLES.md](LOCAL_VARIABLES.md). Expert review is still welcome.

## Ready-to-run reproductions

Run `python ports/dreamcast/repros/reproduce.py` after building the frontend.
The local-vars example now lowers successfully. Early-return remains rejected
by the current safeguard. Neither reproduction is a gameplay test.

- **local-vars.gc (resolved):** verify it lowers and run the named-local tests.
  Unsupported process/self entry ABIs and uninitialized locals still fail closed.
- **early-return.gc:** an early return writes the shared function return value
  and jumps past the normal IR_Return to the final label. Current nonvoid
  fallthrough validation rejects this. Export/handle the shared return and
  unified exit correctly; do not simply remove the guard. Test liveness and
  definite assignment across all paths, plus float and none ABIs.

## Other measured gaps

The initial inventory (before the named-local fix) found that of 47 isolated
original gcommon defuns, 31 lowered, six failed lowering and ten failed
frontend export. Some failures are missing dependencies caused by isolation.
These are compile-only results, not link/execution or full-file coverage.
Original sort hits the PC-relative literal range limit; qmem-copy variants need
explicit 128-bit/vector work. Full gcommon now exports 66 typed functions; its lowering/execution remains
open. Gkernel/gstate need earlier globals/types and original module context.

Full-file target setup must also address upstream PC_PORT/BIG_MEMORY settings,
MMIO, profiler hooks and memory limits. Do not execute desktop-configured
startup under the assumption that passing scalar probes validates it.

The first integrated gameplay target is a bounded Geyser Rock slice with movement,
jumping, collision and camera. Complete gameplay, streaming, audio, saves,
progression, disc packaging and hardware support remain much larger milestones.
