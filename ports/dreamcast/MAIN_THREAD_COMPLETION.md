# Imported main-thread completion receipts (AI-assisted)

The C driver can now verify completion of its exact imported original main
thread and release its C fiber metadata. Import records the pinned C object
address and nonzero original process PID. Resume rejects copied, partial and
stale receipts before restoring either working stack or backup bytes.

Completion requires DONE, no live root or saved image, matching six scalar
arguments, current PID, canonical armed original thread header, registered
code, disjoint protected spans and the original KOS thread/stack owner.
Existing runtime errors are retained. Release clears only C metadata.

Original GOAL thread headers remain armed. The driver must not reimport/replay
them. This is not deactivation, process-pool return, exit-hook execution, GC or
failure unwinding. Native callers own and must pin/quiesce these objects; this
receipt is not a security boundary against native memory forgery.

Verification: 199 host tests, including a real C fixture with 318 checks;
22,649 runtime checks and 39 numeric checks on stock 16 MiB Flycast. The native
fixture genuinely arms, imports, yields, restores a poisoned stack and
completes. A separate original activation changes PID after suspension and
must reject resume without destination/backup writes. Foreign KOS-thread,
copied receipt, header corruption and retained-error cases are exercised.
The host fixture stubs bridge liveness and never executes SH-4 instructions.

Grok's isolated earlier audit informed the design. Its final review attempt
reached its turn limit without findings or a completed final review; it is not
counted as independent approval. Codex verified the integrated implementation.
