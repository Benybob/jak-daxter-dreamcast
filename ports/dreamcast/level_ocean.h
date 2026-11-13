/* The level ELF's ocean surface (AI-assisted), ISC.
 *
 * The PS2 ocean (ocean-mid, ocean-near, ocean-transition, the rendered ocean
 * texture) is VU1 programs fed by DMA. This is a surface of its own from the
 * same map: a disc of rings around the eye out to 3 km at *ocean-map*'s
 * start-corner y, coloured per vertex from the map's 96 m colour grid
 * (ocean-colors, 52 a row), under a mipmapped wave texture made here that
 * drifts with the frame count. No waves, no shore masks (land covers it by
 * depth), no time of day. Nothing is drawn while *ocean-map* is #f. */
#ifndef DC_LEVEL_OCEAN_H
#define DC_LEVEL_OCEAN_H
#include <stdint.h>
#include "goal_runtime.h"
#include "instance_pass.h"
typedef struct {uint32_t frames,drawn,triangles,submitted,texture_bytes,map;float height;} dc_level_ocean_metrics_t;
extern volatile dc_level_ocean_metrics_t dc_level_ocean_metrics;
/* Between pvr_list_begin(PVR_LIST_OP_POLY) and its finish. eye in meters.
 * 1, or 0 for geometry the clipper refused. */
int dc_level_ocean_draw(dc_goal_runtime *rt,const dc_instance_camera *cam,const float eye[3]);
#endif
