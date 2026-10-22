/* Bounded CPU-thread stack image (AI-assisted), ISC.
 * Backup stores the exact bytes [sp, stack-top) at the thread payload.
 * Restore writes those bytes back and leaves the thread object untouched.
 * An invalidated view stays invalid until open; clear only zeroes the view.
 */
#include "goal_thread_stack.h"
#include <limits.h>
#include <string.h>
enum {
  THREAD_RAW_BYTES=128, THREAD_BACKUP_OFF=124, THREAD_METHODS=12,
  OFF_PROCESS=4, OFF_STACK_TOP=28, OFF_BACKUP_BYTES=32,
  OFF_TOP_THREAD=44, OFF_HEAP_BASE=76, OFF_HEAP_TOP=80, OFF_HEAP_CUR=84
};
typedef char dc_goal_thread_raw_check[(THREAD_BACKUP_OFF+4)==THREAD_RAW_BYTES?1:-1];
static int fail_view(dc_goal_thread_stack *view,enum dc_goal_error err) {
  dc_goal_runtime *rt=view?view->heap.runtime:NULL;
  if(view) memset(view,0,sizeof *view);
  if(rt) rt->error=err;
  return 0;
}
static int read_u32(dc_goal_runtime *rt,uint32_t addr,uint32_t *out) {
  unsigned char *p;
  if(addr>UINT32_MAX-3u) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  p=dc_goal_resolve(rt,addr,4);
  if(!p) return 0;
  memcpy(out,p,4);
  return 1;
}
static int add_ok(uint32_t base,uint32_t off,uint32_t *sum) {
  if(off>UINT32_MAX-base) return 0;
  *sum=base+off;
  return 1;
}
static int ranges_overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
  uint32_t a_end,b_end;
  if(!an || !bn) return 0;
  if(!add_ok(a,an,&a_end) || !add_ok(b,bn,&b_end)) return 1;
  return a<b_end && b<a_end;
}
/* Re-check the process reservation, cpu-thread schema, owner links, backup
 * capacity, and the execution span. Reads only. *backup_out receives the
 * uint32 at basic+32, the pinned backup capacity (original stack-size). */
static int inspect(dc_goal_runtime *rt,uint32_t process,uint32_t reservation,
                   uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,
                   uint32_t *backup_out) {
  dc_goal_process_heap fresh;
  dc_goal_type_fields schema;
  unsigned char *schema_bytes;
  uint32_t installed,thread_type,symbol,tag,owner,top_thread,stack_top,backup,base,cur,top;
  uint32_t raw,extent,need,at,res;
  if(!rt) return 0;
  if(!dc_goal_process_heap_open(&fresh,rt,process,reservation)) return 0;
  if(rt->s7>UINT32_MAX-(uint32_t)DC_FIX_SYM_THREAD_TYPE) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  thread_type=dc_goal_symbol_value(rt,rt->s7+(uint32_t)DC_FIX_SYM_THREAD_TYPE);
  symbol=dc_goal_find(rt,"cpu-thread");
  installed=symbol?dc_goal_symbol_value(rt,symbol):0;
  if(!thread_type || dc_goal_type_of(rt,thread_type)!=rt->type_type ||
     !installed || dc_goal_type_of(rt,installed)!=rt->type_type) {
    rt->error=DC_GOAL_BAD_TYPE; return 0;
  }
  schema_bytes=dc_goal_resolve(rt,installed,sizeof schema);
  if(!schema_bytes) return 0;
  memcpy(&schema,schema_bytes,sizeof schema);
  if(schema.allocated_size!=THREAD_RAW_BYTES || schema.num_methods!=THREAD_METHODS ||
     schema.parent!=thread_type) {
    rt->error=DC_GOAL_BAD_TYPE; return 0;
  }
  if(thread<4u || !add_ok(thread-4u,THREAD_RAW_BYTES,&need)) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  raw=thread-4u;
  if(!read_u32(rt,raw,&tag)) return 0;
  if(tag!=installed || dc_goal_type_of(rt,thread)!=installed) {
    rt->error=DC_GOAL_BAD_TYPE; return 0;
  }
  if(!add_ok(thread,OFF_PROCESS,&at) || !read_u32(rt,at,&owner) ||
     !add_ok(thread,OFF_STACK_TOP,&at) || !read_u32(rt,at,&stack_top) ||
     !add_ok(thread,OFF_BACKUP_BYTES,&at) || !read_u32(rt,at,&backup) ||
     !add_ok(process,OFF_TOP_THREAD,&at) || !read_u32(rt,at,&top_thread) ||
     !add_ok(process,OFF_HEAP_BASE,&at) || !read_u32(rt,at,&base) ||
     !add_ok(process,OFF_HEAP_TOP,&at) || !read_u32(rt,at,&top) ||
     !add_ok(process,OFF_HEAP_CUR,&at) || !read_u32(rt,at,&cur)) {
    if(rt->error==DC_GOAL_OK) rt->error=DC_GOAL_BAD_ARGUMENT;
    return 0;
  }
  if(owner!=process || top_thread!=thread) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  if(!stack_bytes || !add_ok(stack_base,stack_bytes,&at) || stack_top!=at) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  if(!add_ok(THREAD_RAW_BYTES,backup,&extent) || !add_ok(raw,extent,&need)) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  if(raw<base || cur>top || need>cur) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  if(process<4u || !add_ok(process-4u,reservation,&res)) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  if(ranges_overlap(stack_base,stack_bytes,process-4u,reservation)) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  if(stack_bytes && !dc_goal_resolve(rt,stack_base,stack_bytes)) return 0;
  if(!dc_goal_resolve(rt,raw,THREAD_RAW_BYTES)) return 0;
  *backup_out=backup;
  return 1;
}
static int copy_exact(dc_goal_runtime *rt,uint32_t dst,uint32_t src,uint32_t n) {
  unsigned char *from,*to;
  uint32_t end;
  if(!add_ok(dst,n,&end) || !add_ok(src,n,&end)) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  from=dc_goal_resolve(rt,src,n);
  if(!from) return 0;
  to=dc_goal_resolve(rt,dst,n);
  if(!to) return 0;
  if(n) memcpy(to,from,n);
  return 1;
}
int dc_goal_thread_stack_open(dc_goal_thread_stack *view,dc_goal_runtime *rt,
                              uint32_t process,uint32_t reservation_bytes,
                              uint32_t thread,uint32_t stack_base,
                              uint32_t stack_bytes) {
  uint32_t backup=0;
  if(!view) { if(rt) rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  memset(view,0,sizeof *view);
  if(!dc_goal_process_heap_open(&view->heap,rt,process,reservation_bytes)) return 0;
  if(!inspect(rt,process,reservation_bytes,thread,stack_base,stack_bytes,&backup))
    return fail_view(view,rt->error);
  view->thread=thread;
  view->stack_base=stack_base;
  view->stack_bytes=stack_bytes;
  view->backup_bytes=backup;
  view->image_sp=0;
  view->image_bytes=0;
  view->valid=1;
  view->saved=0;
  rt->error=DC_GOAL_OK;
  return 1;
}
int dc_goal_thread_stack_backup(dc_goal_thread_stack *view,uint32_t sp) {
  dc_goal_runtime *rt;
  uint32_t backup=0,top,used,payload;
  if(!view) return 0;
  rt=view->heap.runtime;
  if(view->valid!=1 || view->saved!=0 || !rt)
    return fail_view(view,DC_GOAL_BAD_ARGUMENT);
  if(!inspect(rt,view->heap.process,view->heap.allocation_bytes,view->thread,
              view->stack_base,view->stack_bytes,&backup))
    return fail_view(view,rt->error);
  if(backup!=view->backup_bytes) return fail_view(view,DC_GOAL_BAD_ARGUMENT);
  if(!add_ok(view->stack_base,view->stack_bytes,&top) || sp<view->stack_base || sp>top)
    return fail_view(view,DC_GOAL_BAD_ARGUMENT);
  used=top-sp;
  if(used>view->backup_bytes) return fail_view(view,DC_GOAL_OUT_OF_MEMORY);
  if(!add_ok(view->thread,THREAD_BACKUP_OFF,&payload))
    return fail_view(view,DC_GOAL_BAD_ARGUMENT);
  if(!copy_exact(rt,payload,sp,used)) return fail_view(view,rt->error);
  view->image_sp=sp;
  view->image_bytes=used;
  view->saved=1;
  rt->error=DC_GOAL_OK;
  return 1;
}
int dc_goal_thread_stack_restore(dc_goal_thread_stack *view) {
  dc_goal_runtime *rt;
  uint32_t backup=0,top,payload;
  if(!view) return 0;
  rt=view->heap.runtime;
  if(view->valid!=1 || view->saved!=1 || !rt)
    return fail_view(view,DC_GOAL_BAD_ARGUMENT);
  if(!inspect(rt,view->heap.process,view->heap.allocation_bytes,view->thread,
              view->stack_base,view->stack_bytes,&backup))
    return fail_view(view,rt->error);
  if(backup!=view->backup_bytes) return fail_view(view,DC_GOAL_BAD_ARGUMENT);
  if(!add_ok(view->stack_base,view->stack_bytes,&top) ||
     view->image_sp<view->stack_base || view->image_sp>top ||
     top-view->image_sp!=view->image_bytes ||
     view->image_bytes>view->backup_bytes)
    return fail_view(view,DC_GOAL_BAD_ARGUMENT);
  if(!add_ok(view->thread,THREAD_BACKUP_OFF,&payload))
    return fail_view(view,DC_GOAL_BAD_ARGUMENT);
  if(!copy_exact(rt,view->image_sp,payload,view->image_bytes))
    return fail_view(view,rt->error);
  view->saved=0;
  rt->error=DC_GOAL_OK;
  return 1;
}
void dc_goal_thread_stack_clear(dc_goal_thread_stack *view) {
  if(view) memset(view,0,sizeof *view);
}
