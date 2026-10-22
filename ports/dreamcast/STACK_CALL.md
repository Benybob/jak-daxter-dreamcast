# Synchronous calls on a separate owned stack (AI-assisted)

The port can now call a trusted registered zero-argument scalar function on an
explicit caller-owned stack, then return to the original C stack. The unchanged
original `remove-exit` behavior passes this path after deliberate process-context
binding. Grok supplied an isolated assembly proposal; Codex supplied C validation
and independent host/native acceptance tests.

This is a synchronous native call, not suspension, saved-continuation restoration
or process scheduling. Original CPU-thread PC/SP/register fields, main-thread
binding and process status are not rewritten by this primitive. Allocation never
implies execution identity.

## Validated API

```c
int dc_goal_call_on_stack(dc_goal_runtime *rt, uint32_t entry,
                         uint32_t stack_base, uint32_t stack_bytes,
                         uint64_t *out);
```

The entry must be an exact current loader-installed function start. Its signature
is a caller-known zero-argument uint64/object ABI; entry provenance does not infer
argument or return types. The caller owns a pinned, writable, adequate stack span
inside the runtime arena, separate from every other live code, metadata, data and
C-frame range. Base and top must be 16-byte aligned, with at least 4,096 bytes.
That private alignment is stronger than the four-byte default C ABI requirement.
The minimum size is a bootstrap floor, not proof of an arbitrary body's stack
budget. Software boundary checks cannot prevent a target stack overflow.

`out` is separate, valid caller-owned uint64 storage. Failure leaves it unchanged.
Validation checks the entry, span bounds/alignment/overflow, overlap with the exact
target code span, output overlap (including native RAM aliases), current SP and
non-reentrancy. It does not establish complete ownership merely from a span.
The caller still excludes all other live ranges. Host builds validate inputs and
refuse execution with BAD_ARGUMENT, including a valid registered entry.

Native calls enter with enabled FPU and normal C FPSCR.FR/SZ/PR modes. Those are C
ABI preconditions, including for compiler-generated prologues. The snapshot mode
check provides an additional guard; it is not permission to enter arbitrary C
with a nondefault FPU mode. No exceptions, longjmp, GC or nonreturning targets are
supported while the alternate stack is active. Runtime installation/execution is
quiescent, with one synchronous call at a time and no nested stack calls.

The API sets no implicit process binding. An original implicit-self entry still
requires explicit context binding. A target that returns normally with a runtime
error causes the API to return zero and preserve `out`; earlier target effects
are not rolled back. The active-call flag is cleared after any normal return.
There is no general failure unwinding.

## Native trampoline

The private `dc_goal_stack_call_raw` entry is used only after C validation. It
saves PR, R8..R14, FPUL, FPSCR and FR12..FR15 on the original stack (56 bytes).
R8 holds that original frame and R9 the target while SP is set to the supplied
top. The target follows ordinary callee-save conventions and returns through JSR.

On return, the trampoline recovers the original SP and parks both uint64 result
halves in scratch R2:R3. It establishes the default FPU bank/transfer mode before
popping the original FR12..FR15, then restores the original full FPSCR, FPUL,
GPRs and PR. R0:R1 return the exact scalar bits. Caller-clobbered registers outside
this set are not promised unchanged. No SR/GBR switching or allocator is involved.
The assembly is 84 bytes of 16-bit SH-4 instructions, with no literal pool.
This is a code-size observation, not a performance or real-hardware result.

## Independent acceptance

488 new native checks pass in stock 16 MiB Flycast. They cover 32 varying 64-bit
C callback results with live GCC GPR/FPR/status/stack canaries; callback snapshots
showing SP inside the owned 16 KiB span; each of seven target FR/SZ/PR mode-return
combinations; both stack fences; rejected pointer/span/entry/output requests;
nested-call rejection and propagated error; and a later deliberate diagnostic
call after normal error return. The mode-changing test bodies return directly
to assembly, which restores the original mode before returning to C.

An original-constructed process and CPU thread then use this same stack. Missing
execution identity rejects before original frame access. With explicit binding,
unchanged `remove-exit` pops its original frame and returns false on the alternate
stack. The original CPU header and saved registers remain unchanged; status stays
dead and main-thread stays false. The context is explicitly released afterward.
This proves the tested body can run on that stack, without claiming activation or
full kernel behavior.

25 host boundary checks exercise the actual validation code while proving host
execution is refused, outputs/managed memory remain unchanged and no allocation
or registry change occurs. The full suite passes 191 host tests, 20,606 runtime
checks and 39 numeric checks. Diagnostic boxed/native GOAL code is 134,636 bytes;
main arena 468,256 / 1,048,576 bytes, 660 symbols. The added 110 boxed bytes are
native diagnostic entries. Executable/KOS, C/assembly code, stacks outside the
arena, renderer and separate fixtures are not whole-game memory accounting.
The owned diagnostic stack itself is included in this main arena figure.

## Next gate

Cooperative yield/resume needs explicit stable saved-state ownership, validated
continuation sites and SPs, lifecycle/activation rules, stack-image integration
and error cleanup. This call/return trampoline is not a suspended context.
Original platform startup, GC, failure unwinding and gameplay remain unfinished.
Physical Dreamcast validation and CDs remain deferred.
