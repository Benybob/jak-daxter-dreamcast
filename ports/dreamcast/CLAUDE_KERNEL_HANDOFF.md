# Claude kernel continuation handoff

2026-10-26. (AI-assisted: Codex; prepared at the human user's request.)

## Verified baseline

Use branch `dreamcast`. The last verified implementation is public commit
`8483b8ba8d8ead292a007283c6b9b4dd3a68ebc1` (private full-checkout counterpart
`8edc8d0e17cdc9421b1c4d8c17bc84dc669e820e`). It passes **447 host tests and
24,793 native runtime checks in stock 16 MiB Flycast**. The separate numeric
binary matches its previous sealed 39-check execution; it was not rerun.

Already complete: Claude's original process-system work was integrated; original
send-event, deactivate, suspend, enter-state, dispatcher/trans/post hooks,
physical fiber lifetime guards, compiler stack objects, raw same-stack catch
return, and original root process initialization are verified. Do not repeat
these milestones. Read [ORIGINAL_INITIALIZATION.md](ORIGINAL_INITIALIZATION.md).
Full kernel startup, fiber catch lifetimes and full gameplay remain unfinished.

## Current blocker and reproducible experiment

The new experiment connects original root initialization to original
`enter-state`, original `set-to-run`, native main-thread import, state execution
and original deactivation/pool return. It currently **fails** and is supplied
as an opt-in [source patch](repros/initialize-state-handoff.patch). The default
verified implementation is unchanged by this documentation commit.

Observed in Flycast: initialization returns true, leaves waiting-to-run status,
runs no state observer early and restores scheduler A's lexical scope. However,
the intended main thread's PC and target register remain zero. Native main
import refuses with `DC_GOAL_BAD_ARGUMENT`.

A native eight-uint64 observing trampoline temporarily replaces the `set-to-run`
symbol value, records all arguments, forwards all eight to the unchanged
original setter, then the test restores the original symbol. The trampoline
is called once, but its first argument is **0x0c133234**, where the real thread
is **0x0c1c9e74** in that build. Addresses are observations, not stable constants.
The observed value may be the `enter-state` code address; this has **not** been
confirmed. Suspect argument ordering or live-range/spill-slot interference in
the original initialization branch. No compiler fix has been established.

Latest diagnostic: 23,527 checks reached, 23,513 passed, first failure ID 13945,
`initialize enter-state passes exact main thread to arming function`, observed
202584628 versus expected 203202164. Later failures/skips may be cascading.
Do not substitute this receipt for the verified baseline. Diagnostic ELF hash:
`b668ac2779c69abbf61309c428dac956852359cb4f43eb9fb9a8fa2f4b399611`.
No diagnostic binary or game assets are included.

The experiment's host suite passes 447 tests. A new assertion verifies the
original state type has protect-frame parent, 36-byte size and nine methods.
Passing host tests do not resolve this native failure.

## Continue from a fresh clone

```text
git switch dreamcast
git apply --check ports/dreamcast/repros/initialize-state-handoff.patch
git apply ports/dreamcast/repros/initialize-state-handoff.patch
```

On the shared development PC the four modified source files are already
present; inspect `git status` and do not apply the patch twice. Source files are
under `ports/dreamcast`; local toolchains and generated modules are under `work/`.
Use the existing local SDK/frontend/Flycast setup instead of reinstalling them.

Build the experiment with the documented `build_runtime_goal.py --verify`
workflow. The patch intentionally preserves the failure as found: its builder
still expects 24,834 checks, while the complete success path with the added
header/argument diagnostics would run **24,843**. The failed bailout reaches
fewer checks. Correct the expected total only after fixing and validating the
actual behavior; never weaken the PC, thread, ownership or import checks.

Inspect the original `enter-state` typed IR around the first `set-to-run` call,
its read operands and parameter ABI, definitions, control flow and coalesced
spill slots. Compare baseline versus optimized lowering. Check whether the
observed first argument is a function pointer and identify precisely where the
thread argument changes. Preserve original GOAL bodies. Add a minimal native
regression and rerun the complete relevant host/runtime checks before publishing
a fix. The diagnostic bailout label is `state_handoff_done`.

## Collaboration and operating constraints

A human-authorized isolated Grok compiler diagnosis is running locally in
`work/grok-state-argument-diagnosis`; its only deliverables are `DIAGNOSIS.md`
and `FIX.patch`. At handoff both were placeholders. Check its assignment and
actual process before relying on it; a polling timeout is not completion.
An older raw-catch review completed in `work/grok-native-catch-review`; its final
`REVIEW.md` and `INTEGRATION_PLAN.md` have not been adjudicated. Review proposals
against current source and verify independently; do not blindly apply them.
No private model streams, credentials or unrelated sessions are published.

One SH-4 build at a time. Never relink the Windows frontend while compiler
clients run. Use stock 16 MiB Flycast. Do not request burns, prepare discs or
claim physical hardware validation. Publish verified source to this repository's
`dreamcast` branch, never upstream OpenGOAL. Keep ISO/BIOS, extracted/converted
assets, embedding binaries, private logs and archives out of GitHub. Follow
AGENTS.md: disclose AI assistance, no issues/PRs or unsolicited outreach.
