/* Bounded immutable-file decoded block owner (AI-assisted), ISC. */
#ifndef DC_RENDER_BLOCK_CACHE_H
#define DC_RENDER_BLOCK_CACHE_H
#include "render_cache.h"
#define DC_RENDER_BLOCK_CACHE_MAX (2u*1024u*1024u)
enum { DC_RENDER_BLOCK_CACHE_SLOTS=128 };
typedef struct {void *state;} dc_render_block_cache;
typedef struct {
 uint32_t hits,misses,loads,evictions,bypasses,failures;
 uint32_t bytes,peak_bytes,blocks,metadata_bytes;
 uint32_t represented_bytes,decodes; /* retained blocks' 116-byte face payload; indexed hits */
} dc_render_block_cache_stats;
/* Zeroed/unowned output, validated catalog. Root, its files and catalog storage
 * remain immutable and alive until free. Explicit opt-in: changes to a cached
 * file are NOT detected. Do not use this owner for mutable filesystem clients.
 * budget1..MAX bounds retained raw payload bytes, excluding fixed metadata,
 * the transient reader's compressed/raw/codec allocations and geometry windows.
 * Large blocks bypass retention and are still fully read and validated.
 * No allocation/open/validation failure is remembered as an empty block.
 * Cache warming/evictions may survive a failed geometry transaction; active
 * frame/window allocations never borrow these blocks. No GPU ownership. */
const char *dc_render_block_cache_make(const dc_render_catalog *,const char *root,
    const char *source_sha,uint32_t budget,dc_render_block_cache *);
/* The same owner retaining each block as DRX1 (render_indexed.h): bytes and
 * the budget count the indexed blocks, and a hit decodes into a block of its
 * own, CRC and every index checked again, freed at release. A miss loads and
 * validates as above, then encodes; a block that will not encode or fit is
 * bypassed, never refused. Peak memory adds one decoded block, and on a miss
 * the encoder's scratch (dc_render_indexed_scratch_bytes) and output. */
const char *dc_render_block_cache_make_indexed(const dc_render_catalog *,const char *root,
    const char *source_sha,uint32_t budget,dc_render_block_cache *);
dc_render_block_source dc_render_block_cache_source(dc_render_block_cache *);
void dc_render_block_cache_measure(const dc_render_block_cache *,dc_render_block_cache_stats *);
/* One serial owner, no outstanding borrowed block when freeing. Idempotent. */
void dc_render_block_cache_free(dc_render_block_cache *);
#endif
