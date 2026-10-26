# Kernel continuations and process memory lifetimes

2026-10-26. (AI-assisted, Codex and human-authorized Grok review). This continues
Claude Code's original process-system milestones from public commit
`97c14995b`. The unchanged original dispatcher, trans/post hooks, suspension,
state transitions and deactivation remain exercised by the integrated suite.

A suspended fiber previously checked process layout and its optional main-thread
PID receipt, but did not remember its physical allocation generation. The root
could clear execution, retire an ownership record, and note the same address
again. The old native continuation could then resume in the new lifetime even
when the process bytes and PID had not changed. An independent stock 16 MiB
Flycast reproduction on the preceding ownership baseline failed five checks:
it completed instead of refusing, executed the old continuation, published its
result and restored the old working stack.

Each native fiber now captures its exact process extent and 64-bit generation
when initialized under a configured ownership ledger. Step and main completion
authenticate that witness before reading process fields or restoring a stack.
A newly installed ledger cannot silently accept a fiber created without a
witness. Detaching a ledger or reclaiming the same address also invalidates the
old witness. Normal legacy operation without a ledger retains the explicit
caller-owned reservation contract.

The registered thread-resume and reset-and-call hooks check the same witness
before changing process status, top-thread, scheduler context or borrowing the
fiber's working stack. A stale registration can still be explicitly removed.
No ownership check itself binds execution or activates a process.

Retirement still forgets authority only; it does not free bytes. A caller must
keep physical process, thread and stack buffers pinned until it discards their
C contexts. The generation check rejects an invalidated context before reuse;
it is not a garbage collector or a moving-pool lifetime implementation. Existing
scope checks continue to forbid retirement while a lexical scope/current fiber
borrows a process. A suspended context can be explicitly invalidated from a
quiescent root, but cannot resume under a later generation.

The witness adds 16 bytes to a native fiber descriptor: the measured SH4 size is
336 bytes, with the witness at offset 320. Existing register-image, main-import
and state-restart offsets are preserved. There is no new heap allocation,
per-process scan or reference-count table.

The integrated verification passes **430 host tests**, **23,556 native runtime
checks** and a freshly rebuilt **39-check numeric regression**. The 35 new native
checks include a real yield/reclaim/resume attempt with an unchanged PID,
integer/FP/root-canary checks, an unchanged 16 KiB working stack on refusal, and
complete process/scheduler byte comparisons for stale dispatcher hook calls.
The portable fiber lifecycle oracle also covers installing ownership after
initialization, missing records, detach, generation reuse and retained faults.
The fixture explicitly installs and restores its transfer imports so the
original deactivation tests still exercise missing-import refusal.

A forced rebuild refreshed every native object after the shared descriptor
layout changed. `build_dreamcast.py --force` invokes Make's forced rebuild and
passes the running Python executable to generators, avoiding an unrelated
MinGW Python installation on PATH.

The runtime diagnostic uses 157,734 bytes of GOAL/bridge code and 839,776 bytes
of its 1 MiB arena, including diagnostic working stacks, with 763 symbols.
KOS, the executable/root C stack and renderer are separate. These stock 16 MiB
Flycast checks do not establish a complete game memory budget or hardware test.

The next kernel gates remain original stack objects and native catch/throw for
`run-function-in-process`, deliberate Dreamcast stack configuration, complete
kernel initialization and a native per-frame dispatcher loop. Full gameplay,
moving heaps/GC and general failure unwinding remain unfinished.
