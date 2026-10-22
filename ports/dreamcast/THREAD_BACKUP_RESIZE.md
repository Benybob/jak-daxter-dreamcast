# Original main-thread backup sizing

2026-10-22. (AI-assisted). The unchanged original `stack-size-set!` method now runs
through `dc_goal_resize_main_backup`. It changes the main CPU thread's backup
capacity and process heap cursor, enabling an originally activated main thread
to run the tested native argument/yield slice. Original scheduling, set-to-run,
deactivation, GC and gameplay remain unfinished.

Grok supplied an isolated read-only preflight. Independent testing caught its
incorrect zero-valued false marker; the integrated version compares the previous
thread link with GOAL `#f`, the runtime's `s7` symbol pointer. Integer zero remains
truthy. The original proposal is preserved separately. Codex integrated the
corrected checker, native original-call wrapper and independent acceptance tests.

Preflight uses the existing owned process/CPU/stack validator, then requires an
actual main/top CPU thread, false previous link, zero original PC and initial SP
at stack top. Working stack base/size and old/new backup sizes are aligned; the
working span is at least 4096 bytes and new capacity fits it. The working stack
must exclude the entire process reservation, including free heap. Changed size
requires the heap cursor immediately after the existing CPU header/backup and a
checked new end strictly below heap top. Zero capacity is allowed. A same-size
no-op may follow later heap allocation, matching the original branch order.

The native wrapper rejects active fibers, requires the same active core runtime
and the canonical registered original export, and uses its two-word **void** C
ABI. It executes the original stores; the checker never changes process, thread
or backup bytes. No payload is cleared. Original error/format branches are kept
unreachable by preflight; complete diagnostic/failure unwinding is unsupported.
The original method is not automatically installed as an unguarded slot-9 call.
CPU construction and activation entry guards also now require the owning export
symbol, rejecting registered foreign aliases before entering another API.

Keep all spans and code pinned and quiescent. Release native contexts before
resizing and reopen borrowed image views afterward. The original PC/SP header
checks cannot prove that no private fiber exists: that engine preserves the
original header. Tests deliberately change capacity after creating a NEW context
to verify that stale context faults before entry. This does not permit resizing
live continuations. Body effects are not rolled back, and this helper does not
infer or change allocation/execution binding.

Verification: 196 host tests, 22,105 native runtime checks and 39 numeric checks
pass in stock 16 MiB Flycast. This milestone adds 256 native checks: 118 shared
read-only preflight cases, 126 original-call/startup cases and 12 constructor/
activation export-identity checks. Host preflight separately passes 118 checks.

Native tests exercise original grow/shrink/no-op calls, strict fit and headroom,
later allocations, missing/foreign entries, stale images and contexts, full owner
byte preservation and stack fences. A same-ABI foreign callback remains uncalled.
During a live fiber an otherwise-valid request passes read-only geometry, then
the actual wrapper rejects it before the original stores. Original activation
creates a ready main CPU with its default 256-byte backup; the guarded original
setter grows it to 8192 for the diagnostic, then a six-argument GOAL body yields,
survives a destroyed working stack and completes under explicit process binding.
Root GPR/FPR/control state and original CPU header remain checked. Original
`ready` status stays unchanged; the C driver supplies these diagnostic steps.

The original method occupies 1512 bytes, uses a 64-byte frame and retains two
static diagnostic strings totaling 184 bytes. Diagnostic GOAL/boxed code totals
141,438 bytes; main arena use is 592,912 / 1,048,576 bytes with 684 symbols.
These exclude executable/KOS, C metadata/stacks and renderer. The 8192-byte test
backup is not a measured minimum or a full-game stack budget. Physical Dreamcast
validation remains unfinished; no discs are prepared or burned.
