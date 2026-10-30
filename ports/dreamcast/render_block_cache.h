/* Bounded immutable-file decoded block owner (AI-assisted), ISC. */
#ifndef DC_RENDER_BLOCK_CACHE_H
#define DC_RENDER_BLOCK_CACHE_H
#include "render_cache.h"
#include "render_draw_window.h"
#define DC_RENDER_BLOCK_CACHE_MAX (2u*1024u*1024u)
enum { DC_RENDER_BLOCK_CACHE_SLOTS=128 };
typedef struct {void *state;} dc_render_block_cache;
/* Called with a missed .drx held in full, if set: a sampling point for tests. */
extern void (*dc_render_block_cache_probe)(void);
typedef struct {
 uint32_t hits,misses,loads,evictions,bypasses,failures;
 uint32_t bytes,peak_bytes,blocks,metadata_bytes;
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
/* The same owner retaining DRX2 blocks: budget bounds their bytes, so about
 * three times the geometry fits. Every block this source lends has zero normals,
 * on a miss too, so replicas keep matching; a window's CRC differs from the
 * package's. A hit lends faces singly and allocates nothing. A miss reads the
 * block's .drx beside its .drs if it is there, whole and validated, and needs no
 * encoder; a .drx that cannot be used or kept falls back to the .drs, which then
 * has to exist. Without one a miss reads the .drs and encodes it, and failing to
 * build a retained block only leaves it uncached. */
const char *dc_render_block_cache_make_draw(const dc_render_catalog *,const char *root,
    const char *source_sha,uint32_t budget,dc_render_block_cache *);
/* Build window for query from a make_draw owner's blocks, reading any missing
 * .drx (there is no .drs fallback here). Faces come out in source-ID order, each
 * once; replicas must match byte for byte and a global face may have none. Every
 * block is held resident for the build, so budget must cover them together. All
 * checks precede the first write to its faces and vertices: failure leaves those,
 * and its readiness, as they were. A query inside the window's coverage changes
 * nothing. One hit a resident block. */
const char *dc_render_block_cache_window(dc_render_block_cache *,const dc_render_catalog *,
    const dc_render_query *,dc_render_draw_window *);
dc_render_block_source dc_render_block_cache_source(dc_render_block_cache *);
void dc_render_block_cache_measure(const dc_render_block_cache *,dc_render_block_cache_stats *);
/* One serial owner, no outstanding borrowed block when freeing. Idempotent. */
void dc_render_block_cache_free(dc_render_block_cache *);
#endif
