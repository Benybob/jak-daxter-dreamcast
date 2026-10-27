/* Read-only main-thread stack-size preflight (AI-assisted), ISC.
 * Uses dc_goal_thread_stack_open for the existing process, cpu-thread schema,
 * owner, top-thread, backup extent, and disjoint working stack. Additional
 * checks are main-thread, previous #f (rt->s7 symbol), pc 0, initial sp, alignment,
 * and, only when the backup size changes, exact heap-cur and strict heap-top
 * headroom. No target write and no backup copy.
 */
#include "goal_thread_resize.h"
#include "goal_thread_stack.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>
enum {
  THREAD_RAW_BYTES=128,
  OFF_PREVIOUS=8,
  OFF_PC=20,
  OFF_SP=24,
  OFF_MAIN_THREAD=40,
  OFF_HEAP_TOP=80,
  OFF_HEAP_CUR=84,
  STACK_MIN_BYTES=4096
};
static int add_ok(uint32_t base,uint32_t off,uint32_t *sum) {
  if(off>UINT32_MAX-base) return 0;
  *sum=base+off;
  return 1;
}
static int read_u32(dc_goal_runtime *rt,uint32_t addr,uint32_t *out) {
  unsigned char *p;
  if(addr>UINT32_MAX-3u) { (DC_GOAL_NOTE_SITE(4),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
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
  (DC_GOAL_NOTE_SITE(4),rt->error=DC_GOAL_BAD_ARGUMENT);
  return 0;
}
static int aligned16(uint32_t value) { return (value&15u)==0; }
int dc_goal_thread_resize_check(dc_goal_runtime *rt,uint32_t process,
                                uint32_t reservation,uint32_t thread,
                                uint32_t stack_base,uint32_t stack_bytes,
                                uint32_t new_backup_bytes) {
  dc_goal_thread_stack view;
  uint32_t main_thread,previous,pc,sp,stack_top,heap_cur,heap_top;
  uint32_t object_end,old_end,new_end,at;
  if(!rt) return 0;
  memset(&view,0,sizeof view);
  if(!dc_goal_thread_stack_open(&view,rt,process,reservation,thread,stack_base,
                                stack_bytes))
    return 0;
  if(!aligned16(stack_base) || !aligned16(stack_bytes) ||
     stack_bytes<STACK_MIN_BYTES || !add_ok(stack_base,stack_bytes,&stack_top) ||
     !aligned16(view.backup_bytes) || !aligned16(new_backup_bytes) ||
     new_backup_bytes>stack_bytes ||
     new_backup_bytes>(uint32_t)INT32_MAX-128u)
    return fail_arg(&view,rt);
  if(!add_ok(process,OFF_MAIN_THREAD,&at) || !read_u32(rt,at,&main_thread) ||
     !add_ok(thread,OFF_PREVIOUS,&at) || !read_u32(rt,at,&previous) ||
     !add_ok(thread,OFF_PC,&at) || !read_u32(rt,at,&pc) ||
     !add_ok(thread,OFF_SP,&at) || !read_u32(rt,at,&sp))
    return fail_keep(&view,rt);
  if(main_thread!=thread || previous!=rt->s7 || pc!=0u || sp!=stack_top)
    return fail_arg(&view,rt);
  if(view.backup_bytes==new_backup_bytes) {
    dc_goal_thread_stack_clear(&view);
    rt->error=DC_GOAL_OK;
    return 1;
  }
  if(thread<4u || !add_ok(thread-4u,THREAD_RAW_BYTES,&object_end) ||
     !add_ok(object_end,view.backup_bytes,&old_end))
    return fail_arg(&view,rt);
  if(!add_ok(process,OFF_HEAP_CUR,&at) || !read_u32(rt,at,&heap_cur) ||
     !add_ok(process,OFF_HEAP_TOP,&at) || !read_u32(rt,at,&heap_top))
    return fail_keep(&view,rt);
  if(heap_cur!=old_end || !add_ok(object_end,new_backup_bytes,&new_end))
    return fail_arg(&view,rt);
  if(new_end>=heap_top) {
    dc_goal_thread_stack_clear(&view);
    rt->error=DC_GOAL_OUT_OF_MEMORY;
    return 0;
  }
  dc_goal_thread_stack_clear(&view);
  rt->error=DC_GOAL_OK;
  return 1;
}
