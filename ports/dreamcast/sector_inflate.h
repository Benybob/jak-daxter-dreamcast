/* Bounded exact zlib blocks for staged sector loading (AI-assisted), ISC. */
#ifndef DC_SECTOR_INFLATE_H
#define DC_SECTOR_INFLATE_H
#include <stdint.h>
enum { DC_INFLATE_RAW_MAX=512*1024, DC_INFLATE_INPUT_MAX=512*1024+1024,
       DC_INFLATE_WORKSPACE_MAX=64*1024 };
/* NULL on success. Exactly one complete zlib stream, exact output size/CRC.
 * Buffers must be owned, valid, disjoint and nonempty. Output is STAGING memory:
 * its contents are unspecified on error and must never be an active world/layer.
 * Internal zlib allocations, including their headers, are capped at 64 KiB.
 * Per-call allocations are released before return. No retained state or file/GPU
 * operations. Callers commit a world/layer only after this and format validation. */
const char *dc_sector_inflate(const void *input,uint32_t input_bytes,
                             void *output,uint32_t output_bytes,uint32_t output_crc);
#endif
