/* Real KOS timer scheduling on borrowed native stacks (AI-assisted), ISC. */
#include <kos.h>
#include <arch/irq.h>
#include <arch/timer.h>
#include "goal_bridge.h"
#include "goal_fiber.h"
#include "goal_stack_call.h"
#include "fiber_test_support.h"
#include "runtime_cases.h"
#include <string.h>
typedef uint64_t (*two)(uint64_t,uint64_t);
static irq_cb_t old_timer;
static kthread_t *probe_owner;
static uint32_t probe_base,probe_bytes;
static volatile unsigned fiber_ticks,sync_ticks,bad_ticks,sync_active;
static unsigned mask_flags,overlap_flags,sync_calls;static uint32_t sync_entry;
static uint32_t entry(dc_goal_runtime *rt,const char *name){return dc_goal_symbol_value(rt,dc_goal_find(rt,name));}
static int filled(const void *p,unsigned n,unsigned char v){const unsigned char *b=p;while(n--)if(*b++!=v)return 0;return 1;}
static void timer_probe(irq_t code,irq_context_t *ctx,void *data){
 (void)data;
 if(thd_get_current()==probe_owner && (dc_goal_fiber_current() || sync_active)){
  if((uintptr_t)probe_owner->stack!=probe_base || probe_owner->stack_size!=probe_bytes ||
     ctx->r[15]<probe_base || ctx->r[15]>probe_base+probe_bytes)++bad_ticks;
  if(sync_active)++sync_ticks;else ++fiber_ticks;
 }
 old_timer.hdl(code,ctx,old_timer.data);
}
static void wait_ticks(void){
 uint64_t start=timer_ms_gettime64();
 while(timer_ms_gettime64()-start<30u)__asm__ volatile("nop");
}
static uint64_t fiber_body(void){
 dc_goal_fiber *f=dc_goal_fiber_current();uint64_t value;
 {uint64_t old_value=f->value,out=UINT64_C(0x1122334455667788),rejected;
  unsigned phase=f->phase,calls=sync_calls;uint32_t old_irq=f->native_irq_state;irq_mask_t sr=irq_disable();
  rejected=dc_goal_fiber_yield(f->runtime,UINT64_C(0xf0e0d0c0b0a09080));
  mask_flags=(rejected==0u)|((f->runtime->error==DC_GOAL_BAD_ARGUMENT)<<1)|((f->phase==phase)<<2)|
   ((f->value==old_value)<<3)|((f->native_irq_state==old_irq)<<4)|(((irq_get_sr()&0xf0u)==0xf0u)<<5);
  irq_restore(sr);f->runtime->error=DC_GOAL_OK;
  {int result=dc_goal_call_on_stack(f->runtime,sync_entry,probe_base,4096,&out);
   overlap_flags=(result==0)|((f->runtime->error==DC_GOAL_BAD_ARGUMENT)<<1)|
    ((out==UINT64_C(0x1122334455667788))<<2)|((sync_calls==calls)<<3)|
    ((filled(dc_goal_resolve(f->runtime,probe_base,4096),4096,0xa5))<<4);}
  f->runtime->error=DC_GOAL_OK;
 }
 wait_ticks();value=dc_goal_fiber_yield(f->runtime,UINT64_C(0xabcdef0123456789));
 wait_ticks();return value^UINT64_C(0x123456789abcdef0);
}
static uint64_t sync_body(void){
 ++sync_calls;sync_active=1;wait_ticks();sync_active=0;return UINT64_C(0xfedcba9876543210);
}
typedef struct {dc_goal_fiber *fiber;uint64_t value;int result;} foreign_attempt;
static void *foreign_body(void *data){
 foreign_attempt *attempt=data;attempt->result=dc_goal_fiber_step(attempt->fiber,99,&attempt->value);return NULL;
}
uint32_t dc_runtime_kos_stack_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id,dc_goal_cache_sync sync){
 dc_goal_module_result module={0};dc_goal_fiber fiber;
 uint32_t proc=entry(rt,"process"),old_object=dc_goal_get_method(rt,rt->object_type,0),old_proc=dc_goal_get_method(rt,proc,0);
 uint32_t owner,thread,allocation,stack,name,target,used,symbols;uint64_t value;unsigned intact,previous_ticks;irq_mask_t sr;
 unsigned char header[128],backup[32];void *root_stack;size_t root_bytes;kthread_t *worker;
 unsigned char code[2][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
 dc_goal_code_export exports[2]={{"dc-kos-fiber-probe",code[0],12,NULL,0},{"dc-kos-sync-probe",code[1],12,NULL,0}};
 two new_proc=(two)(uintptr_t)entry(rt,"dc-new-process");foreign_attempt attempt;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 probe_owner=thd_get_current();root_stack=probe_owner->stack;root_bytes=probe_owner->stack_size;sr=irq_get_sr();
 target=(uint32_t)(uintptr_t)fiber_body;memcpy(code[0]+8,&target,4);target=(uint32_t)(uintptr_t)sync_body;memcpy(code[1]+8,&target,4);
 CHECK(dc_goal_load_module(rt,exports,2,sync,NULL,&module),1,"timer/native bodies registered with exact scalar ABI");if(module.functions!=2)return 0;
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"timer owner sized allocator selected");
 CHECK(dc_goal_set_method(rt,proc,0,entry(rt,"(method new process)")),1,"timer owner original process constructor selected");
 dc_goal_clear_process_context(rt);allocation=dc_goal_alloc(rt,16416);stack=allocation+16;name=dc_goal_intern(rt,"dc-kos-stack-owner");owner=(uint32_t)new_proc(name,12288);
 CHECK(allocation && owner,1,"timer oracle owns fenced16KiB working span and process");if(!allocation || !owner)goto restore;
 thread=dc_goal_construct_main_thread(rt,owner,12400,stack,16384,name,8192);
 CHECK(thread!=0,1,"timer oracle owns original CPU backup");if(!thread)goto restore;
 memset(dc_goal_resolve(rt,allocation,16416),0xa5,16416);memcpy(header,dc_goal_resolve(rt,thread-4,128),128);
 used=rt->used;symbols=rt->symbol_count;probe_base=stack;probe_bytes=16384;fiber_ticks=sync_ticks=bad_ticks=sync_active=0;
 sync_entry=entry(rt,"dc-kos-sync-probe");mask_flags=overlap_flags=sync_calls=0;
 old_timer=irq_get_handler(EXC_TMU0_TUNI0);CHECK(old_timer.hdl!=NULL,1,"real KOS scheduling timer handler is present");if(!old_timer.hdl)goto restore;
 CHECK(irq_set_handler(EXC_TMU0_TUNI0,timer_probe,NULL),0,"timer observation chains existing KOS handler");
 CHECK(dc_goal_fiber_init(&fiber,rt,owner,12400,thread,stack,16384,entry(rt,"dc-kos-fiber-probe")),1,"timer-preemptible native context initialized");
 CHECK(dc_runtime_fiber_checked_step(&fiber,0,&value,&intact),DC_FIBER_YIELDED,"fiber survives actual scheduling interrupts before first yield");
 CHECK(mask_flags&1u,1,"masked yield returns refusal");
 CHECK((mask_flags>>1)&1u,1,"masked yield reports argument error");
 CHECK((mask_flags>>2)&1u,1,"masked yield keeps running phase");
 CHECK((mask_flags>>3)&1u,1,"masked yield leaves yielded value untouched");
 CHECK((mask_flags>>4)&1u,1,"masked yield preserves saved resume SR");
 CHECK((mask_flags>>5)&1u,1,"masked yield preserves caller mask before recovery");
 CHECK(overlap_flags&1u,1,"sync call rejects lower part of its entire current OS stack");
 CHECK((overlap_flags>>1)&1u,1,"current-stack overlap reports argument error");
 CHECK((overlap_flags>>2)&1u,1,"current-stack overlap preserves result");
 CHECK((overlap_flags>>3)&1u,1,"current-stack overlap never executes target");
 CHECK((overlap_flags>>4)&1u,1,"current-stack overlap preserves unused working payload");
 CHECK(intact,1,"timer-preempted yield preserves root GPR/FPR/control");CHECK(value,UINT64_C(0xabcdef0123456789),"timer-preempted fiber yields exact64-bit value");
 CHECK(fiber_ticks>0,1,"real TMU0 interrupt observed while native fiber owns CPU stack");
 CHECK(probe_owner->stack==root_stack && probe_owner->stack_size==root_bytes,1,"root KOS stack record restored after yield");
 CHECK(irq_get_sr()&0x700080f0u,sr&0x700080f0u,"root IRQ/FPU/supervisor control restored after yield");previous_ticks=fiber_ticks;
 memset(dc_goal_resolve(rt,stack,16384),0xd3,16384);
 CHECK(dc_runtime_fiber_checked_step(&fiber,UINT64_C(0x9876543210abcdef),&value,&intact),DC_FIBER_COMPLETED,"restored fiber survives further KOS scheduling and completes");
 CHECK(intact,1,"timer-preempted completion preserves root registers");CHECK(value,UINT64_C(0x9876543210abcdef)^UINT64_C(0x123456789abcdef0),"timer and native suspension preserve resumed64-bit value");
 CHECK(fiber_ticks>previous_ticks,1,"real scheduling interrupts observed after image restoration");
 CHECK(probe_owner->stack==root_stack && probe_owner->stack_size==root_bytes,1,"root KOS stack record restored after completion");
 CHECK(dc_goal_fiber_clear(&fiber),1,"timer continuation metadata released");
 CHECK(dc_goal_call_on_stack(rt,entry(rt,"dc-kos-sync-probe"),stack,16384,&value),1,"synchronous alternate-stack call survives real timer scheduling");
 CHECK(value,UINT64_C(0xfedcba9876543210),"synchronous timer-preempted result remains exact");CHECK(sync_ticks>0,1,"real timer interrupt observed during synchronous target");
 CHECK(probe_owner->stack==root_stack && probe_owner->stack_size==root_bytes,1,"synchronous call restores original KOS stack record");
 CHECK(irq_get_sr()&0x700080f0u,sr&0x700080f0u,"synchronous call restores original root IRQ control");
 /* A different OS thread may not import a paused private native continuation. */
 CHECK(dc_goal_fiber_init(&fiber,rt,owner,12400,thread,stack,16384,entry(rt,"dc-kos-fiber-probe")),1,"fresh context for real foreign OS-thread refusal");
 CHECK(dc_goal_fiber_step(&fiber,0,&value),DC_FIBER_YIELDED,"foreign-thread probe has a valid suspended image");
 memcpy(backup,dc_goal_resolve(rt,thread+124,32),32);memset(dc_goal_resolve(rt,stack,16384),0xd3,16384);
 attempt.fiber=&fiber;attempt.value=UINT64_C(0x1122334455667788);attempt.result=99;
 worker=thd_create(false,foreign_body,&attempt);CHECK(worker!=NULL,1,"real separate KOS thread starts refusal attempt");
 if(worker){CHECK(thd_join(worker,NULL),0,"foreign OS-thread attempt joined before inspecting shared result");}
 CHECK(attempt.result,DC_FIBER_FAIL,"foreign OS owner rejected before native restoration");CHECK(attempt.value,UINT64_C(0x1122334455667788),"foreign OS owner preserves output");
 CHECK(fiber.phase,DC_FIBER_FAULT,"foreign OS owner closes unsupported continuation");
 CHECK(filled(dc_goal_resolve(rt,stack,16384),16384,0xd3),1,"foreign OS owner cannot write poisoned working stack");
 CHECK(memcmp(backup,dc_goal_resolve(rt,thread+124,32),32),0,"foreign OS owner leaves backup payload unchanged");CHECK(dc_goal_fiber_clear(&fiber),1,"foreign-owner terminal metadata released");
 CHECK(dc_goal_fiber_init(&fiber,rt,owner,12400,thread,stack,16384,entry(rt,"dc-kos-fiber-probe")),1,"fresh context for unsupported root IRQ mask");
 value=UINT64_C(0x1122334455667788);{irq_mask_t masked=irq_disable();int result=dc_goal_fiber_step(&fiber,0,&value);irq_restore(masked);
 CHECK(result,DC_FIBER_FAIL,"masked root rejected before entering native context");}
 CHECK(fiber.phase,DC_FIBER_FAULT,"masked root context remains terminal");CHECK(value,UINT64_C(0x1122334455667788),"masked root rejection preserves output");
 CHECK(filled(dc_goal_resolve(rt,stack,16384),16384,0xd3),1,"masked root rejection cannot touch working span");CHECK(dc_goal_fiber_clear(&fiber),1,"masked-root terminal metadata released");
 CHECK(irq_set_handler(EXC_TMU0_TUNI0,old_timer.hdl,old_timer.data),0,"original scheduling timer handler restored exactly");
 {irq_cb_t restored=irq_get_handler(EXC_TMU0_TUNI0);CHECK(restored.hdl==old_timer.hdl && restored.data==old_timer.data,1,"timer handler and userdata identity preserved");}
 CHECK(bad_ticks,0,"every observed fiber/sync interrupt sees correct KOS stack and savedSP");
 CHECK(memcmp(header,dc_goal_resolve(rt,thread-4,128),128),0,"OS scheduling does not rewrite original GOAL CPU header");
 CHECK(filled(dc_goal_resolve(rt,allocation,16),16,0xa5),1,"timer-preempted working stack lower fence");CHECK(filled(dc_goal_resolve(rt,stack+16384,16),16,0xa5),1,"timer-preempted working stack upper fence");
 CHECK(rt->used,used,"timer/context paths allocate no GOAL arena bytes");CHECK(rt->symbol_count,symbols,"timer/context paths intern no names");
restore:
 dc_goal_set_method(rt,rt->object_type,0,old_object);dc_goal_set_method(rt,proc,0,old_proc);rt->error=DC_GOAL_OK;
#undef CHECK
 return module.code_bytes;
}
