# Original main-thread arming and KOS stack ownership

2026-10-24. (AI-assisted). Bounded native startup; full scheduler/gameplay unfinished.

The unchanged original `set-to-run` now compiles as an eight-scalar-argument
routine. `dc_goal_arm_main_thread` validates a ready original main CPU thread,
canonical exports, disjoint code/schema/namespace spans and its working stack,
then calls the original routine. Its ten stores set waiting-to-run, bootstrap PC,
initial SP, target and six full-width arguments. It does not execute the target.
The explicit C `dc_goal_fiber_init_main` adapter validates that armed image and
imports its target/arguments into a native cooperative context. The original
assembly bootstrap remains a fail-closed guard; this is not original scheduler
execution, return cleanup, deactivation or exception unwinding. Arming never
binds process identity. Callers supply the exact six-scalar target prototype,
pin live storage and release old contexts before arming.

## Timer-safe borrowed stacks

A real timing failure exposed a platform dependency: KOS's scheduler compared
an interrupted P0 GOAL stack pointer against the owning OS thread's original P1
root stack record and asserted stack underrun. Logging merely changed interrupt
timing. Both synchronous calls and fibers now borrow their owning KOS thread's
stack record while running on the validated working stack. IRQ masking covers
record/SP transitions; normal timer interrupts remain enabled during the body.
Return restores the root stack record and SR before enabling interrupts there.
KOS's stack guard and scheduler are unchanged.

Each native context records its live OS owner and suspended SR. Cross-thread,
interrupt-context, masked-SR and unsupported-control entry fail before restoring
an image. Live OS owner and pinned stack/context storage must outlive execution.
Thread exit, cancellation, migration, arbitrary metadata mutation, NMI-driven
switching, blocking-call generality and unwind across these scopes are unsupported.
Working-stack capacity must include interrupt headroom. Context size is now
304 bytes on SH4; saved register offsets retain their existing layout.

## Verified boundary

198 host tests, 22,458 native runtime checks and 39 unchanged numeric checks pass
in stock 16 MiB Flycast. The native oracle chains KOS's original TMU0 handler,
requires timer ticks with interrupted SP inside the actual borrowed stack and
matching owner metadata, exercises yield/poison/restore/completion and synchronous
return, and checks root state and canaries. A real second OS thread cannot resume
the suspended image. Read-only main-thread validation adds 98 shared C checks.
Original arming tests compare the complete process reservation, preserve all six
64-bit arguments including aliased input, and verify no implicit identity binding.

Runtime GOAL code is 142,170 bytes, arena 643,584 / 1,048,576 bytes, 693 symbols.
These figures exclude executable/KOS, C contexts, working stacks and rendering.
Existing scalar suspension fixtures still require 312/432-byte saved images.
This is emulator validation, not a physical Dreamcast test or a playability claim.
Grok supplied the isolated read-only main-thread gate and reviewed platform design;
Codex integrated and independently executed the tests. No assets are in this source.
