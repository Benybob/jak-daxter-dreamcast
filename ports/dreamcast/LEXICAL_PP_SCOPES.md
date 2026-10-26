# Lexical process scopes and native return cleanup

2026-10-26. (AI-assisted). Original protect-frame construction and `remove-exit`
now execute under lexical process B while scheduler current-process remains A.
Process allocations follow B and restore A on leave. This is a dependency of
original cleanup callbacks. Full `deactivate`, exit execution, initialization
throws, the scheduler, GC and original gameplay remain unfinished.

All **394 host tests** and **23,056 native runtime checks** pass in stock 16 MiB
Flycast, including 207 new native checks. Four new portable host groups perform
140 independent checks. The separate 39-check numeric checkpoint is retained;
its lowerer and executable are unchanged.

## Three identities

`dc_goal_bind_process_context` deliberately binds scheduler A and its explicit
owned reservation. Registering a `dc_goal_pp_scope_stack` alone establishes no
execution identity. Entering B with its exact reservation pushes lexical identity
and saves/replaces the process allocation record. Scheduler current-process is
read and authenticated, never changed by enter or leave.

The existing compiled `dc-c-current-process` getter authenticates the top scope.
It validates the original 48-byte kernel context, its symbol/current field,
scheduler and all live ancestor reservation geometry, and PID witnesses. It
returns B or nested C while retaining an existing error. A denied scoped getter
returns zero before the compiled original body accesses memory. Authentication
itself reads validated process type/heap/PID fields.

Process-symbol `new_basic` and sized `new_object` reauthenticate the lexical
domain. They allocate from that explicit heap with the existing original strict
fit, alignment and zeroing rules. A retained scope fault refuses a fresh
allocation. Global allocation keeps its explicit global meaning. Allocation-only
binding outside scopes retains its previous behavior and never supplies pp.

Leave restores the saved allocation record by value, including while the outgoing
span is stale or an error is retained. Out-of-order or foreign-owner leave
refuses. Later body access reauthenticates the restored identity. Binding/clearing
execution or allocation and clearing core bridges refuse live scopes. Quiescent
core clear detaches the empty registration before releasing its runtime pointer.

## Ownership and lifetime

The stack is stable caller-owned C metadata outside the runtime arena/object,
fiber metadata and working stack. Eight frames are supported; overflow rejects
without altering the old nest. SH-4 compiler DWARF reports **348 bytes** for the
metadata object. It allocates no heap memory. Temporary getter output may live
on the current working stack; it is not retained metadata.

Reservations are explicit, pinned, unmoved base-process spans. Do not reclaim or
reuse them while borrowed. PID witnesses detect changes during the lease; they
are not a general reuse epoch, security capability or proof against PID wrap.
Derived process layouts and moving pools remain unsupported. Registration ownership
is held separately from mutable descriptor fields: foreign runtime queries cannot
drop the owner's nest, and descriptor runtime/depth corruption denies safely.

Native registration requires `dc_goal_kos_stack_owner`, which rejects unsupported
IRQ/SR/stack contexts. A running fiber must also match scheduler process/span,
runtime, phase, owner and the recorded fiber anchor. Normal teardown is quiescent
and owner-checked. The primitive's NULL hook is only the portable host model.

## Native transfers and errors

Live scopes refuse fiber step/resume and yield before working-image restore,
phase/IRQ changes or raw SP transfer. Yield preserves an earlier runtime fault.
The tested masked-IRQ path refuses getter and leave without process writes.

GCC entry return is another transfer. Before bootstrap marks DONE/FAULT or leaves
to its validated root, an internal fixed-site hook handles a leaked scope nest.
A valid nest restores the base scheduler allocation record, clears depth and
faults the fiber. An earlier error wins. Corrupt metadata drops registration
without memsetting untrusted caller storage and invalidates bridge bindings.
The root return uses the existing closed native continuation, never a guessed
SP/SR image or `longjmp`. This hook runs no original GOAL exit/deactivation code.
It is not an arbitrary public scope-leave operation.

## Independent verification

Native cases use unchanged original process/CPU-thread constructors and the
already compiled original protect constructor and `remove-exit`. Full reservation
and fenced-frame comparisons cover A/B/C, nested allocation domains, LIFO leave,
stale scheduler PID/heap metadata, refused binding/clear and allocation-only entry.

A registered GCC entry actually runs on the owned KOS working stack. Eight modes
check normal frame construction/pop/allocation under B, one-level and nested
leaks, retained-fault leaks, fresh/retained-error yield refusal and corrupt-depth
return. Caller checks preserve integer/FP state and stack canaries. Failed returns
publish no result. Corrupt return requires fresh explicit registration/binding.

Grok supplied the isolated initial C proposal and independently reviewed integration.
Codex reproduced and corrected hook alias loss, stale scheduler PID acceptance,
foreign-runtime registration loss and retained-fault yield overwrite. The last
case failed two real native checks before correction. Grok's reviews were source
evidence; the host/native receipts are independent execution evidence.

Runtime diagnostic GOAL/bridge code is 144,782 bytes, with arena use 679,472 /
1,048,576 bytes and 708 symbols. Arena use includes owned diagnostic working-stack
storage; KOS, executable/static storage, root C stacks and rendering are separate.
These counts do not prove whole-game memory fit or physical hardware performance.

## Reproduce and next gate

Use the configured frontend and KOS SDK, with one SH-4 build at a time:

```sh
python ports/dreamcast/build_runtime_goal.py --msys /path/to/msys --verify --output /private/pp-check
```

The normal host build includes `host_pp_scope.c` and its four Python case groups.
Do not relink the frontend while compiler clients execute it. No game assets,
ISO/BIOS or embedding ELFs are uploaded to GitHub.

Scopes are currently explicit C brackets. The compiler still refuses mutable
r13, r15 transfers and unsupported unwind operations in original `deactivate`.
Native execution of that full method also needs trusted process-span lookup,
original callback/callee integration and verified abandon/initialize continuations.
Do not remove original branches to compile it. Full startup, original gameplay,
rendering fidelity, audio, progression and saves remain open.
