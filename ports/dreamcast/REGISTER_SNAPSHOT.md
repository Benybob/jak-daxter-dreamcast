# Same-stack SH-4 register snapshot (AI-assisted)

A native leaf routine now copies selected SH-4 registers into a separate,
caller-owned 60-byte image. Grok supplied the isolated header/assembly proposal
and reviewed its guard/test contract; Codex independently compiled, linked,
disassembled and exercised it with a separate assembly/C oracle in Flycast.

This is an observation of the current call. It does not restore registers,
resume execution, switch stacks, activate a process or modify original GOAL
CPU-thread fields. The original CPU-thread layout remains unchanged.

## API and preconditions

`goal_register_snapshot.h` declares:

```c
uint32_t dc_goal_native_regs_snapshot(struct dc_goal_native_regs *out);
```

The caller provides 60 writable, unmoved bytes aligned to four, separate from
any stack bytes whose preservation it is checking. Pointer length, mapping,
ownership and lifetime are caller preconditions. The routine does not validate
a guest object, process reservation or executable address. The FPU must already
be enabled (SR.FD clear), including when a nondefault FPSCR mode is rejected.

A null or misaligned pointer returns zero before any destination store. With a
valid pointer, the routine checks FPSCR.FR/SZ/PR (mask `0x00380000`) before any
store or FPR transfer. Any nonzero combination returns zero without writing the
image. Other FPSCR flags, DN and rounding bits are copied and preserved exactly.
An invalid mapped-span precondition may fault; this is not a memory-fault handler.

Success returns one after copying these raw words:

| Fields | Byte offsets |
| --- | --- |
| R8 through R14 | 0 through 24 |
| Entry PR / entry SP | 28 / 32 |
| FPUL / full FPSCR | 36 / 40 |
| FR12 through FR15 | 44 through 56 |

Floating registers are transferred as bits, without arithmetic or conversion.
The mode gate ensures single-word transfers from the expected FPU bank. The
routine never writes SP, PR, FPUL, FPSCR, the captured GPRs or any FPR. Scratch
registers are R0, R1 and condition bit T. R4 is also preserved by this implementation.
It makes no allocation or process-binding changes.

Entry PR is the return site of the JSR that called the snapshot, not the
wrapper's return-to-C PR saved before that JSR. Entry SP is sampled after the
test wrapper's own frame has been established. Neither value is a validated
resume target. BT/BF guards are non-delayed branches; their following NOPs are
harmless fall-through instructions. RTS has a delay slot that sets the result.

GNU tools produce 80 bytes for the leaf, consisting of forty 16-bit SH-4
instructions with no relocation or literal pool. This records code density,
not a speed comparison or a performance measurement. The independent oracle
and C fixtures add their own diagnostic code and stack usage.

## Independent native verification

The assembly oracle saves the C caller's state, installs known R8..R14, FPUL and
FPR bit patterns, records the exact snapshot callsite and SP, then calls the
routine. It records the state afterward in a different layout, resets FPSCR
before reading the default FPU bank, and restores the C caller before returning.
The C caller also keeps its own integer/FPR/status and stack canaries live.
Expected image words come from separate C patterns and measured callsite/SP;
the oracle does not invoke the snapshot a second time to define expectations.

3,034 new native checks cover 108 scenarios: four independent bit-pattern sets,
four accepted FPSCR settings, all seven rejected FR/SZ/PR combinations, null
and each misalignment, exact output words, both destination and observation
canaries, unchanged complete buffers on rejection, full FPSCR/FPUL preservation,
caller GPR/FPR preservation, and SR preservation except T. Float patterns include
signed zero, infinities, quiet/signaling NaN payloads, normal and subnormal bits.
No floating arithmetic is used to establish these expectations.

Two new host tests compile the layout/type contract as C99 and C++11, then use
actual GNU target tools to link the assembly definition against a real C call.
All 189 host tests, 19,847 native runtime checks and 39 numeric checks pass in
stock 16 MiB Flycast. Diagnostic GOAL code remains 134,526 bytes; main arena
438,592 / 1,048,576 bytes, 650 symbols. Native static code, executable/KOS,
stacks/renderer and the separate shared-C fixture runtime are outside that arena
metric. No whole-game memory fit or physical Dreamcast validation is claimed.

## Remaining work

A snapshot is not a resumable context. A future cooperative save/restore must
define ownership, validation, FPSCR policy, exact continuation/caller semantics,
code allowlists, stack bounds/alignment, activation and failure cleanup, and must
pass independent native roundtrip and stack-transition tests. KOS interrupt
contexts and `jmp_buf` are different representations; see CONTEXT_ABI_AUDIT.md.
Full platform startup, GC, failure unwinding and gameplay remain unfinished.
