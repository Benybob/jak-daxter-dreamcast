/* Native prototype collision-camera state, collision-local meters (AI-assisted), ISC. */
#ifndef DC_CAMERA_RIG_H
#define DC_CAMERA_RIG_H
#include "camera_boom.h"
#define DC_CAMERA_PITCH (-0.5f)
#define DC_CAMERA_DESIRED 10.4f
typedef struct {
 dc_vec3 target,direction,eye;
 float distance,limit;
 uint32_t initialized,shortened,fallbacks,coverage_limited;
} dc_camera_rig;
/* Zero-initialized first use. Preferred floor may lag actual feet while jumping.
 * Queries the new ray, immediately shortens, eases extension <=6m/sec and caps
 * every frame by the fresh query. Failed preferred target gets one independently
 * checked actual-height alternative. Failure leaves rig unchanged, not world
 * scratch. No allocation, no original GOAL camera or full-frustum guarantee. */
int dc_camera_rig_update(dc_camera_rig *,dc_collision_world *,dc_vec3 half_extent,dc_vec3 player_feet,
                          float preferred_floor,float yaw,float dt);
#endif
