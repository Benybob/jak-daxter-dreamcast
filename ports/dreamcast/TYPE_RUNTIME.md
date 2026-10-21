# Native type creation and virtual methods

2026-10-21. AI-assisted work. This is an incomplete native kernel foundation.

The original reader/typechecker compiles `deftype`, `defmethod`, field accesses,
`new` and virtual method calls into typed IR. Those original IR operations now
execute on SH-4 for a bounded scalar class profile. No method bodies are
translated to C, interpreted or rewritten. `type_fixture.gc` is a synthetic
class regression using those original language forms; it is not a game class.

The frontend records the original semantic types and separate ABI classifications
derived from its TypeSystem. A user-defined reference with a four-byte load is
a native address in the existing uint64 scalar container ABI. A name alone does
not authorize a new type. Non-reference aggregates, SIMD register values and
mixed ABIs remain rejected. The numeric profile retains its prior restrictions.

`dc_goal_install_type_bridges` requires the same active core bridge runtime and
installs three boxed SH-4 tail stubs: type/new, method-set! and bounded basic
allocation. The type/new stub is installed in `type`'s method zero; the basic
allocator in `basic`'s method zero. The original deftype call passes a symbol,
parent type and 64-bit flags, with the flags received in a verified GCC stack
argument. Size, heap base and method count occupy successive 16-bit fields.
A zero method-count flag selects the original twelve-method default.

Types retain the original object layout and symbol identity. Reinitialization
can update size, heap metadata and parent and reset inherited methods within
the existing method capacity. Growth requiring a larger table, cyclic parents,
invalid tags, padded-size overflow and allocation exhaustion are rejected.
Original data imports may first intern a forward type (12 or 44 default slots,
zero parent/size). Native deftype later refines the same object within its
allocated method capacity. `OBJECTS.md` describes the data import path.
Parent copies are bounded by the parent's table; extra child slots are zeroed,
instead of reproducing the upstream documented out-of-bounds inheritance bug.

`dc_goal_method_set` implements the original control values: 0 leaves the method
unchanged, 1 clears it, 2 inherits from the parent. Ordinary replacements must
be boxed native functions. `*enable-method-set*` enables propagation to installed
symbol-bound descendant types whose slot still equals the old method value.
Other overrides and unrelated types are retained. Two passes validate the
update before writing any slots. A corrupt descendant chain aborts the update.
Installation and mutation require a quiescent runtime with no process switching.

This profile supports the explicit enable flag, without the desktop linker's
FastLink/MasterDebug/DiskBoot fallback policies. The basic allocator accepts only
the original global heap selector and descendants of basic and uses bounded
zeroed bump allocation. It does not implement actual GOAL heap objects, process
heaps, deletion, GC or the original custom allocator policy.

The shared host/SH-4 cases cover flags, tags, inheritance, redefinition, method
controls, propagation, overrides, corrupt cycles, capacity and exhaustion.
Compiled target cases create two classes, install a native constructor/method,
allocate both types through original `new`, read/write original fields, dispatch
through inherited and replaced methods, exercise original method-set controls,
and rerun startup without allocating duplicate types. Automatic inspectors are
disabled for this fixture because their formatting/kernel dependencies remain
open. Broader game types and kernel initialization are still future work.

Primary local sources: `goalc/compiler/compilation/Type.cpp`,
`game/kernel/jak1/kscheme.cpp` (`new_type`, `set_type_values`, `method_set`),
and `game/kernel/jak1/kscheme.h`. Their original layouts and call contracts drive
this implementation; Dreamcast bounds restrictions above are deliberate.
