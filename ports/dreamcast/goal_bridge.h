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
/* Borrowed ownership check; does not install or infer execution identity. */
int dc_goal_core_bridges_active(const dc_goal_runtime *rt);
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
/* Data-only first CPU-thread construction through the unchanged original
 * native constructor. Caller owns the unmoved process reservation and a
 * disjoint aligned execution-stack span in rt's arena. Main/top thread must
 * both be false. Base cpu-thread only, aligned backup size, strict process
 * heap fit. No temporary threads, PC initialization, execution or switching.
 * Allocation/execution bindings are not changed. No body writes on rejection. */
uint32_t dc_goal_construct_main_thread(dc_goal_runtime *rt,uint32_t process,uint32_t reservation_bytes,
                                       uint32_t stack_base,uint32_t stack_bytes,uint32_t name,uint32_t backup_bytes);
/* Explicit cooperative-yield bridge; no original scheduler/activation. */
int dc_goal_install_fiber_bridge(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
/* Original process.activate on a fresh base process and empty base tree only.
 * Explicit pinned owner/stack spans; no allocation/self binding is inferred.
 * Preflight rejects unsupported schema, live context, scratchpad, bad entries,
 * stack overlap and insufficient original 256-byte backup reservation before
 * body effects. Success creates/attaches a ready process; never runs its code.
 * No rollback of target effects, derived process, scheduler or GC support. */
uint32_t dc_goal_activate_process(dc_goal_runtime *,uint32_t process,uint32_t reservation,
                                 uint32_t destination,uint32_t stack_base,uint32_t stack_bytes,uint32_t name);
/* Original void stack-size-set! on an owned fresh main CPU thread. No live
 * native contexts may use this thread; active fibers are rejected. Checked
 * aligned sizes and strict heap headroom, no intervening allocation for a size
 * change. Same-size no-op may follow later allocation. Borrowed image views
 * must be reopened. No payload clearing, bindings, execution or scheduler. */
int dc_goal_resize_main_backup(dc_goal_runtime *,uint32_t process,uint32_t reservation,
                               uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,uint32_t backup_bytes);
#endif
