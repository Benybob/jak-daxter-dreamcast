/* Native capsule collision with original triangle PAT data (AI-assisted), ISC. */
#ifndef DC_WORLD_COLLISION_H
#define DC_WORLD_COLLISION_H
#include <stdint.h>
#include <stdio.h>
typedef struct {float x,y,z;} dc_vec3;
typedef struct {dc_vec3 a,b,c;uint32_t pat;} dc_world_triangle;
typedef struct dc_world_cached_triangle dc_world_cached_triangle;
typedef struct {
 dc_world_cached_triangle *triangles;
 uint32_t count;
 uint16_t candidates[1024];
 uint32_t candidate_count,distance_tests,failure,failed_triangle;
} dc_collision_world;
typedef struct {
 dc_vec3 position,normal,ground_normal;
 uint32_t contacts,grounded,ceiling,pat,ground_pat,event_bits[2],distance_tests;
} dc_collision_result;
/* Build into an empty/zeroed world. Failure leaves it unchanged. Inputs remain
 * caller-owned; the world caches validated triangle geometry, including PAT. */
const char *dc_collision_build(dc_collision_world *,const dc_world_triangle *,uint32_t);
const char *dc_collision_load(FILE *,dc_collision_world *,dc_vec3 *);
void dc_collision_free(dc_collision_world *);
/* Capsule feet at start, Y-up, height includes both spherical ends. At most
 * 8 units/displacement. Bounded candidate/solver failure leaves result intact.
 * Scratch counters in world may change. No global state, allocation or callbacks
 * during movement. Start must be nonpenetrating (the solver does not repair
 * arbitrary overlapping spawn positions). This is native prototype collision,
 * not original GOAL code. Surface mode thresholds follow pat-mode-info; ground
 * also requires positive polygon up and coverage>=0.9. Events are retained as
 * all-contact bitsets, but no damage/death response is executed here. */
int dc_collision_move(dc_collision_world *,dc_vec3,dc_vec3,float,float,dc_collision_result *);
/* Same sweep, stops at first contact. Used for placement and ground probes;
 * it does not slide the unused portion of a downward probe along the slope. */
int dc_collision_cast(dc_collision_world *,dc_vec3,dc_vec3,float,float,dc_collision_result *);
int dc_collision_clear(dc_collision_world *,dc_vec3,float,float);
#endif
