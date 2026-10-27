/* Allocator-sourced declared extents with no process-field authority (AI-assisted), ISC. */
#include "process_spans.h"
#include "goal_fiber.h"
#include <limits.h>
#include <string.h>
#define SPANS_MAGIC UINT32_C(0x5053504e)
static dc_goal_process_spans *active;
static dc_goal_runtime *registered_runtime;
static dc_goal_process_span *registered_rows;
static uint32_t registered_capacity,registered_owner;
static dc_goal_process_span_hooks registered_hooks;
static uint64_t next_generation;
static uintptr_t address(const void *p){
 uintptr_t v=(uintptr_t)p;
#ifdef __DREAMCAST__
 v&=UINT32_C(0x1fffffff);
#endif
 return v;
}
static int overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn){return an&&bn&&(a<=b?b-a<an:a-b<bn);}
static int fail(dc_goal_runtime *r,enum dc_goal_error error){if(r&&r->error==DC_GOAL_OK)r->error=error;return 0;}
static int external(dc_goal_runtime *r,const void *p,size_t n){
 return r&&r->memory&&p&&!overlap(address(p),n,address(r),sizeof *r)&&
  !overlap(address(p),n,address(r->memory),r->capacity);
}
static int valid_output(dc_goal_runtime *r,const void *p,size_t n){
 dc_goal_fiber *f;uintptr_t at,offset;uint32_t guest;if(!p)return 0;
 if(overlap(address(p),n,address(r),sizeof *r)||
    overlap(address(p),n,address(active),sizeof *active)||
    overlap(address(p),n,address(registered_rows),(size_t)registered_capacity*sizeof *registered_rows))return 0;
 f=dc_goal_fiber_current();
 if(f&&overlap(address(p),n,address(f),sizeof *f))return 0;
 if(!overlap(address(p),n,address(r->memory),r->capacity))return 1;
 /* Only ephemeral CURRENT C working-stack storage may be inside the arena. */
 at=address(p);
 if(at<address(r->memory))return 0;
 offset=at-address(r->memory);
 if(offset>r->capacity || n>r->capacity-offset || offset>UINT32_MAX-r->base)return 0;
 guest=r->base+(uint32_t)offset;
 return f&&f->runtime==r&&f->phase==DC_FIBER_RUNNING&&f->root_live&&
  guest>=f->stack_base&&guest-f->stack_base<=f->stack_bytes&&n<=f->stack_bytes-(guest-f->stack_base);
}
static int state(dc_goal_runtime *r){
 if(!r||!active||registered_runtime!=r)return 0;
 if(!external(r,active,sizeof *active)||!external(r,registered_rows,(size_t)registered_capacity*sizeof *registered_rows)||
    active->magic!=SPANS_MAGIC||active->runtime!=r||active->rows!=registered_rows||
    active->capacity!=registered_capacity||active->count>registered_capacity||active->owner!=registered_owner||
    active->hooks.owner!=registered_hooks.owner||active->hooks.quiescent!=registered_hooks.quiescent||
    (registered_hooks.owner&&registered_hooks.owner()!=registered_owner)){
  (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));return -1;
 }
 return 1;
}
static uint32_t lower_bound(uint32_t process){
 uint32_t lo=0,hi=active->count;
 while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(registered_rows[mid].process<process)lo=mid+1;else hi=mid;}
 return lo;
}
static int exact(const dc_goal_process_span *a,const dc_goal_process_span *b){
 return a->process==b->process&&a->bytes==b->bytes&&a->generation==b->generation&&a->generation!=0;
}
int dc_goal_process_spans_init(dc_goal_process_spans *table,dc_goal_runtime *r,
 dc_goal_process_span *rows,uint32_t cap,const dc_goal_process_span_hooks *hooks){
 dc_goal_process_span_hooks copy;uint32_t owner=0;size_t bytes;
 if(!r||!hooks)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 copy=*hooks;
 if(!cap||cap>UINT32_MAX/(uint32_t)sizeof *rows)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 bytes=(size_t)cap*sizeof *rows;
 if(active||!copy.quiescent||!copy.quiescent(r)||!external(r,table,sizeof *table)||!external(r,rows,bytes)||
    overlap(address(table),sizeof *table,address(rows),bytes))return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
#ifdef __DREAMCAST__
 if(!copy.owner)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
#endif
 if(copy.owner){owner=copy.owner();if(!owner)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));}
 memset(table,0,sizeof *table);memset(rows,0,bytes);
 table->magic=SPANS_MAGIC;table->runtime=r;table->rows=rows;table->capacity=cap;table->owner=owner;table->hooks=copy;
 active=table;registered_runtime=r;registered_rows=rows;registered_capacity=cap;registered_owner=owner;registered_hooks=copy;
 return 1;
}
int dc_goal_process_spans_registered(dc_goal_runtime *r){return state(r);}
int dc_goal_process_span_room(dc_goal_runtime *r){
 int s=state(r);if(s!=1)return s;
 if(active->count==registered_capacity||next_generation==UINT64_MAX){fail(r,DC_GOAL_OUT_OF_MEMORY);return -1;}
 return 1;
}
int dc_goal_process_span_note(dc_goal_runtime *r,uint32_t process,uint32_t bytes,dc_goal_process_span *out){
 uint32_t at,offset;dc_goal_process_span made;
 if(state(r)!=1)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 if(out&&!valid_output(r,out,sizeof *out))return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 if(process<4||((process-4)&15u)||bytes<112||process-4<r->base)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 offset=process-4-r->base;
 if(offset>r->used||bytes>r->used-offset||!dc_goal_resolve(r,process-4,bytes))return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 at=lower_bound(process);
 if(at<active->count&&registered_rows[at].process==process){
  if(registered_rows[at].bytes!=bytes||!registered_rows[at].generation)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
  if(out)*out=registered_rows[at];
  return 1;
 }
 if(at>0&&process-registered_rows[at-1].process<registered_rows[at-1].bytes)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 if(at<active->count&&registered_rows[at].process-process<bytes)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 if(dc_goal_process_span_room(r)!=1)return 0;
 made.process=process;made.bytes=bytes;made.generation=++next_generation;
 memmove(registered_rows+at+1,registered_rows+at,(size_t)(active->count-at)*sizeof *registered_rows);
 registered_rows[at]=made;++active->count;if(out)*out=made;return 1;
}
int dc_goal_process_span_lookup(dc_goal_runtime *r,uint32_t process,dc_goal_process_span *out){
 uint32_t at;
 if(state(r)!=1)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 if(!valid_output(r,out,sizeof *out))return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 at=lower_bound(process);
 if(at==active->count||registered_rows[at].process!=process||!registered_rows[at].generation)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 *out=registered_rows[at];return 1;
}
int dc_goal_process_span_check(dc_goal_runtime *r,const dc_goal_process_span *witness){
 dc_goal_process_span copy;uint32_t at;
 if(!witness||state(r)!=1)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 copy=*witness;at=lower_bound(copy.process);
 return at<active->count&&exact(&copy,registered_rows+at)?1:(DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
}
int dc_goal_process_span_retire(dc_goal_runtime *r,const dc_goal_process_span *witness){
 dc_goal_process_span copy;uint32_t at;
 if(!witness||state(r)!=1)return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 copy=*witness;
 if(!registered_hooks.quiescent(r)||!dc_goal_process_span_check(r,&copy))return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 at=lower_bound(copy.process);
 memmove(registered_rows+at,registered_rows+at+1,(size_t)(active->count-at-1)*sizeof *registered_rows);
 --active->count;memset(registered_rows+active->count,0,sizeof *registered_rows);return 1;
}
int dc_goal_process_spans_detach(dc_goal_runtime *r){
 dc_goal_process_spans *table;dc_goal_process_span *rows;size_t bytes;
 int s=state(r);if(s==0)return 1;if(s<0)return 0;
 if(!registered_hooks.quiescent(r))return (DC_GOAL_NOTE_SITE(5),fail(r,DC_GOAL_BAD_ARGUMENT));
 table=active;rows=registered_rows;bytes=(size_t)registered_capacity*sizeof *rows;
 active=NULL;registered_runtime=NULL;registered_rows=NULL;registered_capacity=registered_owner=0;
 memset(&registered_hooks,0,sizeof registered_hooks);memset(rows,0,bytes);memset(table,0,sizeof *table);return 1;
}
