/* Background base-pass submission, independent resident textures (AI-assisted). */
#ifndef DC_RENDER_BASE_H
#define DC_RENDER_BASE_H
#include "render_gpu.h"
#include "render_world_view.h"
typedef struct {uint32_t flags,list;} dc_render_base_material;
typedef struct {
 pvr_poly_hdr_t *headers;dc_render_base_material *materials;
 uint32_t count,catalog_crc,catalog_bytes,secondary_materials;
 unsigned char source_sha256[32];
} dc_render_base_bank;
typedef struct {
 dc_render_vertex *vertices;uint16_t *order;
 const unsigned char *faces;
 uint32_t first[257],count,materials,catalog_crc,ready,valid;
} dc_render_base_frame;
typedef struct {uint32_t faces,clipped,culled,triangles,invalid,near_clipped;} dc_render_base_stats;
/* Normal KOS FPU ABI; all owners zero-initialized. Bank borrows GPU texture
 * addresses but owns only its CPU header/metadata arrays. GPU textures must
 * outlive every submitted borrowing frame. Opaque and texture-baked MASK base
 * passes use RGB modulation and resident 1-bit alpha; absent-texture MASK is
 * refused explicitly. This is the BASE pass, not an environment/reflection/TOD
 * shader. Secondary inputs stay in the catalog/GPU owner, counted below.
 * Failed construction leaves output unchanged. Never free while PVR borrows. */
const char *dc_render_base_bank_make(const dc_render_catalog *,const dc_render_gpu *,dc_render_base_bank *);
void dc_render_base_bank_free(dc_render_base_bank *);
/* Frame owns view cache and material-group indices; borrows immutable packed
 * window faces. Build a DIFFERENT candidate cache/frame pair before replacing
 * either live owner. Source struct may move, face allocation must stay alive.
 * Failed construction frees only candidate CPU allocations. */
const char *dc_render_base_frame_make(const dc_render_window *,const dc_render_base_bank *,dc_render_base_frame *);
void dc_render_base_frame_free(dc_render_base_frame *);
/* Failure invalidates readiness; draw cannot silently use a stale camera. */
int dc_render_base_prepare(dc_render_base_frame *,const dc_render_window *,const dc_render_camera *);
/* Inside the matching OP/PT PVR list. No allocations/yields/callbacks; stats
 * accumulate. Clips to the complete viewport before perspective submission.
 * Caller waits for the last borrowed frame before releasing any owner. */
int dc_render_base_draw(const dc_render_base_bank *,const dc_render_base_frame *,int,dc_render_base_stats *);
#endif
