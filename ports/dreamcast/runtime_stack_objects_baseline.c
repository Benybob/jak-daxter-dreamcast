/* Separate TU keeps generated baseline static names disjoint (AI-assisted), ISC. */
#include "goal_module.h"
#include "build/stack_objects_baseline.h"
int dc_runtime_stack_baseline_load(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result){
 return dc_goal_load_module_data(rt,goal_stack_objects_baseline_exports,GOAL_STACK_OBJECTS_BASELINE_EXPORT_COUNT,
  GOAL_STACK_OBJECTS_BASELINE_DATA,GOAL_STACK_OBJECTS_BASELINE_DATA_COUNT,sync,NULL,result);
}
