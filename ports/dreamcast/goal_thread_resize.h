/* Read-only main-thread stack-size preflight (AI-assisted), ISC.
 * int dc_goal_thread_resize_check(dc_goal_runtime *rt, uint32_t process,
 *   uint32_t reservation, uint32_t thread, uint32_t stack_base,
 *   uint32_t stack_bytes, uint32_t new_backup_bytes);
 * Returns 1 and sets rt->error to DC_GOAL_OK when the original stack-size-set!
 * method may run. Returns 0 otherwise. Helper BAD_TYPE and BOUNDS errors are
 * preserved. Policy, state, and arithmetic failures set DC_GOAL_BAD_ARGUMENT.
 * A changed backup whose checked new end is not strictly below heap-top sets
 * DC_GOAL_OUT_OF_MEMORY.
 *
 * Read-only: no process, thread, payload, or context writes, and no alloc,
 * intern, or global-state change. It never stores heap-cur or stack-size and
 * never erases or copies backup bytes. An equal old and new backup is a
 * success no-op after main-thread, header, span, and size checks, including
 * when later heap allocation has moved heap-cur.
 *
 * The caller owns pinned, quiescent spans. Header checks (main-thread, previous
 * #f, pc 0, sp at the stack top) are not proof that no private fiber exists.
 * The caller must have no active or paused native context bound to this CPU
 * and must reopen stale image views after a resize. The native wrapper
 * independently rejects an active fiber before the original call. This checker
 * does not execute, switch, allocate, or change an execution binding.
 * This header does not claim host or native verification.
 */
#ifndef DC_GOAL_THREAD_RESIZE_H
#define DC_GOAL_THREAD_RESIZE_H
#include "goal_runtime.h"
int dc_goal_thread_resize_check(dc_goal_runtime *rt,uint32_t process,
                                uint32_t reservation,uint32_t thread,
                                uint32_t stack_base,uint32_t stack_bytes,
                                uint32_t new_backup_bytes);
#endif
