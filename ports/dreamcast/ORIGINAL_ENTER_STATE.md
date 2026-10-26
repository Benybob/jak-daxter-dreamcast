# Original enter-state on native fibers

2026-10-26. (AI-assisted, Claude Code). The unchanged original `enter-state` from
`goal_src/jak1/kernel/gstate.gc`, the transition behind every original `go`, now
lowers and executes on native SH-4. A process on a native fiber changes state and
runs the new state's code on its reset main-thread stack. With this, all six
gstate functions lower. The original scheduler, state `go` macros in game code,
GC and gameplay remain unfinished.

## What the original does

In the running process's main thread, `enter-state`:

1. installs `next-state` as `state`;
2. runs every protect/state frame exit;
3. installs the state's event, post and trans hooks, and its exit frame;
4. calls `enter` with six arguments, then `trans`;
5. finally jumps in x86 assembly. It loads four arguments into `rdi/rsi/rdx/rcx`,
   resets `rsp` to `(-> pp main-thread stack-top)`, pushes
   `return-from-thread-dead` and jumps to `(-> new-state code)`. When the code
   returns, the process deactivates.

The other branches are an `initialize` throw, a deferred `set-to-run` when
another process is current, and a non-main-thread `return-from-thread`.

## Lowering

- **`normalize_register_asm`:** the explicit x86 `(.mov a b)` and `(.add a b)`
  forms between general registers are exactly a 64-bit copy and add, so they are
  read as `move`/`add64`. Every other asm op stays unsupported. This also lets the
  existing `return-from-thread` idiom match the `.mov`-spelled non-main-thread
  branch.
- **`state_code_idiom`:** proves the exact straight-line jump window:
  - the four carg moves;
  - `code = (-> state code) + r15` (offset 12);
  - `rsp = (-> (-> pp main-thread) stack-top) + r15` (offsets 40/28);
  - the `return-from-thread-dead` trampoline plus `r15`;
  - `.push`, then `.jr`, then the two dead keep-alive adds.

  No branch may land inside, and no call or unrelated work may sit inside. It
  becomes one `state_code_transfer` reading the code and four arguments. The
  machine-register bindings must have no other use.
- **Read-only `pp`:** a `process_lexical` role with only resets (as in
  `enter-state`) is treated as an authenticated read-only context.

`state_code_transfer` calls the new `dc-c-enter-state-code` bridge with the five
values. Any return means refusal and exits the body.

## Native continuation

On the bound scheduler process's running fiber, the bridge calls the new
`dc_goal_fiber_restart`. It copies the arguments (they may live on the stack being
discarded) and records the new entry and a return-through-deactivate flag. It
then leaves to the root through the existing closed continuation. The root sees
the pending restart and re-enters a fresh bootstrap through the same entry site,
with SP at the stack top. Like the original, the old stack is discarded, not
unwound. When the new entry returns, the bootstrap runs the process type's
original `deactivate` (method slot 10). As a self-deactivation, that abandons to
the root and completes the fiber. Live lexical scopes refuse the restart. Without
a matching fiber, the bridge records `DC_GOAL_UNSUPPORTED_TRANSFER`.

## Verification

63 new native checks in stock 16 MiB Flycast. A fiber body on an original pool
member calls the unchanged original `enter-state` with six distinct arguments:

- **Direct call from C:** refuses at the code transfer.
- **The original sequence:** it calls the state enter function once with all six
  arguments, then trans, and installs the event, trans and post hooks. Observed
  inside the code, the new state is installed and no frame is pushed for a state
  without exit.
- **The code on the reset stack:** it runs once with the four original register
  arguments and `pp` set to the process. Its SP is within 512 bytes of the main
  thread stack top and above the abandoned `enter-state` frames.
- **Completion:** the body never resumes. When the code returns, the process is
  deactivated (state cleared, dead, PID 0, back in its pool), and the fiber is
  DONE with root registers, FPU state and canaries intact. No scope leaks.
- **Restart outside a fiber** refuses.

All **23,422 native runtime checks**, **39 numeric checks** and **426 host tests**
(1 optional skip) pass, from a clean object rebuild. The native context grows to
320 bytes. The bootstrap frame grows, so the measured suspension image budgets
are now 328/448 bytes (previously 312/432), still within the 512-byte backups.
`enter-state` lowers to 3,672 bytes with a 120-byte frame. Runtime GOAL code is
152,802 bytes, using an arena of 765,760 / 1,048,576 bytes with 742 symbols.

Build note: `runtime_process_spans_cases.o` lacked a `goal_fiber.h` dependency,
so a stale object briefly used the old context layout; the rule is fixed.

## Next gates

Native `throw`/catch (`initialize` paths, `run-function-in-process`), the original
`kernel-dispatcher` that steps processes each frame, and game-side `go`/`defstate`
code.
