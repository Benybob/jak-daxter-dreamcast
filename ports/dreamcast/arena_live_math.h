/* Original affine camera validation (AI-assisted), ISC. */
#ifndef DC_ARENA_LIVE_MATH_H
#define DC_ARENA_LIVE_MATH_H
#include "arena_game.h" /* Layout constants and renderer camera value only. */
#include "instance_pass.h"
typedef struct {float trans[4],inv[16],rot[16];} dc_live_pose;
int dc_live_bounded(float,float);
int dc_live_convert(const dc_live_pose *,float,dc_game_camera *);
int dc_live_read(dc_goal_runtime *,uint32_t,uint32_t,dc_live_pose *);
/* Diagnostics only: qNaN in EVERY consumed lane before each original call.
 * No finite camera pose is ever written by this helper. */
int dc_live_poison(dc_goal_runtime *,uint32_t,uint32_t,uint32_t);
int dc_live_terrain_matches(const dc_live_pose *,const dc_render_draw_window *);
int dc_live_instance_matches(const dc_live_pose *,const dc_instance_camera *);
int dc_live_query_matches(const dc_live_pose *,const dc_render_query *,float);
int dc_live_camera_matches(const dc_live_pose *,const dc_game_camera *);
/* 0 invalid, 1 stable, 2 translation changed, 4 orientation changed (ORed). */
unsigned dc_live_delta(const dc_live_pose *,const dc_live_pose *);
int dc_live_turn_matches(float,float);
#endif
