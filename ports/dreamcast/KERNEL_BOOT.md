# Native boot of the original jak1 kernel (AI-assisted)

`jak-dc-kernel-boot.elf` runs the top-level startup of every file in the
original `KERNEL.CGO`, unchanged and in its original load order, as native SH-4
code. It runs in stock 16 MiB Flycast. A process then starts the original way and runs under the
original `kernel-dispatcher`. **87 of 87 checks pass.**

Build and verify:

```bash
python ports/dreamcast/build_kernel_boot.py --msys C:/msys64 --verify
```

## What runs

| Order | File | Startup |
| --- | --- | --- |
| 1 | gcommon | links and runs; the scalar-configured `valid?` then replaces the desktop memory bounds |
| 2 | gstring-h | links and runs |
| 3 | gkernel-h | links and runs (defines the kernel types and the `break` macro pskernel uses) |
| 4 | gkernel | links and runs; the 8 assembly primitives bind to native counterparts ([KERNEL_COMPILATION.md](KERNEL_COMPILATION.md)) |
| 5 | pskernel | links and runs |
| 6 | gstring | links and runs |
| 7 | dgo-h | links and runs |
| 8 | gstate | links and runs |

Each file compiles as a whole module (`--all-functions`), with the preloads that
match `goal_src/jak1/game.gp`. The Dreamcast target contexts are explicit:

- scalar memory bounds;
- no desktop profiler (`PC_PROFILER_ENABLE #f`);
- original console memory sizes (`BIG_MEMORY #f`, from `dreamcast_memory_context.gc`).
  **Correction (2026-10-27):** until this date the context did not take effect, so the
  kernel ran with the PC 3x actor heap; see [MEMORY_BUDGET.md](MEMORY_BUDGET.md);
- a 512-byte main-thread stack backup (`PROCESS_STACK_SAVE_SIZE`, from
  `dreamcast_thread_context.gc`). The original 256 bytes fit PS2 frames; a suspended
  native main thread needs 348.

Before the original files load, C installs the native counterpart of the desktop
C++ kernel's setup:

- core, type, integer, object, process, scope, transfer and catch bridges;
- the fixed kernel types;
- `_format`, `malloc` and `nothing`.

## What the checks verify

After startup, the checks read what the original code built:

- `*kernel-context*` exists, with no current process;
- the DRAM execution stack;
- the listener process with PID 1, and the null process;
- the 16k, 8k, 4k, target, camera and camera-master dead pools;
- `*nk-dead-pool*` as a `dead-pool-heap`, and it is the default pool;
- no debug pool, because there is no debug segment;
- `*dead-pool-list*` and `*active-pool*`.

gkernel's own `(inspect *test-vec4s*)` prints through native `_format`, with its
type symbol and float fields.

Startup allocates about 1.9 MiB with the corrected context (3.6 MiB before it).
The compiled-in boot ELF uses a 4 MiB arena, and the DGO boot uses 8 MiB.

## A process started the original way

After boot, the checks follow the original `process-spawn-function` sequence:

1. Original `get-process` takes a member of `*8k-dead-pool*`.
2. Original `activate` assigns the next PID, builds the main thread in the
   process heap (with its 512-byte backup), and moves the process under
   `*default-pool*`. The execution stack is an owned 16 KiB stack, in place of
   the scratchpad default.
3. Original `set-to-run` (what `run-next-time-in-process` expands to) arms the
   main thread with original `looping-code`. The status becomes `waiting-to-run`.
4. Trusted C notes the member's exact extent, imports the armed image into a
   native fiber, and registers it for the original resume-hook.
5. Three frames of the original `kernel-dispatcher` each resume the process.
   The process then suspends, and current-process is cleared.
6. Original `deactivate`, called with the booted listener as the current
   process, kills it and returns it to the 8k pool. A further dispatcher frame
   skips it.

## Loading the kernel from KERNEL.CGO

`jak-dc-kernel-dgo.elf` boots the same kernel from a DGO file, not from
compiled-in modules. **90 of 90 checks pass.** Code bytes (247,366), arena use
and symbol count match the compiled-in boot exactly.

- Each module is also written as a DCSH native module ([NATIVE_MODULES.md](NATIVE_MODULES.md)).
- `pack_native_dgo.py` packs them into `build/kernel-dgo/KERNEL.CGO`, using the
  original DGO container layout:
  - a header with the object count and a 60-byte name;
  - per object, its size and 60-byte name, the payload, then padding to 16 bytes.

  This is the same layout `dgo_stream.c` validates. The objects are the 8 original
  files in order, with the Dreamcast `valid?` override as a ninth object right
  after gcommon.
- The romdisk carries the file to `/rd/KERNEL.CGO`.
- `goal_dgo.c` is the native counterpart of the C++ kernel's `load_and_link_dgo`:
  - it reads each object, links it, and runs its startup before reading the next;
  - the first failure stops loading and names the failed object.
- The checks confirm the object names and order, and that 8 startups ran.
  Everything after that is the same as the compiled-in boot.
- Damaged files refuse before anything links:
  - a compressed (`oZlB`) header;
  - a first object with a flipped payload byte, which its CRC catches.

  The symbol count and arena are unchanged afterwards.

DCSH objects are native SH-4 code from this port's compiler. Retail PS2 objects
are not loaded. GAME.CGO's first engine files follow (next section).
## GAME.CGO: first engine files

`build_game_cgo.py --count N` compiles the first N files of the original
GAME.CGO (order from `dgos/game.gd`). Each is a whole module with its
top-level startup, compiled after the complete kernel and every earlier engine
file. They are packed in order into `build/kernel-dgo/GAME.CGO`.

The DGO boot loads it right after KERNEL.CGO, as the original loads GAME.CGO
after the kernel. The first 48 files, through decomp-h, link and run their
original startups ([GAME_CGO.md](GAME_CGO.md)). The order check reads `build/game_cgo_order.h`, which
`build_game_cgo.py` writes. The process spawn and dispatcher checks still pass
afterwards. The boot then checks the VU0 vf lowering: 141 cases, lane-exact
([VU0_LOWERING.md](VU0_LOWERING.md)). **148 of 148 checks pass.** The arena is
now 8 MiB, of which 5.2 MiB is used after the 48-file prefix.
## Fixes this needed

- `export_ir.cpp` now gives scalar bitfield types (for example `handle`) the
  integer ABI class, the same as enums. 128-bit bitfields keep the int128 class.
- `_format`'s `~A`/`~S` now print symbols by name. Symbols live in the symbol
  table, not as tagged basics.

## Not done yet

- The fiber import and span notes are still explicit trusted-C steps; no original
  GOAL caller performs them yet. Trans/post hooks and state `go` are verified only in the
  runtime suite ([ORIGINAL_DISPATCHER.md](ORIGINAL_DISPATCHER.md)), not in this image.
- The rest of GAME.CGO (see [GAME_CGO.md](GAME_CGO.md)), a Dreamcast memory
  budget, GC, level DGOs and gameplay are unfinished.
- No hardware claims.
