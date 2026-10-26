# Original root process initialization

2026-10-26. (AI-assisted: Codex; separate human-authorized Grok integration
review remains pending.)

The unchanged original `run-function-in-process`, `throw`, and protect-frame
constructor now execute on native SH-4. A native adapter implements the
assembly catch constructor and throw-dispatch boundary using the previously
verified closed invoke/escape shim. Original GOAL still selects named catches,
walks the process frame chain, runs protect exits, chooses initialize-go versus
initialize-dead, restores old pp, and deactivates completed initializers.

This first adapter supports the root KOS C stack under a registered lexical
process scope. It requires explicit execution identity and an owned process
span; allocation alone supplies neither. The C descriptor captures allocation
generation, PID, native owner, exact stack record and scope prefix. Guest catch
and parameter objects must lie in separate, aligned live P0/P1 stack spans;
normalizing an unsupported virtual alias never authorizes its dereference. The
original 176-byte catch layout is validated; guest saved SP/RA slots are never
used for native execution. Saved native context remains in pinned C storage.

Dispatch matches a guest object against the active C chain before accepting a
context. It checks physical lifetime, process identity, scope prefix, owner,
stack bounds, exact shim return PC/SP and supported saved mode. A successful
throw restores lexical scope depth, unlinks skipped catches and transfers to
the fixed native return site. Normal and fault returns remove expired stack
objects from the process chain while that same physical owner remains valid.
The global C chain is rejected from another KOS owner before traversal.

The native oracle covers normal six-argument results, 64-bit carries/wraps,
throws to initialize, original protect exits, an unmatched inner catch skipped
for the outer tag, and a throw across an extra lexical bracket. A separate
fixed-pool member exercises the original initialize-dead branch: deactivation
clears PID, detaches the active tree link and returns the member to its pool.
Bad spans, aliases, types and entries leave process/object bytes unchanged.
Invalid dispatch, changed PID, mutated guest catch name, context-clear attempts
and physical-retirement attempts retain faults and clean up lexical state.
An actual native fiber confirms unsupported catch entry refuses before writing
its stack reservation and preserves the root register/stack canaries.

The original frontend also exposed an earlier test fixture's protect-frame
size error: its complete raw layout is 16 bytes, including header and exit.
The prior 12-byte declaration happened to occupy 16 padded bytes. The fixture
now uses the correct schema and allocation size. Host tests derive catch and
protect flags and tagged field offsets from original source through the real
frontend, so padding cannot hide this error again.

Build and verify with `build_runtime_goal.py --verify` and the host suite.
447 host tests and 24,793 native runtime checks pass, including 352 new checks.
Generated GOAL/bridge code uses 179,780 bytes; the diagnostic arena uses 908,720
of 1,048,576 bytes with 800 symbols. This includes diagnostic stacks and excludes
KOS, executable, root C stack and renderer memory. The separate numeric binary
matches its prior sealed 39-check execution; it was not rerun for this change.
Original source bodies are unchanged; only the assembly boundaries are native
adapters. Generated modules contain the three original routines and authored
callbacks, not replacement process-initialization logic.

Fiber catch anchors across suspension, working-stack replacement, restart and
abandon remain unfinished. This root adapter rejects running fibers; the lexical
scope blocks native stack transfers. Cross-stack unwinding and KOS stack-record
restoration are not implemented here. Moving heaps/GC, general failure unwinding,
full kernel boot and full gameplay remain unfinished. Verification uses stock
16 MiB Flycast; no physical Dreamcast or disc testing is claimed.
