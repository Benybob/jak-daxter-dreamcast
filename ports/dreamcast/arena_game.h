/* Original math-camera readback (AI-assisted), ISC. */
#ifndef DC_ARENA_GAME_H
#define DC_ARENA_GAME_H
#include "goal_runtime.h"
#include "render_draw_window.h"
enum {DC_GAME_CAMERA_ROT=364,DC_GAME_CAMERA_INV=428,DC_GAME_CAMERA_TRANS=844,
      DC_GAME_CAMERA_BYTES=1060}; /* raw allocation size, including basic tag */
typedef struct {float trans[4],inv[16];} dc_game_camera_pose;
typedef struct {float eye[3],target[3],yaw,pitch,distance;} dc_game_camera;
/* Independent GOAL-basis oracle over the actual prepared terrain vertices. */
int dc_arena_game_terrain_matches(const dc_game_camera_pose *,const dc_render_draw_window *);
int dc_arena_game_convert(const dc_game_camera_pose *,float,dc_game_camera *);
int dc_arena_game_read(dc_goal_runtime *,uint32_t,uint32_t,dc_game_camera_pose *);
int dc_arena_game_write(dc_goal_runtime *,uint32_t,uint32_t,const dc_game_camera_pose *);
#ifdef DC_ARENA_GAME
void dc_arena_game_bind(dc_goal_runtime *);
void dc_arena_game_report(char *,size_t,uint32_t,uint32_t);
#endif
#endif
