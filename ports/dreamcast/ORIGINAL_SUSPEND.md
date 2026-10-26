# Original (suspend) on native fibers

2026-10-26. (AI-assisted, Claude Code). The original `(suspend)` macro, which
every original process loop uses to wait for the next frame, now lowers to a
native fiber yield. The unchanged original `looping-code` from
`goal_src/jak1/kernel/gstate.gc` runs on a real native fiber and suspends every
step. The original scheduler that steps processes, GC and gameplay remain
unfinished.

## The original expansion

`suspend` (in `gkernel-h.gc`) expands to:

1. **Debug stack check.** With `KERNEL_DEBUG`, it computes the stack depth from
   x86 `rsp` minus the `r15` memory base, compares it with the top thread's
   `stack-top`/`stack-size`, and calls `format` on overflow.
2. **Suspension.** `pp = (-> pp top-thread)`, then
   `((-> (the cpu-thread pp) suspend-hook) 0)`. The hook saves the thread and
   returns to the kernel.
3. **Resume.** The kernel restores `pp` on resume; the enclosing `rlet` resets
   `pp` at the start of every expansion.

## Lowering

`native_machine_registers` gives the two desktop machine registers their exact
native meaning:

- **`r15`:** the x86 GOAL memory base. Native SH-4 GOAL pointers are absolute,
  so a read-only `r15` is the constant 0. A written `r15` still refuses.
- **`rsp`:** a reset `rsp` reads the native SH-4 stack pointer. Any other `rsp`
  write still refuses.

On a fiber whose working stack is the original thread's stack span, the
original debug check therefore measures real native stack use against the
thread's real bounds.

`suspend_idiom` recognizes the suspension exactly:

- a move into `pp` of a 4-byte load of `top-thread` (offset 44);
- then, before the next non-local operation, a call whose target is a
  single-writer chain from a `suspend-hook` load (offset 12) on a copy of `pp`,
  with the original `(function cpu-thread none)` ABI and a single-writer
  constant-0 argument;
- every value defined in that window is consumed inside it, and no branch lands
  inside it.

The thread-typed write and the hook chain disappear, and the call becomes one
`suspend` operation that calls the new `dc-c-suspend` bridge. Each `pp` reset
becomes `process_reread`, an authenticated `dc-c-current-process` read before
any later use. `pp` therefore always holds the authenticated process, and no
thread value ever reaches it. Near-misses (wrong hook offset, ABI or argument,
an extra `pp` write) keep refusing.

`dc-c-suspend` yields the running fiber of the bound scheduler process. The
engine still refuses with live lexical scopes or a foreign context. Without such
a fiber (a direct C call) the bridge records `DC_GOAL_UNSUPPORTED_TRANSFER` and
the body exits.

## Verification

66 new native checks in stock 16 MiB Flycast run the unchanged original
`looping-code` on a real original pool member. The member has an original main
CPU thread (16 KiB working stack, 1 KiB backup) as its top thread:

- **Direct call:** calling it from C refuses at the first suspend.
- **Four fiber steps:** each step yields at the original suspend with value 0.
  Root integer/FPU registers and canaries stay intact, the phase is SUSPENDED,
  the process status is unchanged and no scope is held.
- **Destroyed working stack:** the whole working stack is overwritten after
  every step, and the next resume restores it from the original thread backup.
- **Image size:** the measured suspended image fits the backup.
- **Kill while suspended:** a different scheduler process kills the suspended
  process with original `deactivate`. The process is dead, its PID cleared, and
  it is back in its pool; the suspended fiber then clears.

All **23,359 native runtime checks**, **39 numeric checks** and **425 host tests**
(1 optional skip) pass. Three original-frontend host tests pin the exported
roles, the rewritten operation kinds and the near-miss refusals. `looping-code`
lowers to 740 bytes with an 80-byte frame. The kernel lowering survey now
reaches gcommon 57/65, gkernel 65/75, gstate 5/6 and gstring 38/38 functions.
See [evidence/original-suspend-verification.json](evidence/original-suspend-verification.json).

## Next gates

`enter-state` (its asm stack reset and jump into state code), native
`throw`/catch, the 8 kernel inline-assembly thread primitives (replaced by the
native fiber engine, not compiled), and the original `kernel-dispatcher` loop
that steps every process each frame.
