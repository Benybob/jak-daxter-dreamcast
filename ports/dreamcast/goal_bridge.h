/* Bounded native kernel bridges (AI-assisted), ISC. */
#ifndef DC_GOAL_BRIDGE_H
#define DC_GOAL_BRIDGE_H
#include "goal_module.h"
/* One active runtime, quiescent installation, scalar GCC ABI only.
 * Installs pair new (global selector only) and a namespaced type-of bridge.
 * No original heap object/process allocator or full kernel API yet. */
int dc_goal_install_core_bridges(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                  dc_goal_module_result *result);
void dc_goal_clear_core_bridges(dc_goal_runtime *rt);
/* Requires the same active core runtime; installs original type/new and
 * method-set! paths plus bounded global basic allocation. */
int dc_goal_install_type_bridges(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                  dc_goal_module_result *result);
/* Scalar integer faults return zero and set runtime error status. This is a
 * bootstrap fault ABI, not complete original exception/process semantics. */
int dc_goal_install_integer_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                    dc_goal_module_result *result);
#endif
