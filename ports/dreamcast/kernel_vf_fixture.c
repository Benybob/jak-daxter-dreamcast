/* VU0 vf lowering fixture module for the DGO boot, in its own translation unit
 * so its static names cannot clash with the boot modules (AI-assisted), ISC. */
#include "goal_module.h"
#include "build/vf_fixture.h"
int dc_kernel_load_vf_fixture(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result){
  return dc_goal_load_module_data(rt,goal_vf_fixture_exports,GOAL_VF_FIXTURE_EXPORT_COUNT,
                                  GOAL_VF_FIXTURE_DATA,GOAL_VF_FIXTURE_DATA_COUNT,sync,NULL,result);
}
