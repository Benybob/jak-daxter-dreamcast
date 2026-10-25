/* Resident PowerVR color catalog, separate from geometry (AI-assisted), ISC. */
#ifndef DC_RENDER_GPU_H
#define DC_RENDER_GPU_H
#include <kos.h>
#include "render_package.h"
typedef struct {
 pvr_ptr_t *slots;
 uint32_t count,colors,bytes,catalog_crc;
 unsigned char source_sha256[32];
} dc_render_gpu;
/* PVR initialized and idle; zeroed/unowned output and immutable validated CPU
 * catalog. Copies all ARGB1555 maps (base AND specular color) to resident VRAM.
 * U8 strength slots stay NULL; those inputs remain in the CPU catalog for a
 * future reflection shader. No geometry, palette, frame or material mutation.
 * On failure out and all other owners remain unchanged; partial uploads freed.
 * Source CPU pixels are neither consumed nor borrowed after return. */
const char *dc_render_gpu_make(const dc_render_catalog *,dc_render_gpu *);
/* Caller waits for the PVR frame that last borrowed these textures. Then frees
 * GPU pixels/slot array, never CPU catalog or geometry. NULL/idempotent safe.
 * Stage a replacement in ANOTHER empty owner; only free old after new success. */
void dc_render_gpu_free(dc_render_gpu *);
#endif
