/* Bounded original process heap spans (AI-assisted), ISC. */
#ifndef DC_GOAL_PROCESS_HEAP_H
#define DC_GOAL_PROCESS_HEAP_H
#include "goal_runtime.h"
/* Caller owns this exact reservation, including the 112-byte process header/
 * fields, within rt's arena. The process must remain unmoved and quiescent.
 * Original base process only; no GC, actor-subtype layouts or lifetime tracking.
 * Context carries an explicit known span, rather than trusting field sizes alone. */
typedef struct {
  dc_goal_runtime *runtime;
  uint32_t process,allocation_bytes;
} dc_goal_process_heap;
int dc_goal_process_heap_open(dc_goal_process_heap *heap,dc_goal_runtime *rt,
                               uint32_t process,uint32_t allocation_bytes);
/* Original kscheme.cpp process path: round16, clear, advance heap-cur only if
 * allocEnd < heapEnd. Exact fits fail. Errors leave bytes/cursor unchanged. */
uint32_t dc_goal_process_alloc(dc_goal_process_heap *heap,uint32_t bytes);
uint32_t dc_goal_process_new_basic(dc_goal_process_heap *heap,uint32_t type,uint32_t bytes);
#endif
