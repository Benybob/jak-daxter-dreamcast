/* Jak in the level ELF (AI-assisted), ISC. */
#ifndef DC_LEVEL_PLAYER_H
#define DC_LEVEL_PLAYER_H
#include "goal_runtime.h"
#include "goal_pad.h"
/* Loads eichar, calls original start 'play at the original continue point and
 * imports the target's main thread. dc_level_player_evidence.stage names the
 * step that stopped otherwise. */
int dc_level_player(dc_goal_runtime *rt);
enum {DC_LEVEL_TRACE_FRAMES=192,DC_LEVEL_WALK_FIRST=12,DC_LEVEL_WALK_FRAMES=100};
/* One frame of Jak's position, ground contact and state into dc_level_target_trace. */
void dc_level_player_sample(dc_goal_runtime *rt);
/* goal_pad.c's port state in the level ELF (-DDC_LEVEL_PAD), see goal_pad.h. */
int dc_level_pad_state(int port,dc_goal_pad_state *out);
/* The scripted walk from the trace: frames Jak spent in target-walk, his
 * horizontal travel (meters, first to farthest sample), his height range
 * against the first sample (meters), frames on a surface and walk frames on one. */
typedef struct {unsigned frames,walk_frames,onsurf_frames,walk_onsurf;float travel,below,above;} dc_level_walk;
void dc_level_player_walk(dc_level_walk *out);
#endif
