# Original process queries and name comparisons (AI-assisted)

The unchanged original `process-by-name`, `process-not-name` and `process-count`
now execute with their three real anonymous callbacks, calling the original
process-tree traversal and original `name=` / `string=` routines. These are
native calls through original function references and symbol cells; there is
no C replacement for the game routines.

Full original gkernel frontend export retains the anonymous functions. All six
query functions must be explicitly selected together; selecting just a wrapper
still rejects its unresolved function object. The original gstring comparison
definitions are selected as reader forms with original declarations/macros.
Source, selection and ordered preload hashes are recorded by the normal build.

Two explicit original startup slices initialize the single static
`*null-kernel-context*` definition and the contiguous pair of search globals.
The former retains its original 48-byte zero-filled image. These bounded slices
exclude execution stacks, scratchpad setup and the complete kernel top level.
The count routine's null context is distinct from the explicit execution context.

Original GOAL callbacks share `*global-search-name*` and `*global-search-count*`;
the original queries are not reentrant or a concurrent search facility. The
fixture runs quiescently with valid original-layout objects and no threads/GC.
Name comparisons accept symbols and strings; string comparisons use original
NUL termination, including their explicit zero-reference guard. Arbitrary
invalid basic/string pointers are outside the original typed calling contract.

## Verification

Three new host tests verify source/context hashes, anonymous callback closure,
the unsigned byte loads and original symbol-to-string offset, audited imports,
and the exact original global slices.

508 new native checks cover a 12-by-12 matrix of mixed symbol/string names and
all 81 string pairs: equal content in separate allocations, different names,
prefixes, empty strings, high bytes, and an early NUL despite extra allocated
capacity. Original string= also handles either/both references being zero.

Original constructors and change-parent build four processes and two group
nodes. Query results/counts agree with an independent index-based DFS across
each of the six roots and twelve names, including duplicate names, first-match
order, first nonmatch, absent matches and organizational-node exclusion. A mask
change excludes the node itself while retaining its children. Queries preserve
all tree fields and the null context and allocate no memory.

All 181 host tests, 12,356 runtime checks and 39 numeric checks pass in stock
16 MiB Flycast. Diagnostic GOAL code is 125,832 bytes, arena 425,328 / 1,048,576
bytes, 630 symbols, excluding executable/KOS/stacks/renderer. This does not
establish whole-game resource fit or physical Dreamcast correctness.

Full process activation, thread switching, GC, general failure unwinding,
complete platform startup and gameplay remain unfinished.
