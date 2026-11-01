/* Original camera boot probe (AI-assisted), ISC. */
#ifndef DC_ARENA_CAMERA_H
#define DC_ARENA_CAMERA_H
#include "goal_module.h"

typedef struct {
  uint32_t process,status,state,mask,attempts,resumed,free_frames;
} dc_arena_camera_process;
typedef struct {
  uint32_t version,producer,reason,dispatches,update_calls,update_returns;
  uint32_t diagnostic_seeds,original_wrote_camera,pose_changes,camera;
  uint32_t prevent_mask,selected_pad,pad_valid,pad_services,valid_services;
  uint32_t read_analog,read_buttons,master_mode,time_ratio_bits,registered_slave;
  uint32_t runtime_error,error_site;
  dc_arena_camera_process master,slave,combiner;
} dc_arena_camera_metrics_type;
extern volatile dc_arena_camera_metrics_type dc_arena_camera_metrics;
/* These hooks exist only in separately compiled DC_ARENA_CAMERA objects. */
int dc_arena_camera_begin(dc_goal_runtime *,dc_goal_cache_sync);
void dc_arena_camera_resume(dc_goal_runtime *,uint32_t,int);
void dc_arena_camera_pad(dc_goal_runtime *,uint32_t);
int dc_arena_camera_frame(dc_goal_runtime *);
int dc_arena_camera_finish(dc_goal_runtime *);
void dc_arena_camera_report(char *,size_t,uint32_t,uint32_t);
int dc_arena_camera_check(uint64_t,uint64_t,const char *);
#endif
