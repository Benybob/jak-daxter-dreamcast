/* Read-only main-thread arming preflight (AI-assisted), ISC.
 * Uses dc_goal_thread_stack_open for the existing process, cpu-thread schema,
 * owner, top-thread, backup extent, and stack span disjoint from the whole
 * reservation. Additional checks are armed 0 or 1, stack size and alignment,
 * backup capacity, main-thread, previous #f (rt->s7), initial sp, and the
 * ready or waiting-to-run status/PC pair. No target write, alloc, or intern.
 * Does not prove bootstrap code identity or the absence of a private context.
 */
#include "goal_main_thread.h"
#include "goal_thread_stack.h"
#include <stdint.h>
#include <string.h>
enum {
  OFF_PREVIOUS=8,
  OFF_PC=20,
  OFF_SP=24,
  OFF_STATUS=32,
  OFF_MAIN_THREAD=40,
  STACK_MIN_BYTES=4096
};
static int add_ok(uint32_t base,uint32_t off,uint32_t *sum) {
  if(off>UINT32_MAX-base) return 0;
  *sum=base+off;
  return 1;
}
static int read_u32(dc_goal_runtime *rt,uint32_t addr,uint32_t *out) {
  unsigned char *p;
  if(addr>UINT32_MAX-3u) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  p=dc_goal_resolve(rt,addr,4);
  if(!p) return 0;
  memcpy(out,p,4);
  return 1;
}
static int fail_keep(dc_goal_thread_stack *view,dc_goal_runtime *rt) {
  enum dc_goal_error err=rt->error;
  dc_goal_thread_stack_clear(view);
  rt->error=err==DC_GOAL_OK?DC_GOAL_BAD_ARGUMENT:err;
  return 0;
}
static int fail_arg(dc_goal_thread_stack *view,dc_goal_runtime *rt) {
  dc_goal_thread_stack_clear(view);
  rt->error=DC_GOAL_BAD_ARGUMENT;
  return 0;
}
static int aligned16(uint32_t value) { return (value&15u)==0; }
static int existing_symbol(dc_goal_runtime *rt,const char *name,uint32_t *out) {
  uint32_t symbol=dc_goal_find(rt,name);
  if(!symbol) {
    if(rt->error==DC_GOAL_OK) rt->error=DC_GOAL_BAD_ARGUMENT;
    return 0;
  }
  *out=symbol;
  return 1;
}
int dc_goal_main_thread_check(dc_goal_runtime *rt,uint32_t process,
                              uint32_t reservation,uint32_t thread,
                              uint32_t stack_base,uint32_t stack_bytes,
                              unsigned armed,uint32_t bootstrap) {
  dc_goal_thread_stack view;
  uint32_t main_thread,previous,pc,sp,status,stack_top,symbol,at;
  if(!rt) return 0;
  memset(&view,0,sizeof view);
  if(!dc_goal_thread_stack_open(&view,rt,process,reservation,thread,stack_base,
                                stack_bytes)) {
    dc_goal_thread_stack_clear(&view);
    return 0;
  }
  if((armed!=0u && armed!=1u) || !aligned16(stack_base) ||
     !aligned16(stack_bytes) || stack_bytes<STACK_MIN_BYTES ||
     !add_ok(stack_base,stack_bytes,&stack_top) ||
     !aligned16(view.backup_bytes) || view.backup_bytes>stack_bytes)
    return fail_arg(&view,rt);
  if(!add_ok(process,OFF_MAIN_THREAD,&at) || !read_u32(rt,at,&main_thread) ||
     !add_ok(thread,OFF_PREVIOUS,&at) || !read_u32(rt,at,&previous) ||
     !add_ok(thread,OFF_PC,&at) || !read_u32(rt,at,&pc) ||
     !add_ok(thread,OFF_SP,&at) || !read_u32(rt,at,&sp) ||
     !add_ok(process,OFF_STATUS,&at) || !read_u32(rt,at,&status))
    return fail_keep(&view,rt);
  if(main_thread!=thread || previous!=rt->s7 || sp!=stack_top)
    return fail_arg(&view,rt);
  if(armed==0u) {
    if(!existing_symbol(rt,"ready",&symbol)) return fail_keep(&view,rt);
    if(status!=symbol || pc!=0u) return fail_arg(&view,rt);
  } else {
    if(!existing_symbol(rt,"waiting-to-run",&symbol)) return fail_keep(&view,rt);
    if(status!=symbol || bootstrap==0u || (bootstrap&1u)!=0u || pc!=bootstrap)
      return fail_arg(&view,rt);
  }
  dc_goal_thread_stack_clear(&view);
  rt->error=DC_GOAL_OK;
  return 1;
}
