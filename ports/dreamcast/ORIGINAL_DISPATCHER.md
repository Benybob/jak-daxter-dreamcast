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

## Trans hooks: native reset-and-call

For a process with a trans hook, the original lambda builds a temporary `trans`
thread with the unchanged original `cpu-thread` constructor. That constructor
allocates nothing: it places the 128-byte thread inside the main stack at
`stack-top - PROCESS_STACK_SIZE`. The lambda then calls `reset-and-call` and the
original `delete`, which restores the previous top thread.

`reset-and-call` is x86/ARM64 assembly, so the new `dc-c-reset-and-call` bridge
(bound as the `reset-and-call` value) takes its place. It follows the original:
`top-thread` becomes the thread, status becomes `running`, and the function runs
on the thread's `stack-top` stack. Natively it requires the thread's process to
be the dispatcher's current process with a registered main fiber that is
suspended or new; that fiber's working stack is free because its image is in the
thread backup. It also requires the thread's `stack-top` to be that stack's top.
The function then runs through the existing validated `dc_goal_call_on_stack`,
using only the span above the temporary thread's structure, under the same
ownership-checked binding as `thread-resume`. Anything else records
`UNSUPPORTED_TRANSFER`.

`PROCESS_STACK_SIZE` is still the desktop 24 KiB (the PS2 value is 7 KiB). It is
one of the audited `PC_PORT` values, so the main stack must exceed it; the test
uses 32 KiB.

Post hooks use the same bridge. The original post thread's `stack-top` is
`*kernel-dram-stack*` (`*dram-stack*` + 32 KiB). The caller registers that owned
span with `dc_goal_register_kernel_stack`, and `reset-and-call` accepts a thread
whose `stack-top` is the top of the main fiber stack or of the registered DRAM
stack. Nothing else is accepted.

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

Duplicate registration refuses.

On frames 1 and 3 an original trans hook is installed (22 more checks). It runs
exactly then:
- `pp` is the process;
- the original temporary `trans` thread (type `cpu-thread`, name `trans`) is
  `top-thread`;
- status is `running`;
- SP is on the main stack above the thread's structure.

Afterwards the main thread is `top-thread` again and the process resumes and
suspends normally. Frames 0 and 2 run no hook.

On frames 2 and 3 an original post hook is installed (14 more checks), so frame 3
runs trans and post together. It runs exactly then, after the main code:
- `pp` is the process;
- the original temporary `post` thread on the DRAM stack is `top-thread`;
- SP is on the DRAM stack above that thread's structure.

All **23,521 native runtime checks**, **39 numeric checks** and **430 host tests**
(1 optional skip) pass, from a clean object rebuild. Two new host tests pin that the context removes the profiler calls and
that the resume is a dynamic call through `resume-hook` (+16). Runtime GOAL code
is 157,294 bytes, using an arena of 789,840 / 1,048,576 bytes with 759 symbols.
These are emulator diagnostics, not a hardware or whole-game budget.

## Next gates

A Dreamcast `PROCESS_STACK_SIZE` choice,
throw/catch for process initialization (`run-function-in-process`), and driving
the dispatcher from the native main loop each frame.
