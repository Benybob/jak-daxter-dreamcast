# Guarded original process entry (AI-assisted)

The unchanged original `remove-exit` behavior now runs as native SH-4 code.
It pops one stack-frame link, or returns #f for an empty stack. Its implicit
`self` reference comes from a deliberate execution binding before body memory
access. Allocation-only binding never installs execution identity.

The original reader/macro/typechecker exports the exact `self:process` fixed
entry on desktop r13 as `process_self`. Only one implicit self, zero explicit
arguments, object return, and an initial matching reset are currently supported.
This is a semantic mapping, not use of physical SH-4 R13. Other r13 roles,
resetless `process_self` entries, assembly, ambiguous entries and fixed-register writes reject.
The self register is read-only; the original body can update process fields.

An additive explicit-argument, read-only typed `process_context_read` entry is
described in [PROTECT_FRAME_ENTRY.md](PROTECT_FRAME_ENTRY.md). It authenticates
the same deliberately bound process before the original constructor body. It
does not enable mutable lexical pp or arbitrary resetless register roles.

The normal framed entry calls the strict `function:dc-c-current-process` import,
tests its return, and either stores the real self value or exits through the
ordinary stack/PR/callee-saved restoration with zero. A missing getter rejects
module linking transactionally. Missing context cannot become zero/#f followed
by unchecked body access. This is the bootstrap error-status/zero convention;
it does not implement general GOAL failure unwinding or propagation through
arbitrary calling code.

## Execution binding

After installing core and process bridges and explicitly initializing the
original kernel-context slice, `dc_goal_bind_process_context(rt, process, bytes)`
validates the caller-owned, unmoved base-process reservation using the bounded
heap validator. It requires the original 48-byte context type/schema and a
false current-process field. It sets that field and binds the same known span
for allocation and execution. The getter revalidates the heap, original context
global/type/schema, current-process field and matching allocation binding.

Changing/clearing the allocation target invalidates execution identity.
Invalid same-runtime execution binding clears the former binding. A failed
getter clears both bindings and sets BAD_ARGUMENT. Clearing releases the
current-process field only if it still holds the owned process reference;
foreign changes are preserved and require explicit caller restoration.
Repairing mutated fields never resurrects a cleared context.

Callers must clear the execution context when their quiescent scope ends.
There is one active runtime and one explicit scope, without nested restoration,
automatic lifetime tracking, object ownership discovery, process movement or GC.
Processes and referenced frames must remain valid original-layout objects.
Binding does not activate dead processes or implement threads/scheduling.

## Verification

Seven additional host tests exercise unchanged source selection, exact metadata,
strict import and rejection of unsupported/resetless/mutable entries.
The native suite adds 91 checks: missing-import rollback, missing context without
process writes, allocation-only rejection, two original-constructed owners,
zero/one/two original-layout frames, exact one-link pops, unchanged frame bytes,
independent owners, stale heap/global/current-process rejection, rebinding and
scope clearing. An independent GCC caller keeps R8..R13 values and stack guards
live across both successful and failed native exits.

Current full checks: 178 host tests, 11,848 native runtime checks and 39 numeric
checks in stock 16 MiB Flycast. The original behavior is 260 bytes with a 40-byte
frame; the getter trampoline is 12 bytes. Diagnostic GOAL code is 121,248 bytes,
arena 419,200 / 1,048,576 bytes, 614 symbols, excluding KOS/executable/stacks and
renderer. Whole-game memory fit and physical Dreamcast validation are unproven.

Broader process arguments/roles, thread switching, catch/throw assembly, GC,
failure unwinding, complete platform startup and gameplay remain unfinished.
