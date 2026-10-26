/* Packed world geometry -> SH4ZAM view cache (AI-assisted), ISC. */
#ifndef DC_RENDER_WORLD_VIEW_H
#define DC_RENDER_WORLD_VIEW_H
#include "render_cache.h"
#include "scene_projection.h"

/* World meters. The common actor/terrain renderer uses four meters per view
 * unit, including DC_SCENE_NEAR (0.2 world meters). Target subtraction happens
 * BEFORE the rotation, avoiding cancellation of large matrix translations. */
typedef struct {
 dc_view_vertex target;
 float yaw,pitch,distance;
} dc_render_camera;
#define DC_RENDER_WORLD_SCALE 0.25f

/* Trusted immutable committed window from dc_render_window_retarget; input
 * faces are exact validated 116-byte records. out is a disjoint, 4-byte aligned
 * caller-owned array with capacity in VERTICES, >=window->count*3. Each face's
 * three vertices occupy out[face*3+vertex]. No XYZ deinterleave buffer, allocation,
 * callbacks or yields. Caller keeps both window and output alive while drawing.
 * Camera target axes are finite +/-1e6 m, yaw/pitch finite, distance0..1e6 m.
 * Empty committed windows allow NULL output/capacity0 and perform no matrix work.
 * Returns1 on success; invalid args or SR.FD/FPSCR.PR/SZ/FR return0 without output
 * writes. Normal KOS scalar FPU ABI only. Preserves caller XMTRX bit patterns and
 * FPU control mode; arithmetic exception/status flags are not preserved.
 * Near/behind projected fields are zero, view fields retained for clipping.
 * This transforms the whole selected cache; it does not cull or drop materials. */
int dc_render_world_prepare(const dc_render_window *,dc_render_vertex *,uint32_t,
                            const dc_render_camera *);
#endif
