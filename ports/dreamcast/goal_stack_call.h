/* Synchronous registered zero-argument scalar call on an owned stack.
 * (AI-assisted), ISC. No suspension/resumption, scheduling or unwinding. */
#ifndef DC_GOAL_STACK_CALL_H
#define DC_GOAL_STACK_CALL_H
#include "goal_module.h"
/* Caller owns an unmoved, writable, adequate stack span separate from all live
 * code/metadata/data/C frames; signature is trusted zero-argument uint64 ABI.
 * At least4096 bytes, base/top aligned16, wholly in rt's arena. Entry must be
 * a current loader-registered function start and disjoint from the stack.
 * out is separate caller storage; unchanged on failure. No implicit process
 * binding: entries that need self require prior explicit context binding.
 * No nested calls, exceptions, longjmp, GC or target that fails to return.
 * Boundary validation cannot prevent target stack overflow. Native FPU enabled,
 * incoming FR/SZ/PR clear. Target side effects before an error are not undone. */
int dc_goal_call_on_stack(dc_goal_runtime *rt,uint32_t entry,uint32_t stack_base,
                          uint32_t stack_bytes,uint64_t *out);
#endif
