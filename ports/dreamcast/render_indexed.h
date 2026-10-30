/* Allocation-free DRX1 to DRM2 face decoder. (AI-assisted), ISC. */
#ifndef DC_RENDER_INDEXED_H
#define DC_RENDER_INDEXED_H

#include <stdint.h>

/* blob is a complete little-endian DRX1 block. Output capacity is measured in
 * 116-byte faces, not bytes. Caller supplies valid buffers of the stated sizes
 * and a writable out_count. Unaligned blob and out_faces are supported.
 * Input, written output and out_count must not overlap (checked).
 * Every check precedes every write: failure leaves output/count unchanged.
 * Empty blocks may use NULL out_faces and zero capacity. No allocation or I/O.
 * Success returns NULL and sets out_count, including zero for an empty block.
 */
const char *dc_render_indexed_decode(const void *blob, uint32_t bytes,
                                    void *out_faces, uint32_t out_capacity_faces,
                                    uint32_t *out_count);

#endif
