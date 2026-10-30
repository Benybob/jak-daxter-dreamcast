/* Allocation-free DRX1 and DRX2 to DRM2 face decoders. (AI-assisted), ISC. */
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

/* The same contract for a DRX2 block: 24-byte vertices, written out as faces
 * whose normals are zero. A CRC of the original DRM2 faces will not match. */
const char *dc_render_indexed_decode_draw(const void *blob, uint32_t bytes,
                                         void *out_faces, uint32_t out_capacity_faces,
                                         uint32_t *out_count);

/* Past this a vertex's index or the output bound could overflow. */
#define DC_RENDER_INDEXED_DRAW_MAX_FACES 21845u

/* Largest DRX2 block for a face count: every corner its own vertex. */
uint32_t dc_render_indexed_draw_bound(uint32_t faces);

/* The bytes indexed_sectors.encode_draw_sector writes for these 116-byte faces.
 * capacity is at least the bound. table is scratch, a power of two of at least
 * max(8, 6 * count) slots. On failure out's bytes are unspecified. No allocation. */
const char *dc_render_indexed_encode_draw(const void *faces, uint32_t count, void *out,
                                         uint32_t capacity, uint16_t *table,
                                         uint32_t table_slots, uint32_t *out_bytes);

/* Expansion with no checks and no CRC, for a block this process encoded or
 * already decoded: count*116 output bytes, not overlapping the blob. */
void dc_render_indexed_expand_draw(const void *blob, void *out_faces);

#endif
