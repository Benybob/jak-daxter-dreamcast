/* Scalar-configured original valid? for the kernel boot, in its own translation
 * unit because whole-file gcommon also defines valid? (AI-assisted), ISC. */
#include "goal_module.h"
#include "build/boot_valid.h"
int dc_kernel_boot_load_valid(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result){
  return dc_goal_load_module_data(rt,goal_boot_valid_exports,GOAL_BOOT_VALID_EXPORT_COUNT,
                                  GOAL_BOOT_VALID_DATA,GOAL_BOOT_VALID_DATA_COUNT,sync,NULL,result);
}
