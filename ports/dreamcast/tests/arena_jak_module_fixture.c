/* Real loader/registry with initializer execution doubled by the test harness.
 * HOST only (AI-assisted), ISC. */
#include "../goal_module.h"
#define dc_goal_run_initializer unused_host_initializer
#define dc_goal_run_initializer_void unused_host_initializer_void
#include "../goal_module.c"
