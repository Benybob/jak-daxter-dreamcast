Current extension: explicit process reservations now support bounded allocation.
See [PROCESS_HEAPS.md](PROCESS_HEAPS.md). The global-only milestone figures and
scope below are historical. (AI-assisted).

# Original process constructors and bounded sized allocation

2026-10-21. (AI-assisted). The unchanged original new methods for process-tree
and process now execute on SH-4 through compiled GOAL method dispatch.
Their object-new macro dispatches to object.new(heap,type,size), adapted by an
explicit native bridge into the existing bounded, zeroing global arena.

The bridge accepts the quote-global selector, a valid basic subtype and a
32-bit requested size at least as large as the type's allocated size. It
preserves original type headers and rejects other selectors/types, undersized
requests, size overflow and insufficient arena space with an explicit runtime
error and no allocation. Process-heap selectors, implicit process allocation
context, heap GC and failure unwinding are not implemented by this bridge.

Original gkernel is compiled with ordered source context, then only its two
constructor methods lower; no whole-file initializer runs. Original bodies,
source/dependency hashes, method names, import sets and three-argument sized
allocation calls are pinned. A synthetic GOAL fixture uses ordinary new calls
after explicit installation of the real original methods, and supplies a real
GOAL predicate for original process search.

303 new native checks cover:

- global sized allocation, alignment, type header and zeroed body;
- ten wrong-selector/type/size/overflow/out-of-memory cases preserving the arena;
- original tree constructor fields and organizational-node mask;
- original process construction for dynamic reservations of 0,1,15,16,31,64,
  256 and 1024 bytes, with dead status, zero pid and original false fields;
- heap-base/cur at process+108, exclusive heap-top, aligned reservation, stable
  self/ppointer and untouched zeroed connection bytes;
- constructed processes attaching through original change-parent and being
  found through original search-process-tree with a compiled GOAL predicate.

Two host tests verify original method-only lowering, provenance/imports/native
call signature, all 23 scalar process-field offsets and the original dynamic
storage start. Layout probes cast field results to uint to avoid demanding full
definitions of unrelated forward types such as entity-actor. Game source and
its original type rules are unchanged.

All **163 host tests, 11,520 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. Original constructor bodies are 508 and 1,452 bytes;
the sized allocation tail entry is 12 bytes. Diagnostic code totals 118,918 bytes,
with arena 414,192 / 1,048,576 bytes and 586 symbols, excluding KOS/executable/
stacks/renderer. Totals include diagnostic ABI/far-branch/shift probes.
Whole-game resource fit remains unproven.

Reproduce after building the original typed frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

These processes remain dead, without threads or activation. Reservations are
measured global arena spans; they do not establish a complete process allocator
or the original shared process heap. Allocation-error cases call the bridge
directly; original constructors assume allocation succeeds and still need
appropriate failure/unwind handling before production use. Tests deliberately
provide valid fitting requests to those unchanged constructors.

Full target setup, scheduler/thread switching, process heaps and remaining
platform/ABI dependencies are unfinished. Physical Dreamcast is available later;
no CDs are prepared/burned, no physical hardware result is claimed, and full
gameplay remains unfinished.
