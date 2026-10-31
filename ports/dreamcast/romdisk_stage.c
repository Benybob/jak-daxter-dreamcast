/* VRAM-staged romdisk for the DGO boot (AI-assisted), ISC.
 *
 * KOS clears all of VRAM (vid_init) and sound RAM (spu_init) before main, so
 * neither can be filled by the ELF loader. The romdisk is instead linked high in
 * main RAM (.dc_stage, see the Makefile), above anything KOS init allocates. The
 * first thing main does is copy it into idle VRAM and remount /rd there; its
 * main-RAM pages are then free for the heap, so the GOAL arena can be larger.
 * This stands in for streaming from the GD-ROM, which keeps no copy in main RAM.
 * VRAM is idle in this test (no PVR renderer runs). */
#include <kos.h>
#include <stdint.h>

extern const unsigned char romdisk_data[];
extern const int romdisk_size;

/* 32-bit VRAM: the framebuffer below 1 MiB, the GOAL extension region (PS2 DMA
 * buffers, kernel_boot_cases.c) from 1 to 6 MiB, the romdisk from 6 to 8 MiB. */
#define DC_STAGE_VRAM ((uint32_t *)0xa5600000u)
#define DC_STAGE_VRAM_BYTES (2u * 1024u * 1024u)

/* Pointer staging also serves the PVR-owned loans (AI-assisted), ISC. */
int dc_stage_romdisk_at(void *memory) {
  volatile uint32_t *destination = memory;
  const uint32_t *source = (const uint32_t *)(const void *)romdisk_data;
  uint32_t words = ((uint32_t)romdisk_size + 3u) / 4u, i;
  if (!memory || ((uintptr_t)source | (uintptr_t)memory) & 3u ||
      (uint32_t)romdisk_size > DC_STAGE_VRAM_BYTES) return 0;
  if (fs_romdisk_unmount("/rd")) return 0;
  for (i = 0; i < words; ++i) destination[i] = source[i];  /* VRAM takes 32-bit writes */
  return fs_romdisk_mount("/rd", (const uint8 *)memory, 0) == 0;
}

int dc_stage_romdisk(void) {
#ifdef DC_ARENA_COEXIST
  int dc_arena_stage(void);
  return dc_arena_stage();
#else
  return dc_stage_romdisk_at(DC_STAGE_VRAM);
#endif
}

/* The arena the staged romdisk leaves room for. */
/* 13 MiB: the arena holds 10.7 MB of GAME.CGO; the KOS heap needs the rest of main
 * RAM while objects load, now that the ELF carries the native mips2c code. */
uint32_t __attribute__((weak)) dc_renderer_reserve(void) { return 0; }
uint32_t dc_kernel_arena_bytes(void) { return 13u * 1024u * 1024u - dc_renderer_reserve(); }
