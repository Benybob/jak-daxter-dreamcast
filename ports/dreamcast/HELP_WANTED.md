# Contributor task board

2026-10-21. (AI-assisted). These tasks are open invitations, not assigned work.
Start a discussion on this fork or submit a focused human-reviewed change.
Most compiler/runtime work uses synthetic fixtures and needs no original assets.

| Task | Useful experience | Concrete acceptance target |
| --- | --- | --- |
| Correct local/argument export | C++, compiler IR | The local-vars reproduction exports actual arguments separately from named locals; initialization checks remain strict |
| Unified early return lowering | CFGs, SH-4 ABI | Early-return reproduction returns 77 for input 1 and 99 otherwise; real basic-type?/type-type? and float/void cases pass native checks |
| Literal pool islands | SH-4 assembler/linkers | Original sort lowers beyond the terminal pool limit; GNU assembler and native control-flow/relocation checks pass |
| Stable symbol mangling | Python, compiler tooling | 1/, +, - and other colliding names coexist in one generated module with deterministic distinct identifiers |
| Kernel sequencing and heaps | GOAL, runtime systems | Explicit target configuration and original startup/module dependencies execute within measured stock RAM bounds |
| PowerVR scene pipeline | KOS, PVR, graphics | Measured culling/material deduplication and bounded residency on synthetic fixtures before expanding game scenes |
| Hardware validation | Dreamcast development | Reproduce asset-free native diagnostics on hardware with toolchain, load method, RAM use and failures documented |

## Two ready-to-run compiler bugs

Run `python ports/dreamcast/repros/reproduce.py` after building the frontend.
Both failures are intentional current safeguards, not passing gameplay tests.

- **local-vars.gc:** the original compiler puts named locals into
  FunctionEnv::params. Our exporter assumes each entry has an incoming argument
  copy, then reports `parameter has no incoming IR value`. Preserve real ABI
  arguments, locals and behavior/self constraints distinctly. Do not invent
  incoming values for uninitialized locals.
- **early-return.gc:** an early return writes the shared function return value
  and jumps past the normal IR_Return to the final label. Current nonvoid
  fallthrough validation rejects this. Export/handle the shared return and
  unified exit correctly; do not simply remove the guard. Test liveness and
  definite assignment across all paths, plus float and none ABIs.

## Other measured gaps

Of 47 isolated original gcommon defuns, 31 lower, six fail lowering and ten fail
frontend export. Some failures are missing dependencies caused by isolation.
These are compile-only results, not link/execution or full-file coverage.
Original sort hits the PC-relative literal range limit; qmem-copy variants need
explicit 128-bit/vector work. Full gcommon currently fails parameter export;
gkernel/gstate need earlier globals/types and original module context.

Full-file target setup must also address upstream PC_PORT/BIG_MEMORY settings,
MMIO, profiler hooks and memory limits. Do not execute desktop-configured
startup under the assumption that passing scalar probes validates it.

The first integrated gameplay target is a bounded Geyser Rock slice with movement,
jumping, collision and camera. Complete gameplay, streaming, audio, saves,
progression, disc packaging and hardware support remain much larger milestones.
