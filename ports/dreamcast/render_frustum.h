/* Full viewport clipping of world-view triangles (AI-assisted), ISC. */
#ifndef DC_RENDER_FRUSTUM_H
#define DC_RENDER_FRUSTUM_H
#include "scene_clip.h"
enum {DC_RENDER_CLIP_CAP=8};
/* Normal C FPU ABI. Trusted disjoint input3/output8 arrays, no allocation.
 * View positions finite +/-4e6, UV finite +/-65536. Closed near and viewport
 * planes match x=320+390*x/z,y=260-390*y/z at640x480. Returns0..8 or-1 for
 * invalid input; error/fully-clipped output unchanged. Keeps winding/UV/color.
 * Long edge intersections use binary64, round each final attribute once.
 * Does not implement material alpha, reflection or scene-depth/far clipping. */
int dc_render_clip_viewport(const dc_clip_vertex input[3],dc_clip_vertex output[DC_RENDER_CLIP_CAP]);
#endif
