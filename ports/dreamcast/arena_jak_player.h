/* Original target execution receipt, separate from camera proof (AI-assisted), ISC. */
#ifndef DC_ARENA_JAK_PLAYER_H
#define DC_ARENA_JAK_PLAYER_H
#include "arena_live.h"
enum {DC_JAK_PLAYER_FRAMES=160};
typedef struct {
 uint32_t sequence,state,status,resumes,read_calls,pad_services;
 int32_t joyx;uint32_t buttons;
 float trans[3],magnitude;
} dc_jak_player_frame;
typedef struct {
 uint32_t version,active,phase,failure,frames,pair_available,pair_shortfall,sidekick_loaded;
 uint32_t target,thread,init_entry,read_entry,read_shim,read_calls,read_returns;
 uint32_t attempts,resumes,pad_services,pad_valid,control,cpad,state_changes;
 uint32_t moving_reads,neutral_reads,moving_changes,neutral_stable,restored,error,site;
 uint32_t sidekick_available,sidekick_shortfall;
 dc_jak_player_frame trace[DC_JAK_PLAYER_FRAMES];
} dc_jak_player_evidence;
extern volatile dc_jak_player_evidence dc_arena_jak_player_evidence;
int dc_arena_jak_player_run(dc_goal_runtime *,uint32_t);
int dc_arena_jak_player_active(void);
int dc_arena_jak_player_pad_state(int,dc_goal_pad_state *);
void dc_arena_jak_player_resume(dc_goal_runtime *,uint32_t,int);
void dc_arena_jak_player_pad(dc_goal_runtime *,uint32_t);
void dc_arena_jak_player_report(char *,size_t);
#endif
