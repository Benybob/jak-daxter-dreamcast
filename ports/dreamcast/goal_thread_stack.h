/* Bounded CPU-thread stack image (AI-assisted), ISC.
 * Data-only copy between a caller-owned execution stack and the backup
 * payload at basic+124. No context switch, register save, PC jump, or
 * process/thread metadata write. The caller keeps every supplied span
 * unmoved and quiescent, and must restore from outside the destination
 * execution stack. This component does not detect a target current SP.
 */
#ifndef DC_GOAL_THREAD_STACK_H
#define DC_GOAL_THREAD_STACK_H
#include "goal_process_heap.h"
typedef struct {
  dc_goal_process_heap heap;
  uint32_t thread,stack_base,stack_bytes,backup_bytes,image_sp,image_bytes;
  unsigned valid,saved;
} dc_goal_thread_stack;
int dc_goal_thread_stack_open(dc_goal_thread_stack *view,dc_goal_runtime *rt,
  uint32_t process,uint32_t reservation_bytes,uint32_t thread,
  uint32_t stack_base,uint32_t stack_bytes);
int dc_goal_thread_stack_backup(dc_goal_thread_stack *view,uint32_t sp);
int dc_goal_thread_stack_restore(dc_goal_thread_stack *view);
void dc_goal_thread_stack_clear(dc_goal_thread_stack *view);
#endif
