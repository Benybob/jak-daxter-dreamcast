/* Portable camera-space near-plane clipping (AI-assisted), ISC. */
#ifndef DC_SCENE_CLIP_H
#define DC_SCENE_CLIP_H
#include "scene_projection.h"
#include <stdint.h>
typedef struct {float x,y,z,u,v;uint32_t argb;} dc_clip_vertex;
/* Trusted nonoverlapping arrays, input3/output4. Returns 0..4 polygon vertices
 * (0 means fully clipped/degenerate), -1 invalid input with output unchanged.
 * Interpolates raw UV/color in view space before perspective division. Keeps
 * winding and original order, no allocation, fixed near plane DC_SCENE_NEAR. */
int dc_scene_clip_near(const dc_clip_vertex input[3],dc_clip_vertex output[4]);
#endif
