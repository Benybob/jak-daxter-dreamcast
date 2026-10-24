/* Original main-thread startup data, bounded native adapter (AI-assisted), ISC. */
#ifndef DC_GOAL_MAIN_START_H
#define DC_GOAL_MAIN_START_H
#include "goal_fiber.h"
/* Installs a fail-closed set-to-run-bootstrap entry. Original architecture
 * assembly resume is unsupported. This entry must never execute game code. */
int dc_goal_install_main_start_guard(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
/* Original data-only set-to-run, ready base process/main CPU only. Caller owns
 * pinned, quiescent spans and releases all native contexts before arming.
 * Six valid C input words are copied before original stores. Target must have
 * the explicit six-uint64 scalar prototype; entry provenance proves no ABI.
 * Does not bind process identity or run the target. No effect rollback. */
int dc_goal_arm_main_thread(dc_goal_runtime *,uint32_t process,uint32_t reservation,
                            uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,
                            uint32_t target,const uint64_t arguments[6]);
/* Explicit native adaptation of an armed original image: imports rreg0 target
 * and rreg1..6 arguments into private C fiber metadata. Original PC/SP/register
 * header and waiting-to-run status stay unchanged. Requires initialized owned
 * storage under the same init_words lifetime/alias contract. No original ASM
 * resume, scheduler, return-from-thread-dead, cleanup, GC or unwinding. */
int dc_goal_fiber_init_main(dc_goal_fiber *,dc_goal_runtime *,uint32_t process,
                            uint32_t reservation,uint32_t thread,uint32_t stack_base,uint32_t stack_bytes);
#endif
