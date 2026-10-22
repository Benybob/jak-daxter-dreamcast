# SH-4 context ABI observations (AI-assisted)

The installed GCC 15.1.0 / KOS 2.2.1 configuration has now been measured with
compiler-generated assembly and SDK layout constants. This audit prepares a
future cooperative context save; it does not execute a new thread or change
the existing runtime. Actual register roundtrips and stack transitions remain
unverified.

Run from an environment with the configured SH-4 compiler and KOS headers:

```text
python ports/dreamcast/audit_context_abi.py --cc sh-elf-gcc --output NEW_DIRECTORY
```

The output directory must not exist. The script emits probe sources, assembly,
compiler settings, 24 explicit preservation checks and a hashed observations
report. It does not link, launch Flycast, allocate a live context or switch SP.
The checked compiler flags match this workspace's KOS target flags relevant to
code generation; generated settings are recorded rather than inferred from the
presence of a command-line option.

## Observed compiler behavior

All 24 assembly checks pass: the clobber declarations cause GCC to save and
restore R8 through R14, FR12 through FR15, and PR. Special-register clobbers
FPUL/MACH/MACL do not produce saves in the special probe. This measures ordinary
function-call preservation for these flags. It is not an asynchronous interrupt
save contract and does not prove the behavior of a future handwritten switch.

The unsigned 64-bit forwarding probe passes its first argument in R4:R5, second
in R6:R7 and third on the stack, low word first. Four physical integer argument
registers therefore carry two of these GOAL 64-bit values. The float forwarding
probe uses the existing backend order FR5,FR4,FR7,FR6,FR9,FR8,FR11,FR10, with the
ninth float on the stack. It agrees with the backend's current adaptation.

The actual target report has `-m4-single` enabled and `-mdalign` disabled. GCC's
[SH target definitions](https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.1.0/gcc/config/sh/sh.h)
define stack alignment as 32 bits under that setting. The forwarding assembly
pushes PR and reserves eight bytes before its 64-bit call: an eight-byte-aligned
entry would be four bytes off that alignment at this call. Do not assume an
eight-byte ABI stack boundary from the use of 64-bit GOAL slots. A future switch
may require stronger private alignment, but it must establish and document it.

The [SH options documentation](https://gcc.gnu.org/onlinedocs/gcc/SH-Options.html)
explains that `-m4-single` assumes single-precision FPU mode by default, while
`-mdalign` changes calling conventions. Changing these flags is a separate ABI
change, not an alignment fix to apply silently.

An initial separate diagnostic that declared `fpscr` in an inline-asm clobber
list was rejected by this installed compiler as an unknown register name. That
failed diagnostic is not used by the passing audit script. It does not prove
that FPSCR is preserved or define how a switch should save it. Handwritten
assembly and measured FPSCR/FPUL roundtrips are still needed for that contract.

## Installed SDK layout

The layout probe compiles constants from installed `arch/irq.h` and `setjmp.h`:

| Item | Observed bytes/offset |
| --- | ---: |
| `sizeof(irq_context_t)` | 228 |
| `__alignof__(irq_context_t)` | 32 |
| `sizeof(jmp_buf)` | 80 |
| PC / PR | 0 / 4 |
| FPUL | 28 |
| Primary FR bank | 32 |
| Secondary FR bank | 96 |
| R0..R15 | 160 |
| FPSCR | 224 |

KOS separately reserves 256 bytes via `REG_BYTE_CNT`; use that explicit storage
contract when discussing a KOS interrupt context. Do not infer an array stride
or rounded size solely from the alignment of its typedef. The matching sources
are [KOS irq.h](https://github.com/KallistiOS/KallistiOS/blob/b125c930da1a7969c5b9760da98254c666b3eeba/kernel/arch/dreamcast/include/arch/irq.h)
and [interrupt entry assembly](https://github.com/KallistiOS/KallistiOS/blob/b125c930da1a7969c5b9760da98254c666b3eeba/kernel/arch/dreamcast/kernel/entry.s).
The latter saves both FPU banks and additional machine state around interrupts;
ordinary C call-preservation observations do not replace that mechanism.

The original CPU-thread has 56 register bytes and 32 floating-register bytes
between object offsets36 and124, totaling88. A full KOS interrupt context does
not fit there. The 80-byte `jmp_buf` measurement is not permission to overwrite
those original fields: its semantics/layout differ, and this audit has not
validated lifetime, restored stack memory or nonlocal error behavior. A future
cooperative context needs an explicit native representation and ownership policy.

## Remaining gate and preserved verification

Before execution switching: define native saved-state fields and FPSCR policy,
validate a code entry against an allowlist, prove stack bounds/alignment and
trampoline return registers, test bit-exact register/FPU roundtrips and actual
stack transition in Flycast, then establish process activation/failure cleanup.
Preserve fail-closed unsupported pp roles and separate allocation/execution
binding. Avoid borrowing KOS interrupt context switching as an unreviewed shortcut.

The runtime source/binaries are unchanged by this audit. The previous verified
187 host tests, 16,813 runtime checks and39 numeric checks remain the checkpoint
baseline; they are not evidence of context switching. Full kernel startup, GC,
failure unwinding and gameplay remain unfinished. No physical hardware validation.
