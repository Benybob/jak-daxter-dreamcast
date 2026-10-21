/* Keep baseline arrays in their own C unit for differential execution (AI-assisted), ISC. */
#include "goal_module.h"
#include "build/shift_baseline.h"
int dc_load_shift_baseline(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  return dc_goal_load_module(rt,goal_shift_baseline_exports,GOAL_SHIFT_BASELINE_EXPORT_COUNT,sync,0,result);
}
