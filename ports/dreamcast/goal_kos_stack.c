/* Keep KOS's stack guard accurate across native SP changes (AI-assisted), ISC. */
#include "goal_kos_stack.h"
#include <arch/irq.h>
#include <stdlib.h>
#include <string.h>
int dc_goal_kos_stack_sr_ok(uint32_t sr){
 /* MD=1, BL/RB/FD=0, IMASK=0. T/Q/M are caller-clobbered arithmetic flags. */
 return (sr&UINT32_C(0x700080f0))==UINT32_C(0x40000000);
}
uint32_t dc_goal_kos_stack_owner(void){
 uintptr_t sp,base;size_t bytes;kthread_t *owner=thd_get_current();
 if(!owner || irq_inside_int() || !dc_goal_kos_stack_sr_ok(irq_get_sr()))return 0;
 __asm__ volatile("mov r15,%0":"=r"(sp));
 base=(uintptr_t)owner->stack;bytes=owner->stack_size;
 if(!base || !bytes || bytes>UINT32_MAX-base || sp<base || sp>base+bytes)return 0;
 return (uint32_t)(uintptr_t)owner;
}
int dc_goal_kos_stack_enter(dc_goal_kos_stack *scope,uint32_t base,uint32_t bytes){
 uint32_t owner,sr;
 if(!scope)return 0;
 memset(scope,0,sizeof *scope);owner=dc_goal_kos_stack_owner();
 if(!owner || bytes<4096 || ((base|bytes)&15u) || bytes>UINT32_MAX-base)return 0;
 sr=irq_disable();
 if((uint32_t)(uintptr_t)thd_get_current()!=owner || !dc_goal_kos_stack_sr_ok(sr)){irq_restore(sr);return 0;}
 scope->owner=(kthread_t *)(uintptr_t)owner;scope->old_stack=scope->owner->stack;
 scope->old_bytes=scope->owner->stack_size;scope->root_sr=sr;
 scope->working_base=base;scope->working_bytes=bytes;scope->active=1;
 scope->owner->stack=(void *)(uintptr_t)base;scope->owner->stack_size=bytes;
 __asm__ volatile("":::"memory");return 1;
}
int dc_goal_kos_stack_leave(dc_goal_kos_stack *scope){
 int intact;
 /* Internal paired transition, never exposed as a guest callable bridge. */
 if(!scope || !scope->active || thd_get_current()!=scope->owner)abort();
 irq_disable();
 intact=(uintptr_t)scope->owner->stack==scope->working_base && scope->owner->stack_size==scope->working_bytes;
 scope->owner->stack=scope->old_stack;scope->owner->stack_size=scope->old_bytes;scope->active=0;
 __asm__ volatile("":::"memory");irq_restore(scope->root_sr);return intact;
}
