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

#define DC_STAGE_VRAM ((uint32_t *)0xa5400000u)  /* 32-bit VRAM, 4 MiB in */
#define DC_STAGE_VRAM_BYTES (4u * 1024u * 1024u)

int dc_stage_romdisk(void) {
  const uint32_t *source = (const uint32_t *)(const void *)romdisk_data;
  uint32_t words = ((uint32_t)romdisk_size + 3u) / 4u, i;
  if ((uintptr_t)source & 3u || (uint32_t)romdisk_size > DC_STAGE_VRAM_BYTES) return 0;
  if (fs_romdisk_unmount("/rd")) return 0;
  for (i = 0; i < words; ++i) DC_STAGE_VRAM[i] = source[i];  /* VRAM takes 32-bit writes */
  return fs_romdisk_mount("/rd", (const uint8 *)DC_STAGE_VRAM, 0) == 0;
}

/* The arena the staged romdisk leaves room for. */
uint32_t dc_kernel_arena_bytes(void) { return 27u * 512u * 1024u; }  /* 13.5 MiB; 14 starves the KOS heap */
