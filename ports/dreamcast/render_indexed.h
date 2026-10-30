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

/* Lossless DRM2 to DRX1, byte-identical to indexed_sectors.encode_sector.
 * scratch: at least dc_render_indexed_scratch_bytes(face_count) bytes, 4-byte
 * aligned, contents ignored. out NULL with capacity 0 only measures: *out_bytes
 * gets the exact block size. Faces are trusted to be whole 116-byte records;
 * nonfinite vertices and more than 65,535 distinct vertices are refused.
 * Failure leaves out and out_bytes unchanged. No allocation or I/O. */
enum { DC_RENDER_INDEXED_MAX_VERTICES = 65535,
       DC_RENDER_INDEXED_MAX_FACES = 1u << 24 };
uint32_t dc_render_indexed_scratch_bytes(uint32_t face_count);
const char *dc_render_indexed_encode(const void *faces, uint32_t face_count,
                                    void *scratch, uint32_t scratch_bytes,
                                    void *out, uint32_t capacity, uint32_t *out_bytes);

#endif
