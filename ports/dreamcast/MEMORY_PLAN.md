# Memory plan: main RAM, VRAM and sound RAM (2026-10-28, AI-assisted)

The Dreamcast has 16 MiB of main RAM, 8 MiB of VRAM and 2 MiB of sound RAM. The PS2
had 32 MiB of main RAM, 4 MiB of GS memory and 2 MiB of IOP memory. This page lists
where the DGO boot's memory goes, what the port already does about it, and the options
that are left, including unconventional uses of VRAM and sound RAM. It makes no
hardware claims: everything here is measured in stock 16 MiB Flycast.

## Where main RAM goes (DGO boot, 274 GAME.CGO files, before the DMA move)

| Item | Bytes | Notes |
| --- | --- | --- |
| ELF text, data and bss | 0.44 MB + 0.56 MB | test harness included (a 400 KB damage-test buffer) |
| KOS heap needs while loading | about 1 MB | per-object DCSH buffer, a 256 KiB symbol-table rollback snapshot, zstd context |
| GOAL arena | 13.5 MiB | everything GOAL allocates |
| — kernel startup | 1.79 MB | `*nk-dead-pool*` is the original 984 KiB actor heap |
| — display DMA buffers | 3.42 MB | two PS2 GIF/VIF packet buffers of 1,712 KiB |
| — GAME.CGO code | about 4.3 MB | after the slot and field shrinking in [GAME_CGO.md](GAME_CGO.md) |
| — GAME.CGO data and startup allocations | about 2.5 MB | sparticle, sprite, collide-cache, joint and so on |

## Done

- **Smaller code.** Displacement addressing, R0-indexed float slots, hot-first slot
  order, store forwarding and 32-bit field bases cut code by about a third
  ([GAME_CGO.md](GAME_CGO.md)).
- **Startups are freed after they run.** On PS2 the linker puts an object's top-level
  segment at the top of the heap and frees it once it ran. The DGO loader now does the
  same: each object's initializer is loaded into a temporary region at the top of the
  arena (`dc_goal_alloc_temporary`), runs, and is released; its symbol becomes `#f`.
  The temporary entry is authenticated through `rt->temporary_*`, outside the code
  registry. This saves about 0.5 MB for 274 files.
- **The romdisk is staged into VRAM.** KOS clears all of VRAM in `vid_init` and all of
  sound RAM in `spu_init`, before `main`, so the ELF loader can't fill either. The test
  ELF links its 1.5 MB romdisk high in main RAM (`.dc_stage` at 0x8cd80000), and the
  first thing `main` does is copy it into idle VRAM and remount `/rd` there
  (`romdisk_stage.c`). The main-RAM copy is then free for the heap, so the arena grew
  from 12 to 13.5 MiB; 14 MiB starves the KOS heap. With native mips2c code in the
  ELF it is 13 MiB, still well above the 10.7 MB that all of GAME.CGO uses. This stands in for streaming from
  the GD-ROM, which keeps no copy in main RAM at all.

- **The PS2 DMA buffers live in VRAM.** They hold PS2 packets that the CPU writes
  and the PS2 DMA read; on the Dreamcast nothing reads them yet. The runtime has an
  optional extension region outside the arena (`dc_goal_register_extension`), which
  `dc_goal_resolve` accepts and which is zeroed with 32-bit stores only (Flycast drops
  8-bit VRAM writes). The DGO boot registers idle VRAM from 1 to 6 MiB and places
  global `dma-buffer` objects of 512 KiB or more there (`dc_goal_place_in_extension`),
  which takes display's two 1,712 KiB buffers out of the arena: 274 files fell from
  12.07 to 8.57 MB. The renderer decides later whether they stay, shrink, or move
  back; until then nothing may write bytes into them.

VRAM layout in the DGO boot: the framebuffer below 1 MiB, the extension region (DMA
buffers) from 1 to 6 MiB, the staged romdisk from 6 to 8 MiB.

## Options left, best first

1. **A register allocator.** Code is still the largest thing that grows with
   GAME.CGO. Keeping values in registers instead of 8-byte stack slots would cut it
   much further than any peephole.
2. **Give VRAM back to the renderer.** The PowerVR renderer will want VRAM for
   framebuffers, tile-accelerator lists and textures. The romdisk staging goes away
   with GD-ROM streaming, and the DMA buffers should shrink to what the renderer
   actually needs.
3. **A smaller KOS heap need while loading.** The loader keeps a 256 KiB snapshot of
   the symbol table per object for rollback. A journal of only the changed slots, or
   the snapshot staged in VRAM during boot, would leave more room for the arena.
4. **Textures resident in VRAM.** The PS2 had to keep level textures in main RAM and
   upload them every frame. The Dreamcast can keep them in VRAM permanently. This is
   the main saving for level heaps (10,416 KiB each on PS2), with the renderer.
5. **Sound RAM.** Its natural use is sound banks, which the PS2 kept in IOP memory too.
   Unconventional uses are limited by its access path: the CPU reaches it over the G2
   bus with FIFO waits and 32-bit accesses, and KOS clears it at init. It suits bulk,
   sequential, rarely-touched data the CPU copies in and out: staging for level loads
   (as IOP memory was for DGO loads on PS2), compressed level data waiting to inflate,
   or the loader's rollback snapshot. It is not a place for GOAL objects that native
   code reads directly.
6. **Free the test harness's memory.** The DGO ELF keeps a 400 KB static buffer only
   for its damaged-file tests; allocating it when those tests run returns it.

VRAM and sound RAM can't hold GOAL code or objects that native code reads in its inner
loops: the CPU reads both slowly and can't execute from them usefully. Their value is
taking bulk data out of main RAM so the arena can hold code and objects.

## GAME.CGO census and two PC-only buffers (2026-11-04, AI-assisted)

Measured from `dc_boot_memory_trace` (arena use after every GAME.CGO object)
with each object's compiled function bytes from its lowering manifest: the 326
objects take 8,874,848 bytes, **6,334,194 of it code** and 2,540,654 data.
Largest data: collide-cache-h 356,776, sparticle 332,830, sprite 297,120,
subtitle 264,180. The 19 objects from `goal_src/jak1/pc/` (PC port only, not in
retail GAME.CGO) take 1,020,208 bytes; dropping them means a non-PC_PORT
target, which the port still relies on.

Code density: in `target` (290,275 bytes, 141,537 instructions), 33% of
instructions are loads or stores of R14 frame slots, plus address arithmetic
for slots beyond @(60,R14), and every value is an 8-byte pair even when it is
a 32-bit pointer. A register allocator and 32-bit values in single registers
remain the largest saving left.

First two steps toward it (`lower_sh4_ir.py`, 2026-11-04, AI-assisted):
- **Far frame windows.** R10..R13 are callee-saved and unused by every template
  (fiber and catch images already save them), so a function points up to four
  of them at R14+64k for its most-accessed 64-byte windows. Slots there take one
  `MOV.L @(disp,Rn)` like near slots. Off with `DC_SH4_NO_FRAME_BASES=1`.
- **Single-use temporaries in registers.** A gpr64 value written by one op and
  read only by the next has its store deferred in the emitter; if the read is the
  next code emitted the store is dropped and the read becomes register moves.
  Anything else emitted first (or a label that is a branch target) stores it as
  before; a second access relowers the function without that value. Off with
  `DC_SH4_NO_FORWARD=1`.

GAME.CGO 8,821,024 to 8,273,520 bytes (-547,504: -378,496 windows, -169,008
forwarding); KERNEL.CGO 214,096 to 210,368; TRA-CODE 102,464 to 98,784. Flycast:
kernel 90/532, runtime-goal 870, Jak 7652, level 539. Lowering GAME.CGO takes
about 2 minutes longer (function snapshots for the retry).

Two more (same day):
- **32-bit values.** A gpr64 value whose every store has a high register known
  to be zero (MOV #0 since the last label, call or other write of it) keeps only
  its low word in the slot; its loads set the high register with MOV #0. Assumed
  for every candidate, and a function relowers once without the values that
  missed. Off with `DC_SH4_NO_NARROW=1`.
- **Dead register writes** (`Emitter.remove_dead_writes`). After lowering, a
  MOV #imm, MOV Rm,Rn, frame-slot load or literal load whose register is
  overwritten before any read on the straight path after it is deleted; labels,
  branches and literals move with the code. Branches, calls and returns end the
  path as reads. Off with `DC_SH4_NO_DEAD_WRITES=1`.

GAME.CGO 8,273,520 to 7,679,888 (-238,352 32-bit values, -355,280 dead writes):
1,141,136 bytes (12.9%) below the stack baseline of the morning. KERNEL.CGO
192,784, TRA-CODE 92,608. All Flycast suites pass. GAME.CGO now lowers in
about 14 minutes (most functions lower twice).

Two PC-only buffers now take Dreamcast values through `dreamcast_memory_budget.gc`
(the exporter's budget now also substitutes `defglobalconstant`):
- `PC_SUBTITLE_FILE_SIZE` 256 KiB to 16 KiB: no subtitle files on the disc yet.
- `BIG_COLLIDE_CACHE_SIZE` 5000 to 460 triangles: the PC port mallocs this
  overflow area unconditionally for `activate-big-collide-cache!`, which no Jak
  1 code calls; 460 is the original cache size.

Result: the DGO boot's arena use fell from 10,673,456 to 10,137,296 bytes
(536,160 bytes, 90 and 532 still pass).

Building the exporter: run ninja from the 8.3 short path of `og-current`
(`C:\Users\SULLIN~1\DOCUME~1\...`) with TMP/TEMP set; from the long path, ar
fails on draco object paths longer than 260 characters.
