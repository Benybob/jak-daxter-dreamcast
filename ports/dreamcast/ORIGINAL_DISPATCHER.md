# Original kernel-dispatcher scheduling native fibers

2026-10-26. (AI-assisted, Claude Code). The unchanged original `kernel-dispatcher`,
the per-frame scheduler that runs every process, now executes natively. The
unchanged original process-tree walk and per-process lambda resume a process's
main thread through its original `resume-hook`, which now steps that process's
native fiber. The trans/post temporary threads (`reset-and-call`), the listener,
throw/catch, GC and gameplay remain unfinished.

## Explicit Dreamcast target configuration

Under desktop configuration the dispatcher calls the C++ frame profiler
(`pc-prof`) for every process, because gcommon sets `PC_PROFILER_ENABLE` to `#t`.
The native runtime has no such profiler. `dreamcast_profiler_context.gc`,
preloaded after gcommon, redefines it to `#f`, so the original `profiler-*-event`
and `with-profiler` macros expand to nothing. No original body changes. This is
the first deliberate choice among the six configuration roots listed in
[TARGET_CONFIG_AUDIT.md](TARGET_CONFIG_AUDIT.md). With it, `kernel-dispatcher`
(1,188 bytes) and its lambda (3,220 bytes) lower with no profiler calls.

## Native original thread hooks

The original `cpu-thread` constructor installs `thread-suspend`/`thread-resume`
as the thread's hooks. Both are x86/ARM64 assembly, so natively:

- **Suspend:** `dc-c-suspend` (see [ORIGINAL_SUSPEND.md](ORIGINAL_SUSPEND.md)) now
  also marks the process `suspended`, as original `thread-suspend` does.
- **Resume:** the new `dc-c-thread-resume` is the native resume-hook. It looks up
  the fiber registered for that thread with `dc_goal_register_thread_fiber`.
  That is caller-owned, with at most eight registrations; duplicates refuse. It
  requires the dispatcher's current-process to be that fiber's process and no
  execution binding in force. It then follows the original `thread-resume`:
  temporarily clears current-process so the explicit, ownership-checked binding
  can be made, sets `top-thread` and status `running`, steps the fiber, clears
  the binding, and restores the dispatcher's current-process for the original's
  own cleanup. Without a registered fiber it records `UNSUPPORTED_TRANSFER`.

## Verification

63 new native checks in stock 16 MiB Flycast build an original active tree
holding an original pool member. The member's original main CPU thread runs
unchanged original `looping-code` on a registered native fiber, and the original
`run-logic?` is installed.

- **Four dispatcher frames:** each frame resumes the process, which suspends at
  its original `suspend`. Each time the status is `suspended`, the fiber is
  SUSPENDED, `top-thread` is the main thread, the dispatcher has cleared
  current-process and no scope survives. The working stack is destroyed between
  every frame.
- **A `ready` process** (not waiting or suspended) is not resumed.
- **Unregistered fiber:** the resume refuses explicitly and leaves the fiber
  untouched.
- **Dead process:** after original `deactivate` kills the process, the next
  frame skips it and never resumes its fiber.

Duplicate registration refuses. All **23,485 native runtime checks**, **39 numeric
checks** and **428 host tests** (1 optional skip) pass, from a clean object
rebuild. Two new host tests pin that the context removes the profiler calls and
that the resume is a dynamic call through `resume-hook` (+16). Runtime GOAL code
is 157,294 bytes, using an arena of 789,840 / 1,048,576 bytes with 759 symbols.
These are emulator diagnostics, not a hardware or whole-game budget.

## Next gates

Native `reset-and-call` for trans/post hooks (temporary threads on the main and
DRAM stacks), throw/catch for process initialization (`run-function-in-process`),
and driving the dispatcher from the native main loop each frame.
