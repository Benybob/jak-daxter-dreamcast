# Native boot of the original jak1 kernel (AI-assisted)

`jak-dc-kernel-boot.elf` runs the top-level startup of every file in the
original `KERNEL.CGO`, unchanged and in its original load order, as native SH-4
code. It runs in stock 16 MiB Flycast. **50 of 50 checks pass.**

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

Startup allocates about 3.6 MiB. The ELF uses a 4 MiB arena.

## Fixes this needed

- `export_ir.cpp` now gives scalar bitfield types (for example `handle`) the
  integer ABI class, the same as enums. 128-bit bitfields keep the int128 class.
- `_format`'s `~A`/`~S` now print symbols by name. Symbols live in the symbol
  table, not as tagged basics.

## Not done yet

- A process spawned through the original pools has not yet run under the
  original `kernel-dispatcher` inside this boot image. The dispatcher is verified
  separately ([ORIGINAL_DISPATCHER.md](ORIGINAL_DISPATCHER.md)).
- GC, DGO loading, the engine and gameplay are unfinished.
- No hardware claims.
