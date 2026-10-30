/* Transactional CPU geometry-window cache (AI-assisted), ISC.
 * Depends only on render_package.h and C99. No KOS, GPU, or catalog ownership.
 *
 * One serial owner calls retarget and free. No threads, callbacks, or yields.
 * The catalog, its pixel bytes, and any GPU textures stay with their owner.
 * This cache never frees or mutates them. Face bytes are private CPU copies.
 */
#ifndef DC_RENDER_CACHE_H
#define DC_RENDER_CACHE_H

#include "render_package.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Closed XZ query in meters. x0<=x1 and z0<=z1.
 * Each coordinate is finite and within +/- DC_RENDER_CACHE_COORD_MAX.
 * Each axis extent (x1-x0, z1-z0) is <= the binary64 value of the literal
 * DC_RENDER_CACHE_QUERY_MAX (decimal 36.8), plus at most 1mm absolute rounding
 * tolerance. Y is not part of the query. Inclusion is closed binary64 math;
 * no coordinate-scaled slack or invented view frustum.
 */
#define DC_RENDER_CACHE_QUERY_MAX 36.8
#define DC_RENDER_CACHE_COORD_MAX 1000000.0

enum {
    /* Closed 4 m cells and a 36.8 m query touch at most 11 indexes per axis.
     * 11*11 = 121 occupied locals. These are fail-closed backstops, not a
     * neighborhood radius. Every occupied cell whose rectangle touches is kept
     * when the touch count is within the caps. */
    DC_RENDER_CACHE_AXIS_CAP = 11,
    DC_RENDER_CACHE_LOCAL_CAP = 121,
    DC_RENDER_CACHE_ROOT_MAX = 240,
    DC_RENDER_CACHE_SHA_MAX = 64,
    DC_RENDER_CACHE_PATH_MAX = 512
};

#define DC_RENDER_CACHE_ERR_OUTPUT "render cache output"
#define DC_RENDER_CACHE_ERR_OWNED "render cache owned"
#define DC_RENDER_CACHE_ERR_ARGUMENTS "render cache arguments"
#define DC_RENDER_CACHE_ERR_QUERY "render cache query"
#define DC_RENDER_CACHE_ERR_ROOT "render cache root"
#define DC_RENDER_CACHE_ERR_SHA "render cache sha"
#define DC_RENDER_CACHE_ERR_PATH "render cache path"
#define DC_RENDER_CACHE_ERR_COVERAGE "render cache coverage"
#define DC_RENDER_CACHE_ERR_OPEN "render cache open"
#define DC_RENDER_CACHE_ERR_CLOSE "render cache close"
#define DC_RENDER_CACHE_ERR_ALLOCATION "render cache allocation"
#define DC_RENDER_CACHE_ERR_FACE_CAP "render cache face cap"
#define DC_RENDER_CACHE_ERR_REPLICA "render cache replica"
#define DC_RENDER_CACHE_ERR_ROLE "render cache replica role"
#define DC_RENDER_CACHE_ERR_MISSING "render cache replica missing"
#define DC_RENDER_CACHE_ERR_ORDER "render cache order"
#define DC_RENDER_CACHE_ERR_IDENTITY "render cache identity"

typedef struct dc_render_query {
    double x0, z0, x1, z1;
} dc_render_query;

/* CPU window. Zero-filled storage is unowned: valid==0, ready==0, count==0,
 * faces/root/source_sha NULL. A committed window has valid==1 and ready==1.
 * Known-empty is committed with count==0 and faces==NULL; root and source_sha
 * are still owned copies. coverage is the exact query that produced faces.
 *
 * faces, root, and source_sha are owned by the window. faces holds count
 * records of DC_RENDER_FACE_BYTES (116) source bytes, sorted by ascending
 * little-endian source id, with one record per id. The cache copies those
 * bytes and does not rebase, clip, or edit positions, normals, UVs, or pads.
 *
 * Face layout (little-endian), preserved verbatim:
 *   0 u32 source id, 4 u32 material id
 *   8+v*36 vertex v: f32 x,y,z, f32 u,v, f32 nx,ny,nz, 4 untouched bytes
 * A face is included when its XZ AABB touches the query on every axis.
 *
 * Package identity is source_sha + catalog CRC and counts + cell size +
 * FNV-1a identity_mix of the global entry and every sector entry + root.
 * source_sha must be exactly64 hex characters matching catalog bytes164..195.
 * A covered retarget with the same identity keeps this window byte-for-byte,
 * including the larger face set. Disk edits under an unchanged identity are
 * not detected on that reuse path.
 *
 * Paths under root, joined with '/':
 *   global.drs                         the global bucket, every retarget
 *   sectors/<ix>_<iz>.drs              decimal PRId32 ix and iz, no '+' pad
 * Root is a caller-owned NUL-terminated directory path of1..ROOT_MAX bytes;
 * relative/absolute paths, spaces and Windows drives are supported. Root is
 * not read from the packet. Every file suffix is formed only from signed
 * catalog entry indices; no packet-controlled path is accepted. Empty cells
 * are not files. A selected listed file that cannot be read fails.
 */
typedef struct dc_render_window {
    unsigned char *faces;
    char *root;
    char *source_sha;
    dc_render_query coverage;
    uint32_t count;
    uint32_t catalog_crc;
    uint32_t catalog_bytes;
    uint32_t package_bytes;
    uint32_t source_count;
    uint32_t sector_count;
    uint32_t global_count;
    uint32_t replica_count;
    uint32_t material_count;
    uint32_t texture_count;
    uint32_t texture_bytes;
    uint32_t identity_mix;
    float cell_size;
    int valid;
    int ready;
} dc_render_window;

/* Memset unowned storage to zero. NULL is ignored. An owned or inconsistent
 * window is left untouched, so init does not free or leak. */
void dc_render_window_init(dc_render_window *window);

/* Release faces, root, and source_sha when those pointers are non-NULL, then
 * zero the struct. NULL and a second free are no-ops. Idempotent on zero,
 * known-empty, and committed windows. */
void dc_render_window_free(dc_render_window *window);

/* Replace window with the geometry for query. NULL means success.
 * Argument checks run before reuse and before any file open. Reuse returns
 * NULL and leaves window unchanged when it is committed, the identity matches,
 * and the query is closed-inside coverage.
 * Any allocation, open, read, close, CRC, identity, coverage, face-cap, or
 * replica failure leaves window and its heap buffers byte-for-byte unchanged.
 * Reader errors are returned as the reader's literal. Global is read even
 * when it is empty, before a known-empty commit. Selected files are loaded
 * one at a time through dc_render_block_load. */
const char *dc_render_window_retarget(const dc_render_catalog *catalog,
                                      const char *root,
                                      const char *source_sha,
                                      const dc_render_query *query,
                                      dc_render_window *window);

/* Explicit validated-block source. One serial load/release pair at a time.
 * load must return complete dc_render_block_load-validated records for this
 * exact catalog entry/path, including EOF, compressed/raw CRC and close checks.
 * Failure leaves block zero. Successful blocks are borrowed until release;
 * release clears block. Neither callback mutates catalog/query/old window.
 * The source and its immutable filesystem identity remain alive for the call.
 * The normal retarget above always uses direct FILE reads, without this source.
 * All source-ID, replica/role, full-payload and coverage checks still run. */
typedef struct {
    void *user;
    const char *(*load)(void *,const dc_render_catalog *,const dc_render_entry *,
                        int global,const char *path,dc_render_block *);
    void (*release)(void *,dc_render_block *);
    /* Optional. Nonzero lends this entry's u32 source IDs in place, every stride
     * bytes, for the pass that reads nothing else; the IDs stay valid until the
     * next call on the source. Zero falls back to load, with nothing changed. */
    int (*peek)(void *,const dc_render_catalog *,const dc_render_entry *,int global,
                const unsigned char **ids,uint32_t *stride);
} dc_render_block_source;
const char *dc_render_window_retarget_source(const dc_render_catalog *,const char *,
    const char *,const dc_render_query *,const dc_render_block_source *,dc_render_window *);

#ifdef __cplusplus
}
#endif

#endif
