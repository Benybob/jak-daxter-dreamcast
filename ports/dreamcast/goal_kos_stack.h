/* Private KOS borrowed-stack transition scope (AI-assisted), ISC. */
#ifndef DC_GOAL_KOS_STACK_H
#define DC_GOAL_KOS_STACK_H
#include <kos/thread.h>
#include <stdint.h>
typedef struct {
 kthread_t *owner;void *old_stack;size_t old_bytes;uint32_t root_sr;
 uint32_t working_base,working_bytes;unsigned active;
} dc_goal_kos_stack;
/* Default supervisor C context, IRQs/FPU enabled, outside an interrupt, current
 * SP inside the current KOS stack record. Zero means unsupported context. */
uint32_t dc_goal_kos_stack_owner(void);
int dc_goal_kos_stack_sr_ok(uint32_t);
/* Separate pinned root-C scope. On success IRQs remain disabled; raw switch must
 * enable them only AFTER installing SP on the borrowed stack. No intervening
 * arbitrary C work. Caller already validated owned, adequate span including IRQ
 * headroom. No thread exit/cancel/unwind or OS migration during this scope. */
int dc_goal_kos_stack_enter(dc_goal_kos_stack *,uint32_t base,uint32_t bytes);
/* Raw return must mask IRQs before restoring root SP. This restores the original
 * owner record and SR, then reports any unsupported stack-record mutation. */
int dc_goal_kos_stack_leave(dc_goal_kos_stack *);
#endif
