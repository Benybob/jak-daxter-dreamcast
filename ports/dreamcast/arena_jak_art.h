/* Isolated full player-art admission evidence (AI-assisted), ISC. */
#ifndef DC_ARENA_JAK_ART_H
#define DC_ARENA_JAK_ART_H
#include "goal_art.h"
typedef struct {
  uint32_t version,eichar_bytes,sidekick_bytes,single_shortfall;
  dc_goal_art_result load;
  uint32_t after,error,last_failed_alloc,relocate_entry,relocate_calls;
  uint32_t slab_bytes,slab_live,slab_high_water;
} dc_jak_art_evidence;
extern volatile dc_jak_art_evidence dc_arena_jak_art_evidence;
extern char dc_arena_jak_art_reason[96];
int dc_arena_jak_art_load(dc_goal_runtime *,uint32_t level);
#endif
