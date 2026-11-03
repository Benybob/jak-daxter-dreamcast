/* Original camera/renderer integration and evidence ABI (AI-assisted), ISC. */
#ifndef DC_ARENA_LIVE_H
#define DC_ARENA_LIVE_H
#include "arena_live_math.h"
#include "goal_module.h"
#include "arena_camera.h" /* Process diagnostic record type only. */
#include "goal_pad.h"
enum {DC_LIVE_UPDATES=181,DC_LIVE_DISPATCHES=185};
typedef struct {
 uint32_t sequence,dispatch,stage,pad_services,master_resumed,slave_resumed,combiner_resumed,delta;
 dc_live_pose pose;
 uint32_t drawn;
 float target[3],yaw,pitch,distance,half,draw_distance;
 /* Ratio consumed by the slave producing this output, before frame-start
  * computes the NEXT ratio. Version 3 appends it without moving pose lanes. */
 float time_adjust_ratio;
} dc_live_trace;
typedef struct {
 uint32_t version,producer,reason,dispatches,update_calls,update_returns;
 uint32_t poison_batches,witnessed_updates,pose_changes,camera;
 uint32_t prevent_mask,selected_pad,pad_valid,pad_services,valid_services;
 uint32_t read_analog,read_buttons,master_mode,time_ratio_bits,registered_slave;
 uint32_t runtime_error,error_site;
 dc_arena_camera_process master,slave,combiner;
 uint32_t poisoned_lanes,affine_reads,rendered,warmup,init_events,init_verified;
 uint32_t neutral_pairs,moving_pairs,turning_pairs,position_changes,orientation_changes;
 uint32_t pad_stage,selected_services_before_update,original_update,shim,restored;
 dc_live_pose initial;
 dc_live_trace trace[DC_LIVE_UPDATES];
} dc_live_evidence;
extern volatile dc_live_evidence dc_arena_live_evidence;
int dc_arena_live_begin(dc_goal_runtime *,dc_goal_cache_sync);
int dc_arena_live_initialize(dc_goal_runtime *);
void dc_arena_live_resume(dc_goal_runtime *,uint32_t,int);
void dc_arena_live_pad(dc_goal_runtime *,uint32_t);
int dc_arena_live_frame(dc_goal_runtime *);
int dc_arena_live_finish(dc_goal_runtime *);
void dc_arena_live_report(char *,size_t,uint32_t,uint32_t);
int dc_arena_live_check(uint64_t,uint64_t,const char *);
/* Returns 1 with a fresh validated update, 0 during four explicit warmup
 * dispatches, -1 on missing/duplicate/stale/changed camera. */
int dc_arena_live_sample(dc_live_pose *,dc_game_camera *);
int dc_arena_live_drawn(const dc_game_camera *,float,float);
/* Fixed synthetic input at the existing hardware service boundary. */
unsigned dc_live_input_stage(unsigned);
int dc_live_recorded_pad(unsigned,int,dc_goal_pad_state *);
void dc_live_input_advance(unsigned);
int dc_arena_live_pad_state(int,dc_goal_pad_state *);
/* Fixture is used once as an event argument; never a per-frame producer. */
int dc_live_initial_transform(float,float,const float [3],float,float [8]);
int dc_live_initial_matches(const dc_live_pose *,const float [8]);
int dc_arena_live_initial_transform(float [8]);
#endif
