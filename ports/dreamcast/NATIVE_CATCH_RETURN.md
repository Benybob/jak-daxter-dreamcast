# Native catch return primitive

2026-10-26. (AI-assisted: Codex implementation; human-authorized Grok review
requested separately.)

The SH-4 catch dependency now has a private same-stack invoke/escape primitive.
Invoke calls an exact native C callback and returns once to its C caller. A
normal return uses the callback's result; an escape uses a separate 64-bit
result slot. Zero is a valid result. The saved continuation is the fixed invoke
wrapper label, with its caller's PR word on the stack. No original GOAL saved
RA/SP field is executed.

Both exits restore R8 through R14, FPUL, full FPSCR and raw FR12 through FR15.
Restoration clears FPSCR mode bits before floating-point loads, so an escape
can recover after a callback changes FR/SZ/PR. There is no generic C function
that returns twice, no dynamic stack allocation and no KOS stack-record change.

This is an internal trusted-C primitive, like the existing raw fiber shims.
It does not validate arbitrary pointers or authenticate a GOAL catch frame.
Its header documents pinned storage, exact callback ABI, supported native
entry mode and same live KOS owner/stack requirements. Yield, restart, abandon,
cross-stack transfer, interrupt entry and cancellation across the scope are
not supported. It is deliberately not installed as a guest-callable import.

The independent native oracle runs 80 combinations: four full FPSCR patterns,
four 64-bit result patterns and five paths. Paths include normal return, deep
C-frame escape, nested normal calls, escaping past an inner invocation to the
outer one, and escaping to the inner invocation then continuing the outer
callback. The assembly escape probe deliberately clobbers every saved GPR and
FPR, FPUL, all three FPSCR mode bits, and reserves extra stack space before
escaping. GCC caller canaries, exact original/captured SP, the fixed saved PC,
frame boundary/padding and distinct normal/escape result sources are checked.
These add 802 native checks to the existing runtime suite, all passing in
stock 16 MiB Flycast. C99 and C++11 callers are independently compiled and
relocatable-linked against the real assembly symbols using the installed SDK.

Next is a guarded runtime adapter for the original catch-frame constructor and
throw-dispatch. It must authenticate process allocation generation, PID, native
owner and known stack spans before reading context metadata; restore lexical
scope depth; and manage catch anchors through fiber suspension, restart,
abandon and faults. Cross-stack unwinding also needs correct KOS stack records.
The original `throw` algorithm and protect callbacks must remain intact.
The original `run-function-in-process` compiles but has not executed natively.
Full kernel startup and full gameplay remain unfinished.
