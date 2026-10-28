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
trace that the DGO loader records. `memory_trace.py` prints it. `tests/test_memory_trace.py`
checks its reading of a synthetic dump (deltas, ranking, the failed object, a missing trace).

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
- **The boot arena** filled its 8 MiB at projectiles-h (object 116, a refused
  4,352-byte request), then 10 MiB at object 171. GAME.CGO in the test ELF's
  romdisk is now **zstd** (4.9 MB → 0.66 MB, `zstd -19 --zstd=wlog=17`), streamed
  object by object through the vendored decompressor with a 128 KiB window
  (`goal_dgo_zst.c`). The ELF fell from 6.2 to 2.0 MB, and the arena is **12 MiB**:
  192 files use 11.45 MB, and 237 use 12.44 MB. The arena is full at ambient
  (object 240: 12.52 of 12.58 MB, a refused 1,952-byte request), so the next files
  need the arena to grow again or memory to shrink. The largest single item is
  display's two PS2 DMA buffers: 3.4 MB of PS2 GIF/VIF packets that the PowerVR
  renderer will not consume as they are. Their size is a renderer decision
  (`DMA_BUFFER_GLOBAL_SIZE` in `dreamcast_memory_budget.gc`). Spare VRAM is not a fit for the current users (see
  [VRAM_POOL.md](VRAM_POOL.md)). A disc build would stream from the GD-ROM instead. It
  still trades against VRAM-side and audio-side buffers later.
- **mips2c functions** need real SH-4 implementations before gameplay. A first
  probe is encouraging: `probe_mips2c_sh4.py` syntax-checks all **25 of 25** jak1
  `game/mips2c` C++ files with `sh-elf-g++`. It needs only two adaptations: a
  scalar stand-in for the 8 SSE intrinsics they use, and vu.h's `REALLY_INLINE`
  on a non-Linux platform. That is parsing only: nothing is linked or run, and
  their memory model (GOAL addresses, the scratchpad) is still to be checked.

`tests/test_memory_budget.py` runs the real frontend on a small file to check that
the memory context gives `PROCESS_HEAP_MULT` 1. It also checks that a budget entry
replaces both `#cond` branches of an in-file constant and is recorded, and that a
stale entry is reported. This keeps the original bug from returning silently.

No hardware claims. Everything is verified in Flycast.
