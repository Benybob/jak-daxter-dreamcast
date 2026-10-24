/* Native movement on original/static collision triangles (AI-assisted), ISC. */
#ifndef DC_PLAYER_WORLD_MOTION_H
#define DC_PLAYER_WORLD_MOTION_H
#include "player_motion.h"
#include "world_collision.h"
/* Atomic player commit; collision scratch may change on refusal. Placement
 * sweeps down four units from caller's nonoverlapping starting capsule. */
int dc_player_world_place(dc_player *,dc_collision_world *,dc_vec3);
int dc_player_world_tick(dc_player *,dc_collision_world *,float,float,int,dc_collision_result *);
#endif
