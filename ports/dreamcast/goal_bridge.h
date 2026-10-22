/* Bounded native kernel bridges (AI-assisted), ISC. */
#ifndef DC_GOAL_BRIDGE_H
#define DC_GOAL_BRIDGE_H
#include "goal_module.h"
/* One active runtime, quiescent installation, scalar GCC ABI only.
 * Installs pair new (global selector only), namespaced type-of and missing-call
 * error bridges. Install before linking runtime modules containing calls.
 * No complete original heap/process allocator or full kernel API yet. */
int dc_goal_install_core_bridges(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                  dc_goal_module_result *result);
void dc_goal_clear_core_bridges(dc_goal_runtime *rt);
/* Requires the same active core runtime; installs original type/new and
 * method-set! paths plus bounded basic allocation (global or explicit process span). */
int dc_goal_install_type_bridges(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                  dc_goal_module_result *result);
/* Scalar integer faults return zero and set runtime error status. This is a
 * bootstrap fault ABI, not complete original exception/process semantics. */
int dc_goal_install_integer_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                    dc_goal_module_result *result);
/* Explicit sized object.new(heap,type,size): global, or explicitly bound process
 * reservation. No GC, thread switching or exception/failure unwinding. */
int dc_goal_install_object_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                   dc_goal_module_result *result);
/* Caller owns the full unmoved process reservation, including 112-byte header/
 * fields. Same-runtime binding failure clears the old target, with no global
 * fallback. No implicit pp/self ABI or thread context is installed. */
int dc_goal_bind_process_heap(dc_goal_runtime *rt,uint32_t process,uint32_t allocation_bytes);
void dc_goal_clear_process_heap(dc_goal_runtime *rt);
/* Read-only implicit self entry: explicit, quiescent, unmoved base-process
 * reservation, and the initialized original *kernel-context* object. Binding
 * sets its current-process field; clear sets it back to #f if still owned.
 * Allocation-only rebinding/clearing invalidates execution identity. A failed
 * getter clears both bindings and returns zero with BAD_ARGUMENT before body
 * memory access. No scheduler, nested contexts, GC or failure unwinding. */
int dc_goal_install_process_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,
                                    dc_goal_module_result *result);
int dc_goal_bind_process_context(dc_goal_runtime *rt,uint32_t process,uint32_t allocation_bytes);
void dc_goal_clear_process_context(dc_goal_runtime *rt);
#endif
