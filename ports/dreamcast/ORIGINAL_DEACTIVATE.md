# Unchanged original process deactivation

2026-10-26. (AI-assisted, Claude Code). The unchanged original
`(method deactivate process)` from `goal_src/jak1/kernel/gkernel.gc` now lowers
and executes on native SH-4. It is how every original process dies.
Scheduler-driven execution, a native abandon continuation, `throw`, GC and
gameplay remain unfinished.

## What runs

For a process other than the running one, the whole original body executes:

- next-state is set to `dead-state`, and the entity handler runs when an entity exists;
- every protect-frame and state frame on the process's stack-frame chain gets its
  exit called, newest first, inside the original `pp` bracket
  ([LEXICAL_PP_BRACKETS.md](LEXICAL_PP_BRACKETS.md)), so each exit runs with `pp`
  set to the dying process;
- the `process-disconnect` hook is skipped while it is unbound (original `0` test);
- every child deactivates recursively through original virtual method slot 10;
- original `return-process` (slot 15) returns the process to its pool; state,
  next-state, entity and PID are cleared, and status becomes `dead`.

## The two original control transfers

**Self-deactivation.** The desktop branch for the running process loads
`return-from-thread`, adds the x86 `r15` memory offset and does `.push`/`.ret`.
`return_from_thread_idiom` recognizes exactly that straight-line sequence (no
branch into it, no call inside, the offset used nowhere else) and replaces it with
one explicit `return_from_thread` operation that calls the new
`dc-c-return-from-thread` bridge. No verified native abandon continuation exists
yet, so the bridge records the new `DC_GOAL_UNSUPPORTED_TRANSFER` error and the
body exits instead of guessing an SP/PC. The original cleanup before the transfer
has already run. Any other `r15` use or near-miss keeps refusing.

**Initialization.** The original calls `(throw 'initialize #f)`. `throw` is not
native yet, so the existing call guard faults with `BAD_FUNCTION`, after the
original status write.

## One frontend artifact

`(when entity (entity-deactivate-handler ...))` copies the void handler's
phantom `none` result into the unread `when` value. A move from an unwritten
`none` value into a never-read register is now dropped as dead. A read of such a
destination still fails definite assignment.

## Verification

66 new native checks in stock 16 MiB Flycast run the unchanged original against
a real original three-member dead pool, two original-schema protect frames and
an original `change-parent` child link:

- **Missing import:** the module refuses to load without the transfer import, with rollback.
- **Non-running process:** exit order and `pp` inside each exit; recursive child
  deactivation; pool return; status, PID, state, next-state and entity clearing;
  no children left; `pp`, depth and scheduler restored.
- **Already dead:** returns without callbacks.
- **Initialize:** marks the process dead, then faults on the unbound `throw`
  without leaking a scope.
- **Self-deactivation:** cleanup and pool return complete, then
  `UNSUPPORTED_TRANSFER` is reported.
- **Missing identity:** the body never starts and nothing is written.

All **23,272 native runtime checks**, **39 numeric checks** and **422 host tests**
(1 optional skip) pass. Four new synthetic host tests cover the idiom, its seven
near-misses, the numeric profile and the phantom-copy rule. The original-frontend
test now pins successful lowering and continued refusal of other `r15` uses. The
original lowers to 2,324 bytes with a 72-byte frame. Runtime GOAL code is 148,318
bytes, using an arena of 705,552 / 1,048,576 bytes with 728 symbols. The test
defines `protect-frame` as a real stack-frame subtype when no earlier module has.
See [evidence/original-deactivate-verification.json](evidence/original-deactivate-verification.json).

## Next gates

A verified fiber abandon continuation for `return-from-thread` (the running process
returns to its kernel), and native `throw`/catch for `initialize` and
`run-function-in-process`. Do not remove original branches to compile them.
