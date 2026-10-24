/* Read-only main-thread arming preflight (AI-assisted), ISC.
 * int dc_goal_main_thread_check(dc_goal_runtime *rt, uint32_t process,
 *   uint32_t reservation, uint32_t thread, uint32_t stack_base,
 *   uint32_t stack_bytes, unsigned armed, uint32_t bootstrap);
 * Returns 1 and sets rt->error to DC_GOAL_OK when the quiescent main thread
 * matches the armed or disarmed policy below. Returns 0 otherwise. Helper
 * BAD_TYPE and BOUNDS errors are preserved. Policy and arithmetic failures
 * set DC_GOAL_BAD_ARGUMENT. A NULL runtime returns 0 and writes nothing.
 *
 * Read-only: no process, thread, stack, or context writes, and no alloc,
 * intern, or global-state change. dc_goal_thread_stack_open already requires
 * the exact 112-byte process schema, a non-wrapping reservation, heap
 * geometry, the 128-byte 12-method cpu-thread schema and parent, owner and
 * top-thread links, backup extent, and a stack span disjoint from the entire
 * process reservation including free heap. This checker does not repeat or
 * drop those facts. It does not require heap-cur to equal the cpu-thread end:
 * original arming does not resize or allocate.
 *
 * Additional policy: armed is 0 or 1; the stack is at least 4096 bytes with
 * a 16-byte-aligned non-wrapping base and size; backup capacity is
 * 16-byte-aligned and no larger than that span. process+40 is this thread,
 * thread+8 is rt->s7 (#f, not integer 0), and thread+24 is the stack top.
 * Armed 0 requires process+32 to be the existing "ready" symbol and
 * thread+20 to be 0; bootstrap is ignored. Armed 1 requires process+32 to
 * be the existing "waiting-to-run" symbol, a non-zero even bootstrap, and
 * thread+20 equal to that bootstrap. Names are found, never interned.
 * Bootstrap code identity is not established here. The native wrapper
 * separately checks the canonical registered bootstrap and the original
 * set-to-run entries.
 *
 * The caller owns pinned, quiescent objects and the working stack, and must
 * release native contexts before arming. PC, SP, and status guards do not
 * prove that no private context exists. This checker does not bind, schedule,
 * collect, or clean up. This header does not claim host or native verification.
 */
#ifndef DC_GOAL_MAIN_THREAD_H
#define DC_GOAL_MAIN_THREAD_H
#include "goal_runtime.h"
int dc_goal_main_thread_check(dc_goal_runtime *rt,uint32_t process,
                              uint32_t reservation,uint32_t thread,
                              uint32_t stack_base,uint32_t stack_bytes,
                              unsigned armed,uint32_t bootstrap);
#endif
