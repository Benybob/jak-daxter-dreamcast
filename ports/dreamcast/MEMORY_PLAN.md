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
  from 12 to 13.5 MiB; 14 MiB starves the KOS heap. This stands in for streaming from
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
