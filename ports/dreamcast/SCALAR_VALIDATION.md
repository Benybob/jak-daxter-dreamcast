# Dreamcast scalar memory context and original object validation

2026-10-21. (AI-assisted). The unchanged original `valid?` now executes as native
SH-4 code in stock 16 MiB Flycast. Its desktop `END_OF_MEMORY=0x08000000`
previously excluded the Dreamcast's cached P0 RAM at `0x0c000000`.

`scalar_validation_context.gc` is an explicit, bounded compiler context:

- `END_OF_MEMORY` becomes the exclusive stock-RAM end `0x0d000000`.
- Symbol-table macros use the actual reserved native table: 262,144 bytes,
  with `s7=table+65,540`. Their offsets are -65,540 and +196,604. The original
  approximate +/-32,768 bounds exclude real native hash buckets.
- PC_PORT, BIG_MEMORY, profiler and VM choices remain untouched. This context
  enables no PS2 scratchpad/MMIO, desktop runtime bridges, process heap,
  scheduler or full kernel startup. It applies only to scalar validation.

The original gcommon file compiles first as context, followed by this visible
Dreamcast context, then the selected original `valid?` reader object. No original
function body is copied or rewritten. Self-preloading a source is permitted
only with explicit selected defuns; full-file self-preloads, duplicates and
missing sources still reject. IR and manifests record the selection and pin
ordered source/context hashes. Compiler context does not run native startup.

Four new host tests cover selected self-context, strict rejection, manifest
provenance, context ordering and original validator lowering. Seventy-seven
new native checks cover raw addresses, exclusive RAM bounds, P0/P1 distinction,
16-byte structures, pairs/empty pairs, 64-bit bintegers, false policy, recursive
type validation, inheritance, malformed type tags and symbols across both old
excluded table ranges. Printing is suppressed by passing name=#f; the format
import is retained, not implemented as a silent no-op. The original validator
is a shallow diagnostic and does not establish that arbitrary in-range pointers
are allocated/readable. Tests only dereference known allocated fixtures.

All **146 host tests, 8,476 native runtime checks and 39 numeric checks pass**
in stock 16 MiB Flycast. Diagnostic code is 68,124 bytes, including 26,574 bytes
of far-branch probes/padding and a retained 3,244-byte shift baseline. The
original validator itself is 8,248 bytes with seven far jumps and seven literal
islands. Arena use is 348,416 / 1,048,576 bytes with 305 symbols, excluding
KOS/executable/stacks/renderer. These figures do not establish whole-game fit.

Reproduce from the repository root after building the typed frontend:

```powershell
./ports/dreamcast/build_host_windows.ps1
python ports/dreamcast/build_runtime_goal.py --verify
python ports/dreamcast/build_numeric_goal.py --verify
```

Inspect just the selected original validator with its explicit context:

```powershell
python ports/dreamcast/compile_goal_subset.py --frontend ../og-host/bin/goalc-dreamcast-ir.exe --source goal_src/jak1/kernel/gcommon.gc --preload goal_src/jak1/kernel/gcommon.gc --preload ports/dreamcast/scalar_validation_context.gc --select-defuns --function "valid?" --runtime --header ports/dreamcast/build/original_valid.h
```

Generic original-source export without this context remains available and keeps
its desktop constants. Full-file target configuration still requires audited
PC/profiler/MMIO paths, memory layout, process/self roles and platform bridges.
Whole original gcommon/gkernel/gstate modules are not started; full kernel and
gameplay remain unfinished. Physical Dreamcast is available later; no CDs are
prepared or burned, and physical hardware validation is not claimed.
