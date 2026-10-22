# Exact native export-entry provenance (AI-assisted)

The native module loader now records each installed function's exact entry,
byte length and owning symbol. A read-only query can distinguish an entry from
an arbitrary object carrying a function tag. Explicit scalar and void initializer
calls use this query before executing. Existing original initializers continue
to pass in stock 16 MiB Flycast.

This is a bootstrap provenance check for initial function calls. Internal resume
PCs, literal-pool positions, context restoration and stack switching require
additional contracts. Ordinary emitted calls retain their existing zero/false
target guard; this change does not add general control-flow integrity.

## Contract and representation

`dc_goal_code_entry(rt, entry, &info)` returns entry, code bytes and owning symbol
for an exact current registered entry. It clears all output fields on failure.
The caller supplies separate, stable output storage without aliases to the C
runtime or managed memory. Lookup does not allocate, intern names, execute code
or change process/allocation bindings; only the output and runtime error change.

Compiler descriptors and the cache callback are trusted embedding inputs. Load
and query occur while the runtime is quiescent; code and metadata remain pinned
and unchanged. Lookup is not a code-byte integrity check or a security boundary
against arbitrary memory writes. It cannot infer an ABI signature from an address.
Explicit initializer callers still select known zero-argument scalar/void bodies.

A private, plain arena record holds four 32-bit words: entry, bytes, owning symbol
and next record. It costs 16 resident bytes per installed export. Two C runtime
fields hold the list head/count. No original GOAL object or CPU-thread layout is
changed. Records are append-only and their links strictly descend in allocation
address, with an exact count and zero terminator.

Lookup rejects null/false, misalignment, unregistered objects, headers, interior
instructions, embedded literal data and past-end addresses. Traversal validates
record alignment, resolvable code spans, even nonzero code lengths, symbol tags,
strictly descending record and entry addresses, and exact termination. Invalid list structure reports
BAD_MODULE; an absent/stale entry or changed function tag/binding reports
BAD_FUNCTION. Bad C arguments report BAD_ARGUMENT.
Initializer wrappers retain their existing BAD_MODULE classification when entry
validation fails. Host callers of a valid registered initializer are still
refused with BAD_ARGUMENT; the original host loader tests cover both paths.

The owning symbol must still contain the recorded entry. Replacing its function
makes an old entry ineligible for a fresh lookup, even if another alias retains
that old pointer. Restoring the owning cell explicitly can make that entry
eligible again: this API is a current-state query, not a sticky execution token.
Old code remains allocated; active-frame pinning, unload and GC are future work.

## Loader transaction

Registration follows all code/data relocation and all cache synchronization.
Thus new entries remain unregistered while the cache callback runs. Each record
uses the same bump allocator as code. The existing loader snapshot includes the
head, count and used cursor, so failure after partial registration restores those
roots and namespace bindings together. No older record is edited. Code and record allocations follow export order,
so strict descending entry addresses also reject duplicate records. Retired bytes
beyond the restored cursor are inaccessible through the registry and are not
promised to be erased.

Native boxed bridges use this same loader and receive records too. Runtime init
clears both roots. Data-only objects and manually fabricated function objects do
not receive executable-entry provenance.

## Independent verification

271 shared C properties pass on host and SH-4. They cover exact spans and owning
symbols; fabricated typed objects; both initializer rejection paths; interior,
header, literal and end addresses; replacement/aliases/rebinding; changed tags;
12 independent list/span/count/symbol corruptions, including a duplicate entry; query output clearing and
managed-memory/allocator/namespace preservation; reset; and an allocation limit
that lets both code buffers synchronize and the first metadata record commit
before the second metadata allocation fails. The latter verifies complete loader
rollback after partial registration.

The property fixture never executes its registered test bytes. It temporarily
uses separate 512 KiB runtime storage and a 512 KiB byte snapshot, both freed;
these are outside the main arena metric. Native initializer rejection probes
must reject the unregistered typed object before any attempt to execute it.
The full suite also exercises unchanged original initializers through the new
lookup requirement.

All 190 host tests, 20,118 native runtime checks and 39 numeric checks pass in
stock 16 MiB Flycast. Diagnostic GOAL code remains 134,526 bytes, with 650 symbols.
The main arena grows from 438,592 to 446,928 / 1,048,576 bytes: 8,336 additional
bytes of resident entry records. Executable/KOS/stacks/renderer and temporary
fixtures remain outside this arena measure. This is not a whole-game memory-fit
or physical Dreamcast result.

Grok reviewed the candidate and identified the duplicate-entry ambiguity; Codex
added the ordering guard and independent corruption regression before publication.
Other review claims about initializer error classification and queried-only header
checking were checked against the existing API tests and documented contract.

Saved-register restoration, continuation validation, execution-stack transitions,
process activation, platform startup, GC, failure unwinding and gameplay remain
unfinished. Allocation binding still does not install execution identity.
