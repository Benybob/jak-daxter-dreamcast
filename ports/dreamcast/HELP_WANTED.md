# Contributor task board

2026-10-21. (AI-assisted). These tasks are open invitations, not assigned work.
Start a discussion on this fork or submit a focused human-reviewed change.
Most compiler/runtime work uses synthetic fixtures and needs no original assets.

| Task | Useful experience | Concrete acceptance target |
| --- | --- | --- |
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
Original sort hits the PC-relative literal range limit; qmem-copy variants need
explicit 128-bit/vector work. Full gcommon now exports 66 typed functions; its lowering/execution remains
open. The new full-export standalone lowering inventory is 54 of 66; this
does not link or execute the whole module. Gkernel/gstate need earlier globals/types
and original module context.

Full-file target setup must also address upstream PC_PORT/BIG_MEMORY settings,
MMIO, profiler hooks and memory limits. Do not execute desktop-configured
startup under the assumption that passing scalar probes validates it.

The first integrated gameplay target is a bounded Geyser Rock slice with movement,
jumping, collision and camera. Complete gameplay, streaming, audio, saves,
progression, disc packaging and hardware support remain much larger milestones.
