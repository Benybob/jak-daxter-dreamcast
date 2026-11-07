/* Geyser Rock through original level loading (AI-assisted), ISC. */
#ifndef DC_LEVEL_LOGIC_H
#define DC_LEVEL_LOGIC_H
#include "goal_runtime.h"
/* Original load-state want-levels / want-display-level for 'training. */
int dc_level_want(dc_goal_runtime *rt);
/* After display-loop frames: original level-status is 'active and the level's
 * bsp-header was attached by its relocate. dc_level_logic_evidence.stage names
 * the step that stopped otherwise. */
int dc_level_check(dc_goal_runtime *rt);
/* Each frame: the *entity-pool* processes still 'ready, and how many of them
 * were already 'ready the frame before (dc_level_ready_probe). */
void dc_level_actors_sample(dc_goal_runtime *rt);
#endif
