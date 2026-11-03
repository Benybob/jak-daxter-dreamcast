/* Original player boot dependency probe (AI-assisted), ISC. */
#ifndef DC_ARENA_JAK_H
#define DC_ARENA_JAK_H
#include "goal_module.h"
/* Separate from the unchanged live v3 ABI. Counts mean completed calls, never
 * inferred player execution. A nonzero blocker is always a failing check. */
typedef struct {
  uint32_t version,debug_calls,debug_entry,debug_before,debug_after;
  uint32_t layout_before,layout_after,debug_bytes,debug_restored;
  uint32_t blocker,start_entry,init_entry,continue_entry,continue_calls,continue_point;
  uint32_t lookup_entry,lookup_calls,art_directory,art_name,art_result;
  uint32_t target_before,target_after,spawn_calls,target_resumes,target_state;
  uint32_t pad_position_updates,follow_frames,runtime_error,error_site;
  char missing[96];
} dc_jak_evidence;
extern volatile dc_jak_evidence dc_arena_jak_evidence;
int dc_arena_jak_initializer(dc_goal_runtime *,const char *,uint64_t *);
int dc_arena_jak_initializer_void(dc_goal_runtime *,const char *);
int dc_arena_jak_finish(dc_goal_runtime *);
#endif
