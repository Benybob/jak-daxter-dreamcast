# Cooperative native GOAL fibers

2026-10-22. (AI-assisted). This is a bounded bootstrap for suspension and
resumption on SH-4. Original scheduler integration and gameplay remain unfinished.
Grok supplied the isolated assembly proposal and reviewed the engine; Codex
integrated it, corrected boundary guards and independently verified the result.

`dc_goal_fiber_init`, `dc_goal_fiber_step`, `dc_goal_fiber_yield` and
`dc_goal_fiber_clear` manage an explicit C context. A trusted registered
zero-argument scalar entry runs on an owned working stack. Yield returns to the
root C caller; a later step returns its 64-bit argument to the blocked yield.
Completion returns the entry's 64-bit value. Terminal faults cannot be resumed.
Invalid output aliases are rejected without consuming a paused image.

Each context has separate 60-byte root and thread register images. The raw
switch saves R8-R14, PR, SP, FPUL, FPSCR and the bit patterns of FR12-FR15.
The installed SH-4 object measures 102 bytes for the leaf switch, 20 bytes each
for enter/leave shims and 12 bytes for the bootstrap trampoline; alignment adds
two bytes, for 156 bytes of text. The shims push one four-byte return address.
Guards require alignment and that entire push to stay inside the working stack
on leave and outside it on root entry. These guards do not prove body stack
budget or ownership. SR, GBR, MACH, MACL and the other FPR bank are not migrated.

Incoming C code requires enabled FPU and default FPSCR.FR/SZ/PR. Snapshot checks
reject those mode bits before copying or executing FPR transfers. Stored
continuations also require default mode and exact phase-specific shim PCs.
This is a closed set of continuation sites, not permission to restore an
arbitrary register snapshot or branch to an arbitrary PC.

The root copies exactly the live suspended stack interval to the original
CPU thread's backup payload, then restores it before resumption. Multiple
contexts may share one working stack. Every step captures a new root anchor;
no pointer to an expired root call is reused. Original CPU PC/SP/register fields
and process status/main-thread fields are unchanged. The caller explicitly
binds the matching process around each step; allocation binding never supplies
execution identity. The installed core bridge must still belong to this runtime.

Keep the runtime, code, original process/thread spans, working stack and C
metadata pinned and quiescent for the context lifetime. Metadata and output
must be separate valid caller storage outside the runtime object and arena.
All live root frames must be outside the working stack. A still-live outer C
frame can own metadata, but must not return while its contexts remain live.
The working stack must be adequate, at least 4096 bytes, with 16-byte aligned
base and top. Its original backup must fit each suspended live interval.

The engine is cooperative, single OS thread and non-reentrant. Active clear,
re-init and nested step are rejected. Inactive clear or failed re-init discards
control metadata; it does not execute or unwind the body, erase backup bytes,
free its process heap or release body resources. Earlier target effects survive
a fault. GC, migration, exceptions, longjmp and general failure unwinding are
unsupported. Keep existing process/type and implicit-self restrictions.

Verification: 192 host tests, 20,991 native runtime checks and 39 numeric checks
pass in stock 16 MiB Flycast. This milestone adds 385 native checks. The host
lifecycle checker passes 66 checks, including the same 24 word-push boundary
cases used by the native oracle; the host never executes SH-4 continuations.

Two compiled GOAL routines interleave on one 16 KiB working stack. The driver
destroys that stack between every step and alternates root call depth. Each
routine calls unchanged original `remove-exit` against its explicitly bound
original process across yields. Separate C bodies verify distinct GPR/FPR bit
patterns, FPUL/FPSCR and 64-bit yield/resume values. Tests cover malformed
continuation PCs/SP/mode, stale entry/capacity, missing images/core bindings,
output aliases, active misuse, paused clear and insufficient backup capacity.
CPU headers, stack canaries and owner identity remain checked.

Runtime generated GOAL and boxed bridge code uses 135,580 bytes; its main arena
uses 510,896 / 1,048,576 bytes with 669 symbols. These are diagnostic totals,
excluding executable/KOS, stacks and renderer; they are not a full-game budget.
Physical Dreamcast execution remains untested. No discs are prepared.
