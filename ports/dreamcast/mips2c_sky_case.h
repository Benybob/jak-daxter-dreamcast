/* One mips2c differential case, shared by the host oracle and the Flycast boot
 * (AI-assisted), ISC.
 *
 * The sky pipeline from sky_tng.cpp: init-sky-regs (reads *math-camera*, writes
 * *sky-tng-data*), set-sky-vf27, set-sky-vf23-value, then render-sky-tri, which
 * clips the triangle through draw-large-polygon and the hyperplane helpers into
 * a dma-buffer, using the fake scratchpad as work memory. Every input lives in
 * one region at a caller-chosen GOAL address, so the host (g_ee_main_mem + offset)
 * and the Dreamcast (absolute P0 addresses) run the same bytes. Pointers stored
 * in the region are written as base + offset and normalized back to offsets
 * before the region is hashed. */
#ifndef DC_MIPS2C_SKY_CASE_H
#define DC_MIPS2C_SKY_CASE_H
#include <stdint.h>
#include <string.h>

enum {
  DC_SKY_REGION = 0x8000,
  DC_SKY_CAMERA = 0x0004,  /* *math-camera* basic (16-aligned fields read to 848) */
  DC_SKY_DATA = 0x0404,    /* *sky-tng-data* basic */
  DC_SKY_VF27 = 0x0600,    /* set-sky-vf27's vector */
  DC_SKY_VERTS = 0x0700,   /* three sky-vertex (pos, stq, col) */
  DC_SKY_DMA = 0x0804,     /* dma-buffer object: its type word at 0x800, data 16-aligned at 0x810 */
  DC_SKY_DMA_BYTES = 0x1000,
  DC_SKY_SPAD = 0x4000,    /* *fake-scratchpad-data*: 16 KiB of work memory */
  DC_SKY_VF23 = 0x12345678
};

static inline void dc_sky_put_f(unsigned char *r, uint32_t at, float v) { memcpy(r + at, &v, 4); }
static inline void dc_sky_put_u(unsigned char *r, uint32_t at, uint32_t v) { memcpy(r + at, &v, 4); }
static inline void dc_sky_vec(unsigned char *r, uint32_t at, float x, float y, float z, float w) {
  dc_sky_put_f(r, at, x); dc_sky_put_f(r, at + 4, y); dc_sky_put_f(r, at + 8, z); dc_sky_put_f(r, at + 12, w);
}

/* A plausible camera: init-sky-regs reads four matrix rows (572..620), the
 * scale vector (700), offset (716), hvdf (732), fog (828), and 124/128/848. */
static inline void dc_sky_fill(unsigned char *r, uint32_t base) {
  memset(r, 0, DC_SKY_REGION);
  dc_sky_vec(r, DC_SKY_CAMERA + 572, 1.0f, 0.0f, 0.0f, 0.0f);
  dc_sky_vec(r, DC_SKY_CAMERA + 588, 0.0f, 1.0f, 0.0f, 0.0f);
  dc_sky_vec(r, DC_SKY_CAMERA + 604, 0.0f, 0.0f, 1.0f, 0.25f);
  dc_sky_vec(r, DC_SKY_CAMERA + 620, 0.0f, 0.0f, -8.0f, 1.0f);
  dc_sky_vec(r, DC_SKY_CAMERA + 700, 256.0f, 224.0f, 1.0f, 1.0f);
  dc_sky_vec(r, DC_SKY_CAMERA + 716, 0.0f, 0.0f, 0.0f, 0.0f);
  dc_sky_vec(r, DC_SKY_CAMERA + 732, 2048.0f, 2048.0f, 16777000.0f, 1.0f);
  dc_sky_put_f(r, DC_SKY_CAMERA + 124, 0.5f);
  dc_sky_put_f(r, DC_SKY_CAMERA + 128, 0.75f);
  dc_sky_put_f(r, DC_SKY_CAMERA + 828, 3.0f);
  dc_sky_put_f(r, DC_SKY_CAMERA + 848, 1.0f);
  dc_sky_vec(r, DC_SKY_VF27, 0.125f, 0.25f, 0.5f, 1.0f);
  /* One vertex behind the near plane, so the clipping helpers run. */
  dc_sky_vec(r, DC_SKY_VERTS + 0, -1.0f, -1.0f, 4.0f, 1.0f);
  dc_sky_vec(r, DC_SKY_VERTS + 16, 0.0f, 0.0f, 1.0f, 0.0f);
  dc_sky_vec(r, DC_SKY_VERTS + 32, 128.0f, 64.0f, 32.0f, 128.0f);
  dc_sky_vec(r, DC_SKY_VERTS + 48, 3.0f, -1.0f, 5.0f, 1.0f);
  dc_sky_vec(r, DC_SKY_VERTS + 64, 1.0f, 0.0f, 1.0f, 0.0f);
  dc_sky_vec(r, DC_SKY_VERTS + 80, 64.0f, 128.0f, 32.0f, 128.0f);
  dc_sky_vec(r, DC_SKY_VERTS + 96, 0.5f, 2.0f, -3.0f, 1.0f);
  dc_sky_vec(r, DC_SKY_VERTS + 112, 0.0f, 1.0f, 1.0f, 0.0f);
  dc_sky_vec(r, DC_SKY_VERTS + 128, 32.0f, 64.0f, 128.0f, 128.0f);
  /* dma-buffer: allocated-length, base (first unused), end, then data. */
  dc_sky_put_u(r, DC_SKY_DMA + 0, DC_SKY_DMA_BYTES);
  dc_sky_put_u(r, DC_SKY_DMA + 4, base + DC_SKY_DMA + 12);
  dc_sky_put_u(r, DC_SKY_DMA + 8, base + DC_SKY_DMA + 12 + DC_SKY_DMA_BYTES);
}

/* Pointer fields back to region offsets; returns the bytes render-sky-tri added. */
static inline uint32_t dc_sky_normalize(unsigned char *r, uint32_t base) {
  uint32_t ptr, end;
  memcpy(&ptr, r + DC_SKY_DMA + 4, 4); memcpy(&end, r + DC_SKY_DMA + 8, 4);
  ptr -= base; end -= base;
  dc_sky_put_u(r, DC_SKY_DMA + 4, ptr); dc_sky_put_u(r, DC_SKY_DMA + 8, end);
  return ptr - (DC_SKY_DMA + 12);
}

static inline uint32_t dc_sky_crc32(const unsigned char *p, uint32_t n) {
  uint32_t crc = 0xffffffffu, i, k;
  for (i = 0; i < n; ++i) {
    crc ^= p[i];
    for (k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
#endif
