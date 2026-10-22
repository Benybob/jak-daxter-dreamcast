# Structured static data and original kernel-context initialization

2026-10-21. (AI-assisted). The original compiler's StaticStructure images now
export their little-endian byte payload, address offset and type/symbol/data/
function relocations. Native lowering validates each reachable image and its
closed dependency graph, then uses the existing transactional private-data
linker. Existing string/float/pair handling remains separate.

The supported structured subset uses address offsets 0 or 4 strictly inside the
payload, unique aligned four-byte patches, explicit known import kinds and
selected function bodies. Interior/external static data pointers and header-only
basic images remain unsupported; unknown or malformed referenced images reject.
Unused unsupported images remain inspectable in frontend output, so unrelated
original compilation is not made to fail.

An inclusive --from-global START with --prefix-through-global END selects a
contiguous range of original reader forms. Both boundaries must be unique and
ordered; missing/nested/reversed boundaries and incompatible selections reject.
The manifest pins source/dependency hashes, both boundary names and form count.
Original bodies and initializers are not copied or rewritten.

This permits the exact single original gkernel definition of *kernel-context*
to initialize natively. Its 48-byte static image has prevent-from-run=65,
next-pid=2, current/relocating-process=#f and low-memory-message=#t. Native tests
verify every field, no dynamic allocation during initialization, and exclusion
of later stack/scratchpad definitions. The existing validated 48-byte C bootstrap
type schema supplies its type relocation; full gkernel-h startup is not run.

Six new host tests cover original images/range provenance, inclusive order,
strict selection guards, closed graphs and malformed bytes/offsets/imports.
Fifty-six new native checks cover:

- transactional rollback when a structured type is missing;
- three typed basic images and a plain structure with nested data references;
- raw signed64/high-half/unsigned values and #f/#t relocations;
- three distinct static lambdas, called through original typed function fields;
- static function references preserving direct-body identity after a global
  function cell is rebound;
- explicit synthetic initialization and exact original kernel-context fields.

All **171 host tests, 11,757 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. Diagnostic code totals 120,976 bytes, arena 418,272 /
1,048,576 bytes with 610 symbols, excluding KOS/executable/stacks/renderer.
The structured synthetic payload is 104 bytes and original context 48 bytes.
Diagnostic ABI/far-branch/shift probes remain included; whole-game fit is unproven.

Reproduce after building the original typed frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

Compile just the original context definition for inspection:

```powershell
python ports/dreamcast/compile_goal_subset.py --frontend ../og-host/bin/goalc-dreamcast-ir.exe --source goal_src/jak1/kernel/gkernel.gc --preload goal_src/jak1/kernel/gcommon.gc --preload ports/dreamcast/scalar_validation_context.gc --preload goal_src/jak1/kernel/gkernel-h.gc --preload goal_src/jak1/kernel/gstring-h.gc --from-global "*kernel-context*" --prefix-through-global "*kernel-context*" --initializer dc-original-context-init --runtime --header ports/dreamcast/build/original_kernel_context.h
```

The unchanged desktop-configured individual-function inventory remains
gcommon 57/66, gkernel 54/76 and gstate 3/7. Whole kernel modules are not started.
Implicit pp/self, scheduler/thread switching, GC, failure unwinding and full
platform configuration remain unfinished. Physical Dreamcast is available later;
no CDs are prepared/burned, no physical hardware result is claimed, and full
gameplay remains unfinished.
