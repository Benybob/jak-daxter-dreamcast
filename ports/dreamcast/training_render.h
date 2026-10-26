/* Bounded prototype world-render lifetime/retarget owner (AI-assisted), ISC. */
#ifndef DC_TRAINING_RENDER_H
#define DC_TRAINING_RENDER_H
#include "render_base.h"
typedef struct {
 dc_render_catalog catalog;dc_render_gpu gpu;dc_render_base_bank bank;
 dc_render_window window;dc_render_base_frame frame;
 char root[DC_RENDER_CACHE_ROOT_MAX+1],source_sha[65];
 uint32_t initialized,commits,refusals;
} dc_training_render;
/* Initialized idle PVR, zeroed output. Root/source immutable during the owner.
 * Texture bank is resident once; geometry/frame candidates have separate lives.
 * Failure leaves output untouched. Caller waits for PVR before free. */
const char *dc_training_render_make(const char *,const char *,dc_training_render *);
void dc_training_render_free(dc_training_render *);
/* Temporary bounded terrain selection, NOT a full-frustum coverage guarantee.
 * A 32m square around target must fit the loaded36.8m square. New anchors snap
 * to the nearest4m grid. Stage full cache+frame before PVR wait/commit/free-old.
 * Synchronous IO may hitch; no scheduling or async guarantee. On failure old
 * owners remain intact and refusals increments; caller must halt before drawing
 * a camera whose required data is missing. Normal KOS FPU ABI, one serial owner. */
const char *dc_training_render_update(dc_training_render *,const dc_render_camera *);
#endif
