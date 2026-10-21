# Original fixed kernel type metadata

2026-10-21. AI-assisted implementation. The complete kernel/game is unfinished.

`generate_kernel_types.py` derives all 35 `set_fixed_type` declarations and six
zero-valued asset placeholders directly from the pinned Jak 1 C bootstrap.
The generated table records original names, fixed offsets, parents, allocation
sizes, heap-base values and method capacities with source hashes. It fails if a
new declaration cannot be parsed or its parent order is incompatible. The
original stored spelling `uintger` is preserved; this stage does not invent
aliases for original namespace quirks.

`dc_goal_install_kernel_types` adds the 24 definitions beyond the minimal
eleven-type runtime and binds the six original asset names, plus the canonical
global selector. Install it before asset imports or compiled code can create
conflicting dynamic names. Existing complete types, methods and placeholder
values are validated/preserved. Repeating installation creates no objects or
names. A temporary 262,144-byte symbol snapshot and arena checkpoint support
rollback of partial allocations, OOM and late namespace collisions. Only newly
allocated type blocks are initialized, so failure cannot rewrite old metadata.

This stage installs metadata and bounded inherited slots. Original printers,
inspectors, illegal-allocation handlers and other remaining method policies
are not implemented here. Process/thread layouts do not implement scheduling,
stack switching or process heaps. Heap selector binding is not a GOAL heap
object. Existing native pair/basic/type bridges remain separate. No unimplemented
kernel routine is presented as completed by this metadata installation.

The shared host/SH-4 test covers all 35 names/offsets, tags, parents and scalar
metadata, method inheritance, repeat setup, preservation of overrides and
initialized bindings, exact table/arena rollback, late collision and mismatched
old metadata. It runs 289 assertions; the complete native runtime diagnostic
passes **775/775 at stock 16 MiB RAM**. The host suite passes **85 tests**.
The separate original-data target installs this metadata first and passes
**182/182**, including both real retail samples and their native fixups.

The expanded runtime probes emit the same 15,332 GOAL code bytes; the new
metadata stage adds C code and data outside that measure. Probe arena use is
287,024 bytes of 1 MiB with 167 installed symbols. These include all diagnostic
modules and test objects, not just kernel metadata, and exclude KOS, executable,
ordinary stacks, allocator overhead and the separate renderer.

Reproduce from the repository with the documented surrounding workspace:

```powershell
python ports/dreamcast/generate_kernel_types.py
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_objects.py --retail-archive iso_data/jak1/CGO/GAME.CGO --retail-art-archive iso_data/jak1/DGO/TRA.DGO --verify
```

Original source contracts: `game/kernel/jak1/kscheme.cpp` (`InitHeapAndSymbol`,
`set_fixed_type`) and `common/symbols.h`. See `TYPE_RUNTIME.md`, `RUNTIME.md` and
`OBJECTS.md` for native type construction, startup and retail data loading.
