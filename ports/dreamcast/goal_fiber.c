/* Cooperative context lifecycle and exact stack images (AI-assisted), ISC.
 * Native continuation PCs are a closed set of platform shim labels. This is not
 * original scheduler integration or general exception/failure unwinding. */
#include "goal_fiber.h"
#include "goal_scalar_call.h"
#include "fiber_bounds.h"
#ifdef __DREAMCAST__
#include "goal_bridge.h"
#include "goal_kos_stack.h"
#include <arch/irq.h>
#endif
#include <stdlib.h>
#include <string.h>
#define FIBER_MAGIC 0x46494252u
#define MODE_MASK 0x00380000u
static dc_goal_fiber *current;
static int bad(dc_goal_runtime *rt) {if(rt)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
static uintptr_t address(const void *p) {
 uintptr_t a=(uintptr_t)p;
#ifdef __DREAMCAST__
 a&=0x1fffffffu;
#endif
 return a;
}
static int overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn) {return an && bn && (a<=b?b-a<an:a-b<bn);}
static int storage_ok(dc_goal_runtime *rt,const void *p,size_t bytes) {
 return rt && rt->memory && p && !overlap(address(p),bytes,address(rt->memory),rt->capacity) &&
  !overlap(address(p),bytes,address(rt),sizeof *rt);
}
static int fault(dc_goal_fiber *f,enum dc_goal_error error) {
 f->phase=DC_FIBER_FAULT;f->root_live=0;dc_goal_thread_stack_clear(&f->image);
 f->runtime->error=error==DC_GOAL_OK?DC_GOAL_BAD_ARGUMENT:error;return DC_FIBER_FAIL;
}
static int view_ok(const dc_goal_fiber *f) {
 return f->image.valid==1 && f->image.heap.runtime==f->runtime && f->image.heap.process==f->process &&
  f->image.heap.allocation_bytes==f->reservation && f->image.thread==f->thread &&
  f->image.stack_base==f->stack_base && f->image.stack_bytes==f->stack_bytes;
}
static int main_receipt_ok(const dc_goal_fiber *f) {
 uint32_t pid;const void *p;
 if(!f->main_import_self)return f->main_import_pid?bad(f->runtime):1;
 if(!f->main_import_pid || f->main_import_self!=(uintptr_t)f || f->process>UINT32_MAX-40u)return bad(f->runtime);
 p=dc_goal_resolve(f->runtime,f->process+36,4);if(!p)return 0;
 memcpy(&pid,p,4);return pid==f->main_import_pid?1:bad(f->runtime);
}
#ifdef __DREAMCAST__
extern void dc_goal_context_enter_raw(struct dc_goal_native_regs *,const struct dc_goal_native_regs *,uint32_t);
extern void dc_goal_context_leave_raw(struct dc_goal_native_regs *,const struct dc_goal_native_regs *,uint32_t);
extern const unsigned char dc_goal_context_boot_raw[],dc_goal_context_enter_return[],dc_goal_context_leave_return[];
#define PC(label) ((uint32_t)(uintptr_t)(label))
static int root_ok(const dc_goal_fiber *f) {
 return f->root_live && f->root.pr==PC(dc_goal_context_enter_return) &&
  f->root.sp==f->root_expected_sp && !(f->root.sp&3u) && !(f->root.fpscr&MODE_MASK);
}
static int thread_ok(const dc_goal_fiber *f,int fresh) {
 uint32_t top=f->stack_base+f->stack_bytes;
 if(f->native_thread.fpscr&MODE_MASK)return 0;
 if(fresh)return f->native_thread.pr==PC(dc_goal_context_boot_raw) && f->native_thread.sp==top;
 return f->native_thread.pr==PC(dc_goal_context_leave_return) && !(f->native_thread.sp&3u) &&
  f->native_thread.sp>=f->stack_base && f->native_thread.sp<=top-4;
}
void dc_goal_fiber_bootstrap(void) __attribute__((noreturn));
void dc_goal_fiber_bootstrap(void) {
 dc_goal_fiber *f=current;uint64_t value;struct dc_goal_native_regs terminal;
 if(!f || f->magic!=FIBER_MAGIC || f->phase!=DC_FIBER_RUNNING || !root_ok(f) || f->argument_count>8)abort();
 value=0;
 if(!dc_goal_scalar_call(f->entry,f->argument_count,f->arguments,&value))bad(f->runtime);
 f->value=value;f->phase=f->runtime->error==DC_GOAL_OK?DC_FIBER_DONE:DC_FIBER_FAULT;
 if(!root_ok(f) || !dc_goal_native_regs_snapshot(&terminal) ||
    !dc_fiber_leave_push_ok(f->stack_base,f->stack_bytes,terminal.sp))abort();
 /* Return on root with IRQs masked until its KOS stack record is restored. */
 dc_goal_context_leave_raw(&f->native_thread,&f->root,irq_disable()|0xf0u);
 /* DONE/FAULT must never be resumed. No unsafe return through bootstrap PR. */
 abort();
}
#endif
int dc_goal_fiber_init_words(dc_goal_fiber *f,dc_goal_runtime *rt,uint32_t process,uint32_t reservation,
                             uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,uint32_t entry,
                             unsigned argument_count,const uint64_t *arguments) {
 dc_goal_code_entry_info code;
 uint64_t copied[8]={0};unsigned i;
 if(current || !f)return bad(rt);
 /* Safe owned metadata is invalidated on an inactive failed rebind, including
  * a null runtime. Reject aliases before touching another live object. */
 if(rt && (overlap(address(f),sizeof *f,address(rt),sizeof *rt) ||
    (rt->memory && overlap(address(f),sizeof *f,address(rt->memory),rt->capacity))))return bad(rt);
 if(argument_count>8 || (argument_count && !arguments)) {memset(f,0,sizeof *f);return bad(rt);}
 /* An explicit inactive rebind may use the old owned vector as input. */
 if(rt && rt->memory)for(i=0;i<argument_count;++i)copied[i]=arguments[i];
 memset(f,0,sizeof *f);
 if(!rt || !rt->memory)return bad(rt);
#ifdef __DREAMCAST__
 if(!dc_goal_core_bridges_active(rt))return bad(rt);
 f->platform_owner=dc_goal_kos_stack_owner();if(!f->platform_owner)return bad(rt);
#endif
 if(stack_bytes<4096 || (stack_base&15u) || (stack_bytes&15u) || stack_bytes>UINT32_MAX-stack_base)return bad(rt);
 if(!dc_goal_code_entry(rt,entry,&code))return 0;
 if(overlap(entry,code.bytes,stack_base,stack_bytes))return bad(rt);
 if(!dc_goal_thread_stack_open(&f->image,rt,process,reservation,thread,stack_base,stack_bytes))return 0;
 f->runtime=rt;f->process=process;f->reservation=reservation;f->thread=thread;
 f->stack_base=stack_base;f->stack_bytes=stack_bytes;f->entry=entry;
 f->argument_count=argument_count;memcpy(f->arguments,copied,sizeof copied);
 f->native_thread.sp=stack_base+stack_bytes;
#ifdef __DREAMCAST__
 f->native_thread.pr=PC(dc_goal_context_boot_raw);
#endif
 f->magic=FIBER_MAGIC;f->phase=DC_FIBER_NEW;rt->error=DC_GOAL_OK;return 1;
}
int dc_goal_fiber_init(dc_goal_fiber *f,dc_goal_runtime *rt,uint32_t process,uint32_t reservation,
                       uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,uint32_t entry) {
 return dc_goal_fiber_init_words(f,rt,process,reservation,thread,stack_base,stack_bytes,entry,0,NULL);
}
int dc_goal_fiber_clear(dc_goal_fiber *f) {
 if(!f)return 0;
 if(current)return bad(f->magic==FIBER_MAGIC?f->runtime:NULL);
 memset(f,0,sizeof *f);return 1;
}
dc_goal_fiber *dc_goal_fiber_current(void) {return current;}
int dc_goal_fiber_main_completed(dc_goal_fiber *f,dc_goal_runtime *rt) {
 if(!rt)return 0;
 if(rt->error!=DC_GOAL_OK)return 0;
 if(!f)return bad(rt);
 if(current || !storage_ok(rt,f,sizeof *f))return bad(rt);
 if(f->magic!=FIBER_MAGIC || f->runtime!=rt || f->phase!=DC_FIBER_DONE ||
    f->root_live || f->argument_count!=6 || !f->main_import_self ||
    !view_ok(f) || f->image.saved!=0)return bad(rt);
 if(!main_receipt_ok(f))return 0;
#ifdef __DREAMCAST__
 if(!f->platform_owner || dc_goal_kos_stack_owner()!=f->platform_owner)return bad(rt);
#endif
 return 1;
}
int dc_goal_fiber_step(dc_goal_fiber *f,uint64_t resume_value,uint64_t *out) {
 dc_goal_runtime *rt;
 void *working;
 if(!f || f->magic!=FIBER_MAGIC)return 0;
 rt=f->runtime;
 if(current || !out || (f->phase!=DC_FIBER_NEW && f->phase!=DC_FIBER_SUSPENDED))return bad(rt);
 if(!storage_ok(rt,f,sizeof *f) || !storage_ok(rt,out,sizeof *out) ||
    overlap(address(out),sizeof *out,address(f),sizeof *f))return bad(rt);
 if(f->argument_count>8)return fault(f,DC_GOAL_BAD_ARGUMENT);
 if(!main_receipt_ok(f))return fault(f,rt->error);
 if(f->stack_bytes<4096 || (f->stack_base&15u) || (f->stack_bytes&15u) ||
    f->stack_bytes>UINT32_MAX-f->stack_base)return fault(f,DC_GOAL_BAD_ARGUMENT);
 working=dc_goal_resolve(rt,f->stack_base,f->stack_bytes);
 if(!working)return fault(f,rt->error);
 if(overlap(address(out),sizeof *out,address(working),f->stack_bytes))return bad(rt);
#ifndef __DREAMCAST__
 (void)resume_value;return bad(rt); /* Host never executes/restores SH4 context. */
#else
 {
  dc_goal_code_entry_info code;
  struct dc_goal_native_regs caller;
  dc_goal_thread_stack fresh;
  dc_goal_kos_stack os_stack;
  int is_new=f->phase==DC_FIBER_NEW;
  if(!dc_goal_core_bridges_active(rt))return fault(f,DC_GOAL_BAD_ARGUMENT);
  if(!f->platform_owner || dc_goal_kos_stack_owner()!=f->platform_owner ||
     (!is_new && !dc_goal_kos_stack_sr_ok(f->native_irq_state)))return fault(f,DC_GOAL_BAD_ARGUMENT);
  if(!view_ok(f) || !thread_ok(f,is_new))return fault(f,DC_GOAL_BAD_ARGUMENT);
  if(!dc_goal_code_entry(rt,f->entry,&code))return fault(f,rt->error);
  if(overlap(f->entry,code.bytes,f->stack_base,f->stack_bytes))return fault(f,DC_GOAL_BAD_ARGUMENT);
  if(!dc_goal_native_regs_snapshot(&caller) || (rt->base&0xe0000000u) || !dc_fiber_root_push_ok(f->stack_base,f->stack_bytes,caller.sp&0x1fffffffu))return fault(f,DC_GOAL_BAD_ARGUMENT);
  if(is_new) {
   if(f->image.saved || !dc_goal_thread_stack_open(&fresh,rt,f->process,f->reservation,f->thread,f->stack_base,f->stack_bytes) ||
      fresh.backup_bytes!=f->image.backup_bytes)return fault(f,rt->error);
  } else {
   if(f->image.saved!=1 || f->image.image_sp!=f->native_thread.sp ||
      f->image.image_bytes!=f->stack_base+f->stack_bytes-f->native_thread.sp)return fault(f,DC_GOAL_BAD_ARGUMENT);
   if(!dc_goal_thread_stack_restore(&f->image))return fault(f,rt->error);
  }
  /* Sample immediately before the three-register-argument enter call: preceding
   * validation may use outgoing argument stack space. No C calls intervene. */
  if(!dc_goal_kos_stack_enter(&os_stack,f->stack_base,f->stack_bytes))return fault(f,DC_GOAL_BAD_ARGUMENT);
  if(!dc_goal_native_regs_snapshot(&caller) || !dc_fiber_root_push_ok(f->stack_base,f->stack_bytes,caller.sp&0x1fffffffu)){
   dc_goal_kos_stack_leave(&os_stack);return fault(f,DC_GOAL_BAD_ARGUMENT);
  }
  if(is_new)f->native_irq_state=os_stack.root_sr;
  f->resume_value=resume_value;f->root_expected_sp=caller.sp-4u;f->root_live=1;
  f->phase=DC_FIBER_RUNNING;current=f;rt->error=DC_GOAL_OK;
  dc_goal_context_enter_raw(&f->root,&f->native_thread,f->native_irq_state);
  current=NULL;f->root_live=0;
  if(!dc_goal_kos_stack_leave(&os_stack))return fault(f,DC_GOAL_BAD_ARGUMENT);
  if(rt->error!=DC_GOAL_OK)return fault(f,rt->error);
  if(f->phase==DC_FIBER_SUSPENDED) {
   uint32_t needed;
   if(!thread_ok(f,0))return fault(f,DC_GOAL_BAD_ARGUMENT);
   needed=f->stack_base+f->stack_bytes-f->native_thread.sp;
   if(needed>f->peak_requested_image_bytes)f->peak_requested_image_bytes=needed;
   if(!dc_goal_thread_stack_backup(&f->image,f->native_thread.sp))return fault(f,rt->error);
   *out=f->value;return DC_FIBER_YIELDED;
  }
  if(f->phase==DC_FIBER_DONE) {*out=f->value;return DC_FIBER_COMPLETED;}
  return fault(f,rt->error);
 }
#endif
}
uint64_t dc_goal_fiber_yield(dc_goal_runtime *rt,uint64_t value) {
 dc_goal_fiber *f=current;
 if(!f)return (uint64_t)bad(rt);
 if(f->runtime!=rt){bad(f->runtime);return (uint64_t)bad(rt);}
 if(f->phase!=DC_FIBER_RUNNING || rt->error!=DC_GOAL_OK)return (uint64_t)bad(rt);
#ifdef __DREAMCAST__
 {
  struct dc_goal_native_regs caller;
  uint32_t yield_sr;
  if(!dc_goal_core_bridges_active(rt) || !root_ok(f) || !dc_goal_native_regs_snapshot(&caller) ||
     !dc_fiber_leave_push_ok(f->stack_base,f->stack_bytes,caller.sp))return (uint64_t)bad(rt);
  yield_sr=irq_disable();
  if(!dc_goal_kos_stack_sr_ok(yield_sr)){
   irq_restore(yield_sr);return (uint64_t)bad(rt);
  }
  f->value=value;f->phase=DC_FIBER_SUSPENDED;f->native_irq_state=yield_sr;
  dc_goal_context_leave_raw(&f->native_thread,&f->root,f->native_irq_state|0xf0u);
  if(current!=f || f->phase!=DC_FIBER_RUNNING || !root_ok(f))return (uint64_t)bad(rt);
  return f->resume_value;
 }
#else
 (void)value;return (uint64_t)bad(rt);
#endif
}
