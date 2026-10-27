# Dreamcast memory budget (AI-assisted)

2026-10-27, Claude Code. The Dreamcast has 16 MiB of main RAM; the PS2 has 32 MiB,
and the PC port sizes several buffers larger still. This page records what the
original startups allocate on Dreamcast, measured in Flycast, and every value
changed for the Dreamcast target.

**Result:** all **92** compiled GAME.CGO files now load and run their original
startups after KERNEL.CGO in stock 16 MiB Flycast: **192 of 192 boot checks pass**.
The arena holds 7.8 of its 8 MiB afterwards. Before this work loading stopped at
display (object 48).

## A correction: the kernel was running with PC-sized heaps

`dreamcast_memory_context.gc` was meant to restore the original multiplier
(`PROCESS_HEAP_MULT 1`) since the kernel-boot milestone. It never did.

- `defglobalconstant` stores its GOAL-side reader (GOOS) value **unevaluated**.
- So binding `PC_BIG_MEMORY` to `(and PC_PORT BIG_MEMORY)` left a non-empty
  list, and `(#if PC_BIG_MEMORY ...)` treats that as true.
- The kernel's `*nk-dead-pool*` was therefore compiled with 3 × 768 processes and
  3 × 984 KiB of heap, and every engine size scaled by the multiplier tripled with it.

The context now binds the literal `#f`. KERNEL.CGO's startup arena dropped from
3.92 MB to **1.89 MB**, and display's DMA buffers from 8.0 MB to 2.7 MB each,
before the budget below. KERNEL_BOOT.md's earlier "original console memory
sizes" claim was wrong until this fix.

## Target constants in the same file that uses them

Some size constants are defined inside the very file that uses them (display.gc
defines `DMA_BUFFER_GLOBAL_SIZE`), so no preloaded context can override them.
`dreamcast_memory_budget.gc` lists Dreamcast values as `(defconstant NAME VALUE)`
forms, and the exporter's `--batch` mode substitutes them:

- **Where:** the value of any `defconstant` of a listed name is replaced as the
  source is read, including inside `#cond`/`#if`. Files on disk are unchanged.
- **Recording:** each exported IR records the names it substituted
  (`target_constants`).
- **Staleness:** the batch status reports any budget entry no source defines.

| Constant | PC | PS2 | Dreamcast | Why |
| --- | --- | --- | --- | --- |
| `PROCESS_HEAP_MULT` (context) | 3 | 1 | 1 | original multiplier; fixes everything it scales |
| `DMA_BUFFER_GLOBAL_SIZE` (budget) | 2,664 KiB | 1,712 KiB | 1,712 KiB | the PS2 original; the PowerVR renderer decides what these hold, so revisit then |

## Measured startup profile

`verify_backend.py --dump dc_boot_memory_trace 2076` writes the per-object arena
trace that the DGO loader records. `memory_trace.py` prints it.

| | bytes |
| --- | --- |
| KERNEL.CGO startup (pools, stacks, listener) | 1,890,208 |
| display (two 1,712 KiB DMA buffers, calc buffers, display) | 3,579,440 |
| texture | 145,648 |
| text-h | 140,864 |
| gs | 133,296 |
| all 92 GAME.CGO objects | 5,905,344 |

The 2.7 MB GAME.CGO and 0.3 MB KERNEL.CGO sit in the romdisk inside the ELF for
these tests (3.4 MB of text). A disc build would stream them instead.

## Native services added to keep loading

These came up while loading, not as budget items. All are native counterparts
of the desktop C++ kernel:

- **`kmalloc`**: global heap only, zeroed, with align-64/256. Other kheaps refuse.
- **`scf-get-time`**: local time as BCD, exactly as desktop `libscf.cpp`.
  Stack objects in a live caller frame are accepted.
- **`structure` new**: now propagated to fixed kernel types made before it, such
  as `kheap`.
- **`__pc-get-mips2c`**: the PC port's C++ translations of original MIPS
  assembly (80 lookups across collision, bones, merc, generic, ripple and
  texture) have **no SH-4 counterparts yet**. The lookup returns a stub that
  faults with `UNSUPPORTED_TRANSFER` if it is ever called. Startup only stores
  these pointers.

## Still open

- **Level heaps:** `LEVEL_HEAP_SIZE` is 10,416 KiB per level, two levels, on PS2
  too: more than the whole Dreamcast. Levels need a streaming design, not just a
  smaller constant. These heaps are allocated when levels load, past the files
  that load now.
- **The 8 MiB arena** is nearly full after 92 files. Raising it trades against
  the romdisk/ELF in these tests and against VRAM-side and audio-side buffers
  later.
- **mips2c functions** need real SH-4 implementations before gameplay. The C++
  sources in `game/mips2c` might compile for SH-4, which is worth testing first.

`tests/test_memory_budget.py` runs the real frontend on a small file to check that
the memory context gives `PROCESS_HEAP_MULT` 1. It also checks that a budget entry
replaces both `#cond` branches of an in-file constant and is recorded, and that a
stale entry is reported. This keeps the original bug from returning silently.

No hardware claims. Everything is verified in Flycast.
