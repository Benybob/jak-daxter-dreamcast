# Compiled original lexical pp brackets and send-event-function

2026-10-26. (AI-assisted, Claude Code). The unchanged original
`send-event-function` from `goal_src/jak1/kernel/gstate.gc` now lowers and
executes natively on SH-4. This is the dispatcher behind every original
`send-event`. Its event hook runs with the target process selected as `pp`, and
the caller's `pp` comes back afterwards. Full `deactivate`, the scheduler, GC and
gameplay remain unfinished.

## The original pattern

```lisp
(with-pp
  (when (and (valid? proc process #f #f 0) (nonzero? (-> proc pid)) (-> proc event-hook))
    (let ((pp-backup pp))
      (set! pp (the process proc))
      (let ((result ((-> proc event-hook) (-> msg from) (-> msg num-params) (-> msg message) msg)))
        (set! pp pp-backup)
        result))))
```

Before this change, any write to the `r13` process register was exported as
role `unsupported`, so the compiler refused the function.

## Export

`host_compiler/export_ir.cpp` now tags a process-typed `rlet` `pp` that is both
read and written as role `process_lexical`. Read-only `pp` keeps
`process_context_read`; other roles stay `unsupported`. The exporter makes no
structural claim; the lowerer proves the structure or refuses.

## Lowering proof

`lexical_process_bracket` in `lower_sh4_ir.py` accepts exactly one bracket:

- an optional entry reset with no earlier process use or control transfer
  (it becomes the authenticated `dc-c-current-process` entry definition);
- one saved copy `S = pp`, written exactly once, before the assignment;
- one plain-move assignment `pp = X` (scope **enter**), and one restore
  `pp = S` (scope **leave**);
- no `return` between them, no branch from inside to outside, and no branch
  from outside to inside, so every normal path that enters also leaves.

Additional writes, a restore from anything other than the saved entry copy, an
argument alias, non-process containers, combined read-only/lexical roles and
forged scope operations all refuse with explicit errors.

## Native operations

Enter and leave call two new guarded C bridges installed with
`dc_goal_install_scope_bridge`: `dc-c-enter-process-scope` (known-span lookup,
then authenticated lexical entry) and `dc-c-leave-process-scope` (must match the
saved process). A zero result exits through the existing process-entry failure
path before any further body access. The scheduler's current process is never
changed.

A dedicated frame slot, outside liveness slot reuse, records an entered scope.
If a callee inside the bracket is missing (zero or `#f`), the call-fault exit
first restores the saved process, then reports `BAD_FUNCTION`, so a fault does
not leak the scope. A refused restore cannot be repaired locally. It leaves the
scope live for the existing fiber native-return hook, which faults the fiber.

## Verification

61 new native checks run the unchanged original in stock 16 MiB Flycast. They cover:

- **Missing imports:** the module refuses to load without the scope bridges, with
  full arena/symbol rollback.
- **Normal dispatch:** the hook runs with `pp` equal to the target process while
  scheduler current-process stays the caller, with exactly one live scope. It gets
  the exact from/num-params/message/block arguments, `pp` and depth return to the
  caller afterwards, and the target process bytes are unchanged.
- **Nested dispatch:** a hook that sends again sees two live scopes and the inner
  `pp`, with `pp` restored at each level.
- **Original filters:** a zero PID, a `#f` hook and a non-process target (original
  `valid?`) each return `#f` with no hook call and no scope.
- **Refused selection:** a copied but unowned process is refused with
  `BAD_ARGUMENT` before its hook runs.
- **Call-fault restore:** a zero hook takes the call-fault exit, reports
  `BAD_FUNCTION` and restores the scope.
- **Missing identity:** with no execution identity, the original body never starts.

All **23,206 native runtime checks** (previously 23,145), the separate **39
numeric checks**, and **418 host tests** (1 optional skip) pass with the full
local SDK/frontend setup. Eight synthetic host tests cover the bracket proof and
its refusals; two original-frontend tests pin the export and lowering. The
original lowers to 1,128 bytes with a 96-byte frame. Runtime GOAL code is 145,958
bytes, using an arena of 701,248 / 1,048,576 bytes with 716 symbols. These are
emulator diagnostics, not hardware performance or whole-game memory claims. See
[evidence/lexical-pp-bracket-verification.json](evidence/lexical-pp-bracket-verification.json).

## Reproduce

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

On Windows, keep the workspace in a short path such as `C:\dcw`. Under a deep
profile path, CMake object paths and IR outputs exceed the 260-character limit.

## Remaining limits

One bracket per function. Nested brackets in one function, returns or `go`
inside a bracket, and `throw`/abandon continuations are unsupported.
`run-function-in-process` still refuses (an uninitialized value across its
catch/throw path), and `deactivate` still refuses on its `r15` inline-assembly
return jump. Do not remove original branches to compile them.
