/* Bounded first-hit sphere camera boom (AI-assisted proposal), ISC.
 * A pure world query. No smoothing, view transform, or clipping. */
#ifndef DC_CAMERA_BOOM_H
#define DC_CAMERA_BOOM_H

#include "world_collision.h"

/* Collision local meters. The probe is a sphere, not a GOAL camera volume.
 * Height is exactly two radii, so both capsule ends sit on the center. */
#define DC_CAMERA_BOOM_RADIUS 0.12f
#define DC_CAMERA_BOOM_HEIGHT (2.f * DC_CAMERA_BOOM_RADIUS)
#define DC_CAMERA_BOOM_MIN_METERS 0.3f
#define DC_CAMERA_BOOM_MAX_METERS 12.f
/* Cast rejects squared displacement above 64. Unit*8 can cross that line. */
#define DC_CAMERA_BOOM_SEGMENT_MAX 7.9f
/* Inset a first-hit stop. The cast has already stopped on its own shell. */
#define DC_CAMERA_BOOM_CONTACT_MARGIN 0.002f
/* |length - 1| above this rejects. Near-unit inputs are normalized. */
#define DC_CAMERA_BOOM_DIRECTION_TOLERANCE 0.001f

/* On success writes *clear_meters in [0, desired_meters] and returns 1.
 * The length is how far the sphere center may travel from target_center
 * along direction. Collision positions are feet, one radius below that center.
 * On failure returns 0 and leaves *clear_meters unchanged.
 * Scratch counters in world may change once clear or cast runs. */
int dc_camera_boom_query(dc_collision_world *world, dc_vec3 target_center,
                         dc_vec3 direction, float desired_meters,
                         float *clear_meters);

#endif
