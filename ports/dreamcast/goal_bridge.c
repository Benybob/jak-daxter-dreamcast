/* Native scalar GOAL -> C tail bridges (AI-assisted), ISC. */
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#ifdef __DREAMCAST__
#include <kos/dbgio.h>
#include <arch/cache.h>
#ifdef __DREAMCAST__
#include <arch/timer.h>
#endif
#endif
#include "goal_mmi.h"
#include "goal_pad.h"
#include "goal_bridge.h"
#include "sh4_helpers.h"
#if defined(DC_ARENA_LIVE)
#include "arena_live.h"
#elif defined(DC_ARENA_CAMERA)
#include "arena_camera.h"
#endif
/* Preserve prior diagnostic site numbers in all existing modes (AI-assisted). */
#ifndef DC_ARENA_LIVE
#line 18
#endif
#include "goal_fiber.h"
#include "goal_stack_call.h"
#include "goal_scalar_call.h"
#include "goal_thread_resize.h"
#include "goal_main_start.h"
#include "goal_main_thread.h"
#include "goal_process_heap.h"
#include "pp_scope.h"
#include "process_spans.h"
#include "goal_kos_stack.h"
#include "goal_catch_raw.h"
#ifndef __DREAMCAST__
#error These boxed tail bridges require native SH-4 code execution.
#endif
static dc_goal_runtime *active_runtime;
static dc_goal_process_heap active_process_heap;
static dc_goal_process_heap execution_heap;
static uint32_t execution_context;
static int spans_quiescent(const dc_goal_runtime *rt) {
  return rt && active_runtime==rt && !execution_context && !active_process_heap.runtime &&
    dc_goal_pp_scope_runtime_clear_allowed(rt) && !dc_goal_fiber_current();
}
static int known_span_matches(dc_goal_runtime *rt,uint32_t process,uint32_t bytes) {
  dc_goal_process_span span;int registered=dc_goal_process_spans_registered(rt);
  if(!registered)return 1; /* legacy explicit C ownership contract, no lookup */
  if(registered<0 || !dc_goal_process_span_lookup(rt,process,&span))return 0;
  if(span.bytes!=bytes){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  return 1;
}
static int refuse_live_scope(dc_goal_runtime *rt) {
  if(dc_goal_pp_scope_runtime_clear_allowed(rt))return 0;
  if(rt && rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);
  return 1;
}
static unsigned char *context_fields(dc_goal_runtime *rt,uint32_t context) {
  uint32_t type,symbol;
  dc_goal_type_fields *schema;
  symbol=dc_goal_find(rt,"kernel-context");
  type=symbol?dc_goal_symbol_value(rt,symbol):0;
  if(!context || !type || dc_goal_type_of(rt,type)!=rt->type_type ||
     dc_goal_type_of(rt,context)!=type) return NULL;
  schema=dc_goal_resolve(rt,type,sizeof(*schema));
  if(!schema || schema->allocated_size!=48 || !dc_goal_resolve(rt,context-4,48)) return NULL;
  return dc_goal_resolve(rt,context,44);
}
void dc_goal_clear_process_context(dc_goal_runtime *rt) {
  unsigned char *fields;
  uint32_t current;
  enum dc_goal_error saved;
  if(!rt || active_runtime!=rt) return;
  if(refuse_live_scope(rt))return;
  saved=rt->error;
  if(execution_context && (fields=context_fields(rt,execution_context))) {
    memcpy(&current,fields+20,4);
    if(current==execution_heap.process) memcpy(fields+20,&rt->s7,4);
  }
  memset(&execution_heap,0,sizeof(execution_heap)); execution_context=0;
  memset(&active_process_heap,0,sizeof(active_process_heap));
  rt->error=saved;
}
int dc_goal_bind_process_context(dc_goal_runtime *rt,uint32_t process,uint32_t allocation_bytes) {
  dc_goal_process_heap candidate;
  unsigned char *fields;
  uint32_t symbol,context,current;
  if(!rt || active_runtime!=rt) { if(rt) (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  if(refuse_live_scope(rt))return 0;
  dc_goal_clear_process_context(rt);
  if(!known_span_matches(rt,process,allocation_bytes))return 0;
  if(!dc_goal_process_heap_open(&candidate,rt,process,allocation_bytes)) return 0;
  symbol=dc_goal_find(rt,"*kernel-context*");
  context=symbol?dc_goal_symbol_value(rt,symbol):0;
  fields=context_fields(rt,context);
  if(!fields) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  memcpy(&current,fields+20,4);
  if(current!=rt->s7) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  execution_context=context; execution_heap=candidate; active_process_heap=candidate;
  memcpy(fields+20,&process,4); rt->error=DC_GOAL_OK; return 1;
}
/* Desktop call_goal enters GOAL from C with pp = #f (r13 = the symbol table):
 * a file's top-level startup runs with no process. Natively that root is the
 * C stack with no execution context bound, the original kernel context naming
 * no current process, and authenticated scope and span ledgers registered. */
static int root_context(dc_goal_runtime *rt,uint32_t *context) {
  unsigned char *fields;uint32_t symbol,value,current;enum dc_goal_error prior=rt->error;int root=0;
  if(execution_context || execution_heap.runtime || dc_goal_fiber_current() ||
     !dc_goal_pp_scope_active(rt) || dc_goal_process_spans_registered(rt)!=1)return 0;
  symbol=dc_goal_find(rt,"*kernel-context*");  /* find resets rt->error */
  value=symbol?dc_goal_symbol_value(rt,symbol):0;
  if((fields=context_fields(rt,value))){memcpy(&current,fields+20,4);root=current==rt->s7;}
  rt->error=prior;  /* a probe, not a refusal; a retained fault stays */
  if(root)*context=value;
  return root;
}
/* The scheduler a lexical scope is checked against: the bound execution
 * context, or at the root the kernel context with #f and no reservation. */
static void scheduler_identity(dc_goal_runtime *rt,uint32_t *context,uint32_t *process,uint32_t *bytes) {
  *context=execution_context;*process=execution_heap.process;*bytes=execution_heap.allocation_bytes;
  if(root_context(rt,context)){*process=rt->s7;*bytes=0;}
}
static int scope_resolve(dc_goal_runtime *rt,uint32_t *lexical) {
  uint32_t context,process,bytes;
  scheduler_identity(rt,&context,&process,&bytes);
  return dc_goal_pp_scope_resolve(rt,context,process,bytes,&active_process_heap,lexical);
}
static uint64_t current_process(void) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_process_heap candidate;
  unsigned char *fields;
  uint32_t symbol,current=0,lexical=0,root=0;int scoped;
  if(!rt) return 0;
  scoped=scope_resolve(rt,&lexical);
  if(scoped==DC_GOAL_PP_SCOPE_DENIED)return 0;
  if(scoped==DC_GOAL_PP_SCOPE_READY)return lexical; /* retain faults through cleanup */
  if(!active_process_heap.runtime && root_context(rt,&root))return rt->s7; /* a retained fault stays */
  symbol=dc_goal_find(rt,"*kernel-context*");
  if(!execution_context || execution_heap.runtime!=rt ||
     active_process_heap.runtime!=rt || active_process_heap.process!=execution_heap.process ||
     active_process_heap.allocation_bytes!=execution_heap.allocation_bytes ||
     !symbol || dc_goal_symbol_value(rt,symbol)!=execution_context ||
     !dc_goal_process_heap_open(&candidate,rt,execution_heap.process,execution_heap.allocation_bytes) ||
     !(fields=context_fields(rt,execution_context))) goto invalid;
  memcpy(&current,fields+20,4);
  if(current!=execution_heap.process) goto invalid;
  rt->error=DC_GOAL_OK; return current;
invalid:
  dc_goal_clear_process_context(rt); (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0;
}
static uint64_t new_pair(uint64_t heap,uint64_t type,uint64_t car,uint64_t cdr) {
  dc_goal_runtime *rt=active_runtime;
  if (!rt) return 0;
  if (heap!=rt->s7+DC_FIX_SYM_GLOBAL_HEAP || type!=rt->pair_type) {
    (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0;
  }
  return dc_goal_new_pair(rt,(uint32_t)car,(uint32_t)cdr);
}
static uint64_t type_of(uint64_t object) {
  return active_runtime?dc_goal_type_of(active_runtime,(uint32_t)object):0;
}
#ifdef DC_ARENA_JAK_PLAYER
/* Redefinitions of an existing type whose methods differ from its parent's:
 * new_type copies the parent's table over them. --dump dc_arena_jak_player_retype 452 */
volatile struct { uint32_t count,kept,pad[2]; struct { uint32_t type,overridden,pc; char name[24],caller[32]; } row[7]; } dc_arena_jak_player_retype;
static const char *jak_function_name(dc_goal_runtime *rt,uint32_t pc);
static void jak_retype(dc_goal_runtime *rt,uint32_t symbol,uint32_t parent,uint32_t pc) {
  uint32_t type=dc_goal_symbol_value(rt,symbol),i,n,over=0;dc_goal_type_fields *f;
  enum dc_goal_error e=rt->error;
  if(type && (f=dc_goal_resolve(rt,type,sizeof *f)))
    for(i=0;i<f->num_methods && i<32u;++i)
      if(dc_goal_get_method(rt,type,(uint16_t)i)!=dc_goal_get_method(rt,parent,(uint16_t)i))over|=1u<<i;
  rt->error=e;
  if(!over)return;
  {const char *nm=dc_goal_symbol_name(rt,symbol);
   if(!nm || (strcmp(nm,"process") && strcmp(nm,"cpu-thread") && strcmp(nm,"thread") &&
      strcmp(nm,"process-tree") && strcmp(nm,"dead-pool-heap") && strcmp(nm,"process-drawable")))return;}
  n=dc_arena_jak_player_retype.count++;
  if(n>=7u)return;
  dc_arena_jak_player_retype.row[n].type=type;dc_arena_jak_player_retype.row[n].overridden=over;
  dc_arena_jak_player_retype.row[n].pc=pc;
  {const char *nm=dc_goal_symbol_name(rt,symbol),*c=jak_function_name(rt,pc);unsigned k;
   for(k=0;k<24u;++k)dc_arena_jak_player_retype.row[n].name[k]=0;
   for(k=0;nm && nm[k] && k<23u;++k)dc_arena_jak_player_retype.row[n].name[k]=nm[k];
   for(k=0;k<32u;++k)dc_arena_jak_player_retype.row[n].caller[k]=0;
   for(k=0;c && c[k] && k<31u;++k)dc_arena_jak_player_retype.row[n].caller[k]=c[k];}
  dc_arena_jak_player_retype.kept=n+1u;
}
#endif
static uint64_t new_type(uint64_t symbol,uint64_t parent,uint64_t flags) {
  uint32_t type;
  if(!active_runtime)return 0;
#ifdef DC_ARENA_JAK_PLAYER
  jak_retype(active_runtime,(uint32_t)symbol,(uint32_t)parent,(uint32_t)(uintptr_t)__builtin_return_address(0));
#endif
  type=dc_goal_new_type(active_runtime,(uint32_t)symbol,(uint32_t)parent,flags);
  if(!type && dc_goal_sticky_error==DC_GOAL_OK)
    dc_goal_sticky_error=active_runtime->error!=DC_GOAL_OK?active_runtime->error:DC_GOAL_BAD_TYPE;
  return type;
}
static uint64_t method_set(uint64_t type,uint64_t index,uint64_t method) {
  dc_goal_runtime *rt=active_runtime;
  uint32_t enable;
  if (!rt) return 0;
  if (index>UINT32_MAX || method>UINT32_MAX) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  enable=dc_goal_find(rt,"*enable-method-set*");
  return dc_goal_method_set(rt,(uint32_t)type,(uint32_t)index,(uint32_t)method,
                            enable && dc_goal_symbol_value(rt,enable)!=0);
}
#ifdef DC_ARENA_JAK_PLAYER
/* Process-heap allocations and the heap words read when a dead-pool record
 * shrinks. Raw guest loads only: dc_goal_resolve would clobber rt->error.
 * Jak ELF only. --dump dc_arena_jak_player_heap 1928 */
typedef struct { uint32_t process,dest,bytes,cur,span; } dc_jak_heap_alloc;
typedef struct {
  uint32_t process,base,top,current,length,child,ppointer,main_thread,type_tag,span_bytes,slot_1b0;
} dc_jak_heap_shrink;
enum { DC_JAK_HEAP_ALLOC_KEEP=40, DC_JAK_HEAP_SHRINK_KEEP=16, DC_JAK_HEAP_RING=8 };
typedef struct {
  uint32_t version,alloc_count,wide_count,alloc_kept;
  uint32_t fail_count,fail_process,fail_type,fail_bytes;
  uint32_t refused,refused_process,refused_type,refused_bytes;
  uint32_t structure_refused,structure_process,structure_type;
  uint32_t shrink_count,shrink_kept,ring_count;
  dc_jak_heap_alloc alloc[DC_JAK_HEAP_ALLOC_KEEP];
  dc_jak_heap_shrink shrink[DC_JAK_HEAP_SHRINK_KEEP];
  dc_jak_heap_shrink ring[DC_JAK_HEAP_RING];
} dc_arena_jak_player_heap_image;
volatile dc_arena_jak_player_heap_image dc_arena_jak_player_heap={.version=1};
typedef char dc_jak_heap_alloc_bytes[sizeof(dc_jak_heap_alloc)==20?1:-1];
typedef char dc_jak_heap_shrink_bytes[sizeof(dc_jak_heap_shrink)==44?1:-1];
typedef char dc_jak_heap_ring_mask[DC_JAK_HEAP_RING==8?1:-1];
typedef char dc_arena_jak_player_heap_bytes[sizeof(dc_arena_jak_player_heap_image)==1928?1:-1];
static uint32_t jak_guest_word(dc_goal_runtime *rt,uint32_t address) {
  uint32_t value=0xffffffffu,offset;
  if(!rt || !rt->memory || address<rt->base) return value;
  offset=address-rt->base;
  if(offset>rt->used || 4u>rt->used-offset) return value;
  memcpy(&value,rt->memory+offset,4);
  return value;
}
static void jak_reloc_note(dc_goal_runtime *rt,uint32_t site);
static void jak_heap_publish(volatile uint32_t *dst,const void *src,unsigned bytes) {
  uint32_t words[11],i,n;
  if(bytes>sizeof words) return;
  memset(words,0,sizeof words);
  memcpy(words,src,bytes);
  n=bytes/4u;
  for(i=0;i<n;++i) dst[i]=words[i];
}
static void jak_heap_refuse(uint32_t type,uint32_t bytes) {
  uint32_t n=dc_arena_jak_player_heap.refused++;
  if(n) return;
  dc_arena_jak_player_heap.refused_process=active_process_heap.process;
  dc_arena_jak_player_heap.refused_type=type;
  dc_arena_jak_player_heap.refused_bytes=bytes;
}
static void jak_heap_structure(uint32_t type) {
  uint32_t n=dc_arena_jak_player_heap.structure_refused++;
  if(n) return;
  dc_arena_jak_player_heap.structure_process=active_process_heap.process;
  dc_arena_jak_player_heap.structure_type=type;
}
/* alloc_count is every new that reached dc_goal_process_new_basic. The stored
 * rows are the first 40 whose noted span is the default 0x4000 stack (0x4070). */
static void jak_heap_alloc(uint32_t type,uint32_t bytes,uint32_t cur,uint32_t made) {
  uint32_t span=active_process_heap.allocation_bytes,n;
  dc_jak_heap_alloc row;
  dc_arena_jak_player_heap.alloc_count++;
  if(!made) {
    n=dc_arena_jak_player_heap.fail_count++;
    if(!n) {
      dc_arena_jak_player_heap.fail_process=active_process_heap.process;
      dc_arena_jak_player_heap.fail_type=type;
      dc_arena_jak_player_heap.fail_bytes=bytes;
    }
  }
  if(span!=0x4070u) return;
  n=dc_arena_jak_player_heap.wide_count++;
  if(n>=DC_JAK_HEAP_ALLOC_KEEP) return;
  row.process=active_process_heap.process;
  row.dest=made; row.bytes=bytes; row.cur=cur; row.span=span;
  jak_heap_publish((volatile uint32_t *)&dc_arena_jak_player_heap.alloc[n],&row,sizeof row);
  dc_arena_jak_player_heap.alloc_kept=n+1u;
}
/* A process-heap allocation whose heap-cur is behind the cursor the previous
 * allocation on the same process left. Records both sides' type and GOAL
 * caller. --dump dc_arena_jak_player_rewind 2436 */
typedef struct {
  uint32_t process,prev_next,cur,prev_type,prev_pc,type,pc;
  char prev_type_name[16],prev_fn[24],type_name[16],fn[24],proc_type[16],proc_name[16],prev_owner[16];
} dc_jak_heap_rewind;
typedef struct { uint32_t process,next,type,pc,owner_type; } dc_jak_heap_last;
volatile struct {
  uint32_t version,count,kept,tracked;
  dc_jak_heap_rewind row[4];
  dc_jak_heap_last last[32];
  uint32_t events;
  struct { uint32_t process,cur; char type_name[16]; } event[48];
} dc_arena_jak_player_rewind={.version=1};
typedef char dc_jak_heap_rewind_bytes[sizeof(dc_arena_jak_player_rewind)==2436?1:-1];
static void jak_copy_name(volatile char *dst,const char *name,unsigned n) {
  unsigned i;
  for(i=0;i<n;++i)dst[i]=0;
  for(i=0;name && name[i] && i+1<n;++i)dst[i]=name[i];
}
static const char *jak_type_name(dc_goal_runtime *rt,uint32_t type) {
  uint32_t symbol=jak_guest_word(rt,type);
  return symbol==0xffffffffu?NULL:dc_goal_symbol_name(rt,symbol);
}
static const char *jak_function_name(dc_goal_runtime *rt,uint32_t pc) {
  uint32_t at=rt->code_registry_head,i;
  for(i=0;i<rt->code_registry_count && at;++i) {
    uint32_t record[4],offset=at-rt->base;
    if(at<rt->base || offset>rt->used || 16u>rt->used-offset)return NULL;
    memcpy(record,rt->memory+offset,16);
    if(pc>=record[0] && pc-record[0]<record[1])return dc_goal_symbol_name(rt,record[2]);
    at=record[3];
  }
  return NULL;
}
static void jak_heap_rewind(dc_goal_runtime *rt,uint32_t type,uint32_t cur,uint32_t made,
                            uint32_t bytes,uint32_t pc) {
  uint32_t proc=active_process_heap.process,n,i;
  volatile dc_jak_heap_last *last=NULL;
  for(i=0;i<dc_arena_jak_player_rewind.tracked;++i)
    if(dc_arena_jak_player_rewind.last[i].process==proc){last=&dc_arena_jak_player_rewind.last[i];break;}
  if(!last || cur<last->next) {
    n=dc_arena_jak_player_rewind.events++;
    if(n<48u) {
      dc_arena_jak_player_rewind.event[n].process=proc; dc_arena_jak_player_rewind.event[n].cur=cur;
      jak_copy_name(dc_arena_jak_player_rewind.event[n].type_name,jak_type_name(rt,jak_guest_word(rt,proc-4u)),16);
    }
  }
  if(last && cur<last->next) {
    n=dc_arena_jak_player_rewind.count++;
    if(n<4u) {
      volatile dc_jak_heap_rewind *row=&dc_arena_jak_player_rewind.row[n];
      row->process=proc; row->prev_next=last->next; row->cur=cur;
      row->prev_type=last->type; row->prev_pc=last->pc;
      row->type=type; row->pc=pc;
      jak_copy_name(row->prev_type_name,jak_type_name(rt,row->prev_type),16);
      jak_copy_name(row->prev_fn,jak_function_name(rt,row->prev_pc),24);
      jak_copy_name(row->type_name,jak_type_name(rt,type),16);
      jak_copy_name(row->fn,jak_function_name(rt,pc),24);
      jak_copy_name(row->prev_owner,jak_type_name(rt,last->owner_type),16);
      jak_copy_name(row->proc_type,jak_type_name(rt,jak_guest_word(rt,proc-4u)),16);
      {uint32_t name=jak_guest_word(rt,proc),length=jak_guest_word(rt,name),k;
       for(k=0;k<16;++k)row->proc_name[k]=0;
       if(length<64u && name<rt->base+rt->used-80u)
         for(k=0;k<length && k<15;++k)row->proc_name[k]=(char)rt->memory[name+4u+k-rt->base];}
      dc_arena_jak_player_rewind.kept=n+1u;
    }
  }
  if(!made)return;
  if(!last) {
    i=dc_arena_jak_player_rewind.tracked;
    if(i>=32u)return;
    last=&dc_arena_jak_player_rewind.last[i]; last->process=proc;
    dc_arena_jak_player_rewind.tracked=i+1u;
  }
  last->next=made-4u+((bytes+15u)&~15u); last->type=type; last->pc=pc; last->owner_type=jak_guest_word(rt,proc-4u);
}
static void jak_heap_shrink_fill(dc_goal_runtime *rt,uint32_t process,uint32_t span_bytes,
                                 dc_jak_heap_shrink *row) {
  memset(row,0,sizeof *row);
  row->process=process; row->span_bytes=span_bytes;
  if(process>UINT32_MAX-0x1b0u) {
    row->base=row->top=row->current=row->length=row->child=row->ppointer=0xffffffffu;
    row->main_thread=row->type_tag=row->slot_1b0=0xffffffffu;
    return;
  }
  row->length=jak_guest_word(rt,process+68u);
  row->base=jak_guest_word(rt,process+76u);
  row->top=jak_guest_word(rt,process+80u);
  row->current=jak_guest_word(rt,process+84u);
  row->child=jak_guest_word(rt,process+16u);
  row->ppointer=jak_guest_word(rt,process+20u);
  row->main_thread=jak_guest_word(rt,process+40u);
  row->type_tag=jak_guest_word(rt,process-4u);
  row->slot_1b0=jak_guest_word(rt,process+0x1b0u);
}
void dc_arena_jak_player_note_shrink(dc_goal_runtime *rt,uint32_t process,uint32_t span_bytes) {
  dc_jak_heap_shrink row; uint32_t n,ring;
  if(!rt || process<4u) return;
  jak_heap_shrink_fill(rt,process,span_bytes,&row);
  n=dc_arena_jak_player_heap.shrink_count++;
  ring=dc_arena_jak_player_heap.ring_count++;
  if(n<DC_JAK_HEAP_SHRINK_KEEP) {
    jak_heap_publish((volatile uint32_t *)&dc_arena_jak_player_heap.shrink[n],&row,sizeof row);
    dc_arena_jak_player_heap.shrink_kept=n+1u;
  }
  jak_heap_publish((volatile uint32_t *)&dc_arena_jak_player_heap.ring[ring&7u],&row,sizeof row);
}
#endif
static int process_family(dc_goal_runtime *rt,uint32_t type) {
  uint32_t root=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_PROCESS_TYPE);
  dc_goal_type_fields *schema;
  if(!root || dc_goal_type_of(rt,root)!=rt->type_type || !(schema=dc_goal_resolve(rt,root,sizeof *schema)) ||
     schema->allocated_size!=112 || schema->heap_base!=0){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_TYPE;return -1;}
  return dc_goal_is_subtype(rt,type,root);
}
static int process_heap_type_allowed(dc_goal_runtime *rt,uint32_t type) {
  int configured=dc_goal_process_spans_registered(rt),family;
  if(configured<0)return 0;
  if(!configured)return 1;
  family=process_family(rt,type);
  if(family==0)return 1;
  if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);
  return 0; /* nested process ownership needs a verified parent-subspan protocol */
}
/* Extension placement (AI-assisted): global-heap objects of one named type, at
 * least min bytes, go to the runtime's extension region instead of the arena.
 * The DGO boot places display's two PS2 DMA buffers (dma-buffer, 1,712 KiB each)
 * in idle VRAM: the CPU writes them and nothing on the Dreamcast reads them yet. */
static char placement_type[64];static uint32_t placement_min,placed_objects,placed_bytes;
static int placement_matches(dc_goal_runtime *rt,uint32_t type,uint32_t bytes){
  uint32_t symbol;enum dc_goal_error prior=rt->error;
  if(!placement_type[0] || !rt->extension_memory || bytes<placement_min)return 0;
  symbol=dc_goal_find(rt,placement_type);rt->error=prior;
  return symbol && dc_goal_symbol_value(rt,symbol)==type;
}
int dc_goal_place_in_extension(dc_goal_runtime *rt,const char *type_name,uint32_t min_bytes){
  if(!rt || !rt->extension_memory || !type_name || !type_name[0] || strlen(type_name)>=sizeof placement_type ||
     !min_bytes){if(rt)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  strcpy(placement_type,type_name);placement_min=min_bytes;placed_objects=placed_bytes=0;return 1;
}
uint32_t dc_goal_extension_placed(uint32_t *bytes){if(bytes)*bytes=placed_bytes;return placed_objects;}
static uint32_t global_basic_new(dc_goal_runtime *rt,uint32_t type,uint32_t bytes) {
  int configured=dc_goal_process_spans_registered(rt),tracked=0;uint32_t result;
  if(configured<0)return 0;
  if(configured){tracked=process_family(rt,type);if(tracked<0)return 0;}
  if(tracked && (bytes<112 || dc_goal_process_span_room(rt)!=1)){
    if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);
    return 0;
  }
  if(!tracked && placement_matches(rt,type,bytes)){
    uint32_t allocation=dc_goal_alloc_extension(rt,bytes);
    volatile uint32_t *header;
    if(!allocation)return 0;
    header=dc_goal_resolve(rt,allocation,4);*header=type;
    ++placed_objects;placed_bytes+=bytes;
    return allocation+4u;
  }
  result=dc_goal_new_basic(rt,type,bytes);
  if(result && tracked && !dc_goal_process_span_note(rt,result,bytes,NULL))return 0;
  return result;
}
/* kscheme alloc_from_heap's heap kinds (AI-assisted). 'debug with a null kheap
 * is the global heap (kmalloc's null-heap fallback). A value outside the symbol
 * table is "treated as a stack address": the object is zeroed in place there,
 * as dead-pool-heap's get-process does with (the symbol gap-address). */
static int debug_heap_absent(dc_goal_runtime *rt,uint64_t heap);
enum heap_kind { HEAP_REFUSED,HEAP_GLOBAL,HEAP_PROCESS,HEAP_ADDRESS,HEAP_KHEAP };
/* kscheme's loading-level: a fixed symbol whose value is a kheap, the global
 * heap's (kglobalheap) except while a level loads, when load-begin sets it to
 * that level's heap. The port's global heap is the arena itself. */
/* The port interns loading-level by name (the C kernel's fixed slot is not
 * bound), so compiled code links that symbol; find it by name, once per runtime. */
static uint32_t loading_level_symbol(dc_goal_runtime *rt){
  static dc_goal_runtime *cached_rt;static uint32_t cached;
  if(cached_rt!=rt){enum dc_goal_error prior=rt->error;cached=dc_goal_find(rt,"loading-level");rt->error=prior;
    if(cached)cached_rt=rt;}
  return cached;
}
static int loading_level_is_global(dc_goal_runtime *rt){
  return dc_goal_symbol_value(rt,loading_level_symbol(rt))==dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_GLOBAL_HEAP);
}
static enum heap_kind heap_kind(dc_goal_runtime *rt,uint64_t heap) {
  if(heap==rt->s7+DC_FIX_SYM_GLOBAL_HEAP || debug_heap_absent(rt,heap))return HEAP_GLOBAL;
  if(heap && heap==loading_level_symbol(rt))return loading_level_is_global(rt)?HEAP_GLOBAL:HEAP_KHEAP;
  if(heap==rt->s7+DC_FIX_SYM_PROCESS_TYPE)return HEAP_PROCESS;
  if(heap>UINT32_MAX || (heap>=rt->table && heap<rt->table+DC_GOAL_SYMBOL_BYTES))return HEAP_REFUSED;
  return HEAP_ADDRESS;
}
/* A (new 'stack ...) object of root C code (a startup) lives on the current
 * KOS thread's stack, outside the arena: the live part, at or above SP. */
static unsigned char *root_stack_object(dc_goal_runtime *rt,uint32_t address,uint32_t bytes) {
  kthread_t *owner;uint32_t base,size,sp;
  if(dc_goal_fiber_current() || !(owner=(kthread_t *)(uintptr_t)dc_goal_kos_stack_owner()))return NULL;
  base=(uint32_t)(uintptr_t)owner->stack;size=owner->stack_size;
  __asm__ volatile("mov r15,%0":"=r"(sp));
  if((address&0xe0000000u)!=(base&0xe0000000u) || address<sp || address<base ||
     address-base>size || bytes>size-(address-base))return NULL;
  rt->error=DC_GOAL_OK;
  return (unsigned char *)(uintptr_t)address;
}
/* An object's bytes in the arena or, for root C code's stack objects, on the
 * live root stack. The arena miss is not kept as an error. */
static unsigned char *object_bytes(dc_goal_runtime *rt,uint32_t address,uint32_t bytes) {
  enum dc_goal_error prior=rt->error;unsigned char *p=dc_goal_resolve(rt,address,bytes);
  if(p)return p;
  rt->error=prior;
  return root_stack_object(rt,address,bytes)?(rt->error=prior,(unsigned char *)(uintptr_t)address):NULL;
}
/* A basic's type tag, read like object_bytes; 0 when it is neither. */
static uint32_t basic_type(dc_goal_runtime *rt,uint64_t object) {
  const unsigned char *p;uint32_t type;
  if(object<4 || object>UINT32_MAX || ((uint32_t)object&7u)!=4u || !(p=object_bytes(rt,(uint32_t)object-4,4)))return 0;
  memcpy(&type,p,4);return type;
}
/* kmalloc on a guest kheap (base, top, current, top-base): 16-byte aligned from
 * current, refused at top, zeroed (KMALLOC_MEMSET). A basic gets its tag. */
/* Every 'loading-level allocation: calls, the runtime error pending on entry
 * (a pending fault makes new refuse before any heap is chosen), successes, and
 * the last type and size. --dump dc_goal_loading_level_probe 32 */
volatile struct { uint32_t calls,pending,pending_error,pending_site,made,last_type,last_bytes,last_result; } dc_goal_loading_level_probe;
static void loading_level_note(dc_goal_runtime *rt,uint64_t heap,uint64_t type,uint64_t bytes){
  if(!heap || heap!=loading_level_symbol(rt))return;
  ++dc_goal_loading_level_probe.calls;
  dc_goal_loading_level_probe.last_type=(uint32_t)type;dc_goal_loading_level_probe.last_bytes=(uint32_t)bytes;
  if(rt->error!=DC_GOAL_OK){++dc_goal_loading_level_probe.pending;
    dc_goal_loading_level_probe.pending_error=rt->error;dc_goal_loading_level_probe.pending_site=dc_goal_error_site;}
}
static uint64_t kheap_new(dc_goal_runtime *rt,uint32_t type,uint32_t bytes,int basic) {
  uint32_t kheap=dc_goal_symbol_value(rt,loading_level_symbol(rt)),field[4],start,size;
  unsigned char *p=kheap?dc_goal_resolve(rt,kheap,16):NULL,*memory;
  if(!p || !bytes || bytes>UINT32_MAX-15u || (basic && bytes<4)){
    if(rt->error==DC_GOAL_OK){(void)DC_GOAL_NOTE_SITE(1);rt->error=DC_GOAL_BAD_ARGUMENT;}
    return 0;
  }
  memcpy(field,p,16);
  start=(field[2]+15u)&~15u;size=(bytes+15u)&~15u;
  if(start<field[2] || start>field[1] || size>field[1]-start){dc_goal_last_failed_alloc=size;rt->error=DC_GOAL_OUT_OF_MEMORY;return 0;}
  if(!(memory=dc_goal_resolve(rt,start,size)))return 0;
  memset(memory,0,size);
  field[2]=start+size;memcpy(p+8,&field[2],4);
  if(!basic)return start;
  memcpy(memory,&type,4);
  ++dc_goal_loading_level_probe.made;dc_goal_loading_level_probe.last_result=start+4u;
  return start+4u;
}
static uint64_t in_place_new(dc_goal_runtime *rt,uint32_t address,uint32_t type,uint32_t bytes,int basic) {
  uint32_t aligned=(bytes+15u)&~15u;
  unsigned char *memory=NULL;enum dc_goal_error prior=rt->error;
  if(bytes && bytes<=UINT32_MAX-15u && !(basic && bytes<4) &&
     !(memory=dc_goal_resolve(rt,address,aligned)) && prior==DC_GOAL_OK)
    memory=root_stack_object(rt,address,aligned);
  if(!bytes || bytes>UINT32_MAX-15u || (basic && bytes<4) || !memory){
    if(rt->error==DC_GOAL_OK){(void)DC_GOAL_NOTE_SITE(1);rt->error=DC_GOAL_BAD_ARGUMENT;}
    return 0;
  }
  memset(memory,0,aligned);
  if(!basic)return address;
  memcpy(memory,&type,4);
  return address+4u;
}
static uint64_t new_basic(uint64_t heap,uint64_t type) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_type_fields *fields;
  if (!rt) return 0;
  loading_level_note(rt,heap,type,0);
  if(dc_goal_pp_scope_blocks_transfer(rt) && rt->error!=DC_GOAL_OK)return 0;
  if (type>UINT32_MAX ||
      !dc_goal_is_subtype(rt,(uint32_t)type,rt->basic_type)) {
    (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0;
  }
  fields=dc_goal_resolve(rt,(uint32_t)type,sizeof(*fields));
  if(!fields) return 0;
  if(heap_kind(rt,heap)==HEAP_ADDRESS)
    return in_place_new(rt,(uint32_t)heap,(uint32_t)type,fields->allocated_size,1);
  if(heap_kind(rt,heap)==HEAP_GLOBAL)
    return global_basic_new(rt,(uint32_t)type,fields->allocated_size);
  if(heap_kind(rt,heap)==HEAP_KHEAP)
    return kheap_new(rt,(uint32_t)type,fields->allocated_size,1);
  if(heap==rt->s7+DC_FIX_SYM_PROCESS_TYPE && active_process_heap.runtime==rt) {
    uint32_t lexical;
    if(!process_heap_type_allowed(rt,(uint32_t)type)) {
#ifdef DC_ARENA_JAK_PLAYER
      jak_heap_refuse((uint32_t)type,fields->allocated_size);
#endif
      return 0;
    }
    if(scope_resolve(rt,&lexical)==DC_GOAL_PP_SCOPE_DENIED)return 0;
#ifdef DC_ARENA_JAK_PLAYER
    {
      uint32_t proc=active_process_heap.process;
      uint32_t cur=proc<=UINT32_MAX-84u?jak_guest_word(rt,proc+84u):0xffffffffu;
      uint32_t made=dc_goal_process_new_basic(&active_process_heap,(uint32_t)type,fields->allocated_size);
      jak_heap_alloc((uint32_t)type,fields->allocated_size,cur,made);
      jak_heap_rewind(rt,(uint32_t)type,cur,made,fields->allocated_size,
                      (uint32_t)(uintptr_t)__builtin_return_address(0));
      return made;
    }
#else
    return dc_goal_process_new_basic(&active_process_heap,(uint32_t)type,fields->allocated_size);
#endif
  }
  (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0;
}
/* Native counterpart of kscheme new_structure: allocated_size bytes from the
 * global heap, 16-byte rounded and zeroed (kmalloc KMALLOC_MEMSET), no type tag.
 * The C++ kernel never expects a process-heap structure here; that refuses. */
static uint64_t new_structure(uint64_t heap,uint64_t type) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_type_fields *fields;
  if (!rt) return 0;
  loading_level_note(rt,heap,type,0);
  if(dc_goal_pp_scope_blocks_transfer(rt) && rt->error!=DC_GOAL_OK)return 0;
#ifdef DC_ARENA_JAK_PLAYER
  if(heap==rt->s7+DC_FIX_SYM_PROCESS_TYPE && active_process_heap.runtime==rt && type<=UINT32_MAX)
    jak_heap_structure((uint32_t)type);
#endif
  if ((heap_kind(rt,heap)!=HEAP_GLOBAL && heap_kind(rt,heap)!=HEAP_ADDRESS && heap_kind(rt,heap)!=HEAP_KHEAP) || type>UINT32_MAX ||
      !dc_goal_is_subtype(rt,(uint32_t)type,rt->structure_type) || dc_goal_is_subtype(rt,(uint32_t)type,rt->basic_type)) {
    (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0;
  }
  fields=dc_goal_resolve(rt,(uint32_t)type,sizeof(*fields));
  if(!fields || !fields->allocated_size){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  if(heap_kind(rt,heap)==HEAP_ADDRESS)return in_place_new(rt,(uint32_t)heap,(uint32_t)type,fields->allocated_size,0);
  if(heap_kind(rt,heap)==HEAP_KHEAP)return kheap_new(rt,(uint32_t)type,fields->allocated_size,0);
  return dc_goal_alloc(rt,fields->allocated_size);
}
/* Native counterparts of the kscheme default asize-of/copy methods (AI-assisted):
 * object asize-of is the zero function and copy is copy_fixed (identity);
 * basic asize-of is asize_of_basic and copy is copy_basic. */
static uint64_t asize_zero(uint64_t object){(void)object;return 0;}
static uint64_t copy_fixed(uint64_t object){return object;}
static uint64_t asize_of_basic(uint64_t object) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_type_fields *fields;
  uint32_t type;
  if(!rt)return 0;
#ifdef DC_ARENA_JAK_PLAYER
  jak_reloc_note(rt,100u);
#endif
  type=dc_goal_type_of(rt,(uint32_t)object);
  if(!type || !dc_goal_is_subtype(rt,type,rt->basic_type)){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  fields=dc_goal_resolve(rt,type,sizeof(*fields));
  return fields?fields->allocated_size:0;
}
/* copy_basic: a new basic on the named heap holding the original's bytes after
 * the type tag. Desktop asks the type's asize-of method for the size; calling a
 * GOAL method from C is not verified yet, so a type that overrides asize-of
 * refuses. So does desktop's "DANGER COPY BASIC" raw-address destination. */
static uint64_t copy_basic(uint64_t object,uint64_t heap) {
  dc_goal_runtime *rt=active_runtime;
  uint32_t type,bytes,result;
  unsigned char *from,*to;
  if(!rt)return 0;
  if(dc_goal_pp_scope_blocks_transfer(rt) && rt->error!=DC_GOAL_OK)return 0;
  type=dc_goal_type_of(rt,(uint32_t)object);
  if(!type || !dc_goal_is_subtype(rt,type,rt->basic_type)){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  if(dc_goal_get_method(rt,type,5)!=dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-basic-asize-of"))){
    rt->error=DC_GOAL_UNSUPPORTED_TRANSFER;return 0;
  }
  bytes=(uint32_t)asize_of_basic(object);
  if(bytes<4){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  result=(uint32_t)new_basic(heap,type);
  if(!result)return 0;
  from=dc_goal_resolve(rt,(uint32_t)object,bytes-4);
  to=dc_goal_resolve(rt,result,bytes-4);
  if(!from || !to)return 0;
  memcpy(to,from,bytes-4);
  return result;
}
static uint64_t new_object(uint64_t heap,uint64_t type,uint64_t size) {
  dc_goal_runtime *rt=active_runtime;
  if (!rt) return 0;
  loading_level_note(rt,heap,type,size);
  if(dc_goal_pp_scope_blocks_transfer(rt) && rt->error!=DC_GOAL_OK)return 0;
  if (type>UINT32_MAX || size>UINT32_MAX ||
      !dc_goal_is_subtype(rt,(uint32_t)type,rt->basic_type)) {
    (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0;
  }
  if(heap_kind(rt,heap)==HEAP_ADDRESS)
    return in_place_new(rt,(uint32_t)heap,(uint32_t)type,(uint32_t)size,1);
  if(heap_kind(rt,heap)==HEAP_GLOBAL)
    return global_basic_new(rt,(uint32_t)type,(uint32_t)size);
  if(heap_kind(rt,heap)==HEAP_KHEAP)
    return kheap_new(rt,(uint32_t)type,(uint32_t)size,1);
  if(heap==rt->s7+DC_FIX_SYM_PROCESS_TYPE && active_process_heap.runtime==rt) {
    uint32_t lexical;
    if(!process_heap_type_allowed(rt,(uint32_t)type)) {
#ifdef DC_ARENA_JAK_PLAYER
      jak_heap_refuse((uint32_t)type,(uint32_t)size);
#endif
      return 0;
    }
    if(scope_resolve(rt,&lexical)==DC_GOAL_PP_SCOPE_DENIED)return 0;
#ifdef DC_ARENA_JAK_PLAYER
    {
      uint32_t proc=active_process_heap.process;
      uint32_t cur=proc<=UINT32_MAX-84u?jak_guest_word(rt,proc+84u):0xffffffffu;
      uint32_t made=dc_goal_process_new_basic(&active_process_heap,(uint32_t)type,(uint32_t)size);
      jak_heap_alloc((uint32_t)type,(uint32_t)size,cur,made);
      jak_heap_rewind(rt,(uint32_t)type,cur,made,(uint32_t)size,
                      (uint32_t)(uintptr_t)__builtin_return_address(0));
      return made;
    }
#else
    return dc_goal_process_new_basic(&active_process_heap,(uint32_t)type,(uint32_t)size);
#endif
  }
  (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0;
}
#ifdef DC_ARENA_JAK_PLAYER
/* Diagnostic: the first integer trap's caller and the GOAL function holding it. */
volatile struct {uint32_t version,count,kind,pc,entry,bytes,symbol;char name[36];} dc_goal_integer_trap_probe={.version=1};
static void note_integer_trap(uint64_t kind,uint32_t pc) {
  dc_goal_runtime *rt=active_runtime;uint32_t at,i;
  if(dc_goal_integer_trap_probe.count++)return;
  dc_goal_integer_trap_probe.kind=(uint32_t)kind;dc_goal_integer_trap_probe.pc=pc;
  if(!rt)return;
  at=rt->code_registry_head;
  for(i=0;i<rt->code_registry_count && at;++i) {
    const uint32_t *record=(const uint32_t *)(const void *)dc_goal_resolve(rt,at,16);
    if(!record)return;
    if(pc>=record[0] && pc-record[0]<record[1]) {
      const char *name=dc_goal_symbol_name(rt,record[2]);size_t n;
      dc_goal_integer_trap_probe.entry=record[0];dc_goal_integer_trap_probe.bytes=record[1];
      dc_goal_integer_trap_probe.symbol=record[2];
      for(n=0;name && name[n] && n<35;++n)dc_goal_integer_trap_probe.name[n]=name[n];
      return;
    }
    at=record[3];
  }
}
#endif
static uint64_t integer_error(uint64_t kind) {
#ifdef DC_ARENA_JAK_PLAYER
  note_integer_trap(kind,(uint32_t)(uintptr_t)__builtin_return_address(0));
#endif
  if (active_runtime) active_runtime->error=kind==1?DC_GOAL_DIVIDE_BY_ZERO:
                                            (kind==2?DC_GOAL_INTEGER_OVERFLOW:DC_GOAL_BAD_ARGUMENT);
  return 0;
}
/* A call to a non-function is always a fault. It is also sticky, because a later
 * bridge's symbol lookup (dc_goal_find) resets rt->error before the loader looks. */
/* The GOAL callers of the first 8 calls to non-functions: return address and
 * the code-registry function holding it. --dump dc_goal_missing_call_probe 260 */
volatile struct { uint32_t count; struct { uint32_t pc; char fn[28]; } row[8]; } dc_goal_missing_call_probe;
static void note_missing_call(uint32_t pc) {
  dc_goal_runtime *rt=active_runtime;uint32_t n=dc_goal_missing_call_probe.count++,at,i,k;
  if(n>=8u || !rt)return;
  dc_goal_missing_call_probe.row[n].pc=pc;
  at=rt->code_registry_head;
  for(i=0;i<rt->code_registry_count && at>=rt->base && at-rt->base<=rt->used-16u;++i){
    uint32_t record[4];memcpy(record,rt->memory+(at-rt->base),16);
    if(pc>=record[0] && pc-record[0]<record[1]){
      const char *name=dc_goal_symbol_name(rt,record[2]);
      for(k=0;name && name[k] && k<27u;++k)dc_goal_missing_call_probe.row[n].fn[k]=name[k];
      return;
    }
    at=record[3];
  }
}
static uint64_t missing_call(void) {
  note_missing_call((uint32_t)(uintptr_t)__builtin_return_address(0));
  if(dc_goal_sticky_error==DC_GOAL_OK)dc_goal_sticky_error=DC_GOAL_BAD_FUNCTION;
  if(active_runtime) active_runtime->error=DC_GOAL_BAD_FUNCTION;
  return 0;
}
/* The original arming body stores this address. Execution through the original
 * architecture-specific bootstrap is unsupported and fails closed. (AI-assisted) */
int dc_goal_install_main_start_guard(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
 unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 dc_goal_code_export function={"set-to-run-bootstrap",code,12,NULL,0};
 uint32_t target=(uint32_t)(uintptr_t)missing_call;
 if(!rt || active_runtime!=rt || dc_goal_fiber_current()) {if(rt)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
 memcpy(code+8,&target,4);return dc_goal_load_module(rt,&function,1,sync,NULL,result);
}
int dc_goal_install_core_bridges(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  /* MOV.L literal,R0; JMP @R0; NOP; alignment NOP; .long C entry.
   * Tail dispatch leaves PR and outgoing GCC stack arguments intact. */
  unsigned char code[3][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},
                            {1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},
                            {1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
  /* The shared SH-4 helpers lowered code calls (sh4_helpers.py) link here too. */
  dc_goal_code_export functions[3+DC_SH4_CORE_HELPER_COUNT]={{"dc-c-pair-new",code[0],12,NULL,0},
                                    {"dc-c-type-of",code[1],12,NULL,0},
                                    {"dc-c-call-error",code[2],12,NULL,0},DC_SH4_CORE_HELPERS};
  uint32_t target,entry,global,global_value=0;
  if (!rt || !result) return 0;
  if (active_runtime && active_runtime!=rt) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  if(refuse_live_scope(rt))return 0;
  global=dc_goal_find(rt,"global");
  if (global) global_value=dc_goal_symbol_value(rt,global);
  if (!dc_goal_bind_fixed(rt,DC_FIX_SYM_GLOBAL_HEAP,"global",global_value)) return 0;
  target=(uint32_t)(uintptr_t)new_pair; memcpy(code[0]+8,&target,4);
  target=(uint32_t)(uintptr_t)type_of; memcpy(code[1]+8,&target,4);
  target=(uint32_t)(uintptr_t)missing_call; memcpy(code[2]+8,&target,4);
  if (!dc_goal_load_module(rt,functions,3+DC_SH4_CORE_HELPER_COUNT,sync,NULL,result)) return 0;
  entry=dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-pair-new"));
  if (!dc_goal_set_method(rt,rt->pair_type,0,entry)) return 0;
  active_runtime=rt;
  return 1;
}
void dc_goal_clear_core_bridges(dc_goal_runtime *rt) {
  if (active_runtime==rt && rt) {
    dc_goal_pp_scope_stack *stack;
    if(dc_goal_fiber_current()){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return;}
    if(refuse_live_scope(rt))return;
    stack=dc_goal_pp_scope_active(rt);
    if(stack && !dc_goal_pp_scope_detach(stack,rt))return;
    dc_goal_clear_process_context(rt);
    if(!dc_goal_process_spans_detach(rt))return;
    active_runtime=NULL;
  }
}
int dc_goal_core_bridges_active(const dc_goal_runtime *rt) {
  return rt && active_runtime==rt;
}
int dc_goal_install_type_bridges(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  const unsigned char stub[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  unsigned char code[8][12];
  dc_goal_code_export functions[8]={{"dc-c-type-new",code[0],12,NULL,0},
                                    {"method-set!",code[1],12,NULL,0},
                                    {"dc-c-basic-new",code[2],12,NULL,0},
                                    {"dc-c-structure-new",code[3],12,NULL,0},
                                    {"dc-c-zero-func",code[4],12,NULL,0},
                                    {"dc-c-copy-fixed",code[5],12,NULL,0},
                                    {"dc-c-basic-asize-of",code[6],12,NULL,0},
                                    {"dc-c-basic-copy",code[7],12,NULL,0}};
  uint32_t targets[8]={(uint32_t)(uintptr_t)new_type,(uint32_t)(uintptr_t)method_set,
                       (uint32_t)(uintptr_t)new_basic,(uint32_t)(uintptr_t)new_structure,
                       (uint32_t)(uintptr_t)asize_zero,(uint32_t)(uintptr_t)copy_fixed,
                       (uint32_t)(uintptr_t)asize_of_basic,(uint32_t)(uintptr_t)copy_basic};
  unsigned i;
  if (!rt || !result) return 0;
  if (active_runtime!=rt) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  for (i=0;i<8;++i) { memcpy(code[i],stub,12); memcpy(code[i]+8,&targets[i],4); }
  if (!dc_goal_load_module(rt,functions,8,sync,NULL,result)) return 0;
#define BRIDGE(name) dc_goal_symbol_value(rt,dc_goal_find(rt,name))
  /* kscheme defaults (goal_constants.h: length 4, asize-of 5, copy 6): object
   * length and asize-of are the zero function and copy is copy_fixed; basic
   * keeps the zero length and has asize_of_basic and copy_basic. All propagate
   * to fixed kernel types made before this, like structure new below. Original
   * process relocate walks a heap with asize-of, so a basic without its own
   * (trsqv) must get asize_of_basic here, not an empty slot 5. */
  if (!dc_goal_method_set(rt,rt->object_type,4,BRIDGE("dc-c-zero-func"),1) ||
      !dc_goal_method_set(rt,rt->object_type,5,BRIDGE("dc-c-zero-func"),1) ||
      !dc_goal_method_set(rt,rt->object_type,6,BRIDGE("dc-c-copy-fixed"),1) ||
      !dc_goal_method_set(rt,rt->basic_type,4,BRIDGE("dc-c-zero-func"),1) ||
      !dc_goal_method_set(rt,rt->basic_type,5,BRIDGE("dc-c-basic-asize-of"),1) ||
      !dc_goal_method_set(rt,rt->basic_type,6,BRIDGE("dc-c-basic-copy"),1)) return 0;
#undef BRIDGE
  return dc_goal_set_method(rt,rt->type_type,0,dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-type-new"))) &&
         dc_goal_set_method(rt,rt->basic_type,0,dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-basic-new"))) &&
         /* Propagate like method-set!: fixed kernel types such as kheap were made
          * before this bridge and still inherit the old slot (basic's is kept). */
         dc_goal_method_set(rt,rt->structure_type,0,dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-structure-new")),1);
}
int dc_goal_install_integer_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  uint32_t target=(uint32_t)(uintptr_t)integer_error;
  /* The division helpers (sh4_helpers.py) fault through this bridge. */
  dc_goal_code_export functions[1+DC_SH4_INTEGER_HELPER_COUNT]={{"dc-c-integer-error",code,12,NULL,0},
                                                                DC_SH4_INTEGER_HELPERS};
  if (!rt || !result) return 0;
  if (active_runtime!=rt) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  memcpy(code+8,&target,4);
  return dc_goal_load_module(rt,functions,1+DC_SH4_INTEGER_HELPER_COUNT,sync,NULL,result);
}


int dc_goal_install_object_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  uint32_t target=(uint32_t)(uintptr_t)new_object;
  dc_goal_code_export function={"dc-c-object-new",code,12,NULL,0};
  if (!rt || !result) return 0;
  if (active_runtime!=rt) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  memcpy(code+8,&target,4);
  if (!dc_goal_load_module(rt,&function,1,sync,NULL,result)) return 0;
  return dc_goal_set_method(rt,rt->object_type,0,dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-object-new")));
}


int dc_goal_bind_process_heap(dc_goal_runtime *rt,uint32_t process,uint32_t allocation_bytes) {
  if(!rt || active_runtime!=rt) {
    if(rt) (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);
    return 0;
  }
  /* Invalid same-runtime binding must not retain a previous allocation target. */
  if(refuse_live_scope(rt))return 0;
  dc_goal_clear_process_context(rt);
  if(!known_span_matches(rt,process,allocation_bytes))return 0;
  return dc_goal_process_heap_open(&active_process_heap,rt,process,allocation_bytes);
}
void dc_goal_clear_process_heap(dc_goal_runtime *rt) {
  dc_goal_clear_process_context(rt);
}

int dc_goal_register_pp_scope(dc_goal_runtime *rt,dc_goal_pp_scope_stack *stack) {
  const dc_goal_pp_owner_hooks hooks={dc_goal_kos_stack_owner};
  if(!rt || active_runtime!=rt){if(rt && rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  return dc_goal_pp_scope_init(stack,rt,&hooks);
}
int dc_goal_enter_lexical_scope(dc_goal_runtime *rt,uint32_t process,uint32_t bytes) {
  dc_goal_pp_scope_stack *stack;
  uint32_t context,scheduler,scheduler_bytes;
  if(!rt || active_runtime!=rt ||
     (execution_heap.runtime!=rt && !root_context(rt,&context))){if(rt && rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  stack=dc_goal_pp_scope_active(rt);
  if(!stack){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  if(!known_span_matches(rt,process,bytes))return 0;
  scheduler_identity(rt,&context,&scheduler,&scheduler_bytes);
  return dc_goal_pp_scope_enter(stack,rt,context,scheduler,scheduler_bytes,
                                process,bytes,&active_process_heap,&active_process_heap);
}
int dc_goal_leave_lexical_scope(dc_goal_runtime *rt,uint32_t process,uint32_t bytes) {
  dc_goal_pp_scope_stack *stack;
  if(!rt || active_runtime!=rt){if(rt && rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  stack=dc_goal_pp_scope_active(rt);
  if(!stack){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  return dc_goal_pp_scope_leave(stack,rt,process,bytes,&active_process_heap);
}
void dc_goal_lexical_native_return(dc_goal_runtime *rt) {
  enum dc_goal_error prior;int result;
  if(!rt || active_runtime!=rt)return;
  prior=rt->error;result=dc_goal_pp_scope_native_return(rt,&active_process_heap);
  if(result==DC_GOAL_PP_SCOPE_ABSENT)return;
  if(result==DC_GOAL_PP_SCOPE_DENIED)dc_goal_clear_process_context(rt);
  rt->error=prior==DC_GOAL_OK?DC_GOAL_BAD_ARGUMENT:prior;
}
static int process_held(uint32_t process);
static void release_moved_fibers(dc_goal_runtime *rt,uint32_t process);
/* A noted process nothing holds: no catch frame or live fiber, no lexical scope
 * frame (its process, scheduler or saved allocation), and neither allocation
 * binding. The original dead pool reused its memory, so its ledger row may go. */
static int span_unheld(const dc_goal_runtime *rt,uint32_t process){
  dc_goal_pp_scope_stack *stack=dc_goal_pp_scope_active((dc_goal_runtime *)rt);unsigned i;
  release_moved_fibers((dc_goal_runtime *)rt,process);
  if(process_held(process) || active_process_heap.process==process || execution_heap.process==process)return 0;
  for(i=0;stack && i<stack->depth;++i)
    if(stack->frames[i].lexical_process==process || stack->frames[i].scheduler_process==process ||
       stack->frames[i].saved_allocation.process==process)return 0;
  return 1;
}
int dc_goal_register_process_spans(dc_goal_runtime *rt,dc_goal_process_spans *table,dc_goal_process_span *rows,uint32_t capacity) {
  const dc_goal_process_span_hooks hooks={dc_goal_kos_stack_owner,spans_quiescent,span_unheld};
  if(!rt || active_runtime!=rt){if(rt&&rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  if(process_family(rt,dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_PROCESS_TYPE))!=1)return 0;
  return dc_goal_process_spans_init(table,rt,rows,capacity,&hooks);
}
int dc_goal_note_process_span(dc_goal_runtime *rt,uint32_t process,uint32_t bytes,dc_goal_process_span *out) {
  if(!rt || active_runtime!=rt){if(rt&&rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  return dc_goal_process_span_note(rt,process,bytes,out);
}
/* Dead-pool members are carved by GOAL, so no C allocator notes them. Root
 * startups note allocated-length+112 after the heap validator. Jak start() is
 * the same spawn with the listener bound; only that ELF admits the caller. */
#ifdef DC_ARENA_JAK_PLAYER
/* Also on a running fiber: its process context is bound by thread-resume, as
 * the listener is for start (a process spawning a child from its own code). */
#define DC_GOAL_BOUND_SPAWN_ADMIT(rt) (execution_context&&execution_heap.runtime==(rt)&&dc_goal_pp_scope_active(rt)&&dc_goal_process_spans_registered(rt)==1)
#else
#define DC_GOAL_BOUND_SPAWN_ADMIT(rt) ((void)(rt),0)
#endif
#ifdef DC_ARENA_JAK_PLAYER
/* Diagnostic: each condition note_root_member tests, per call (last 4 kept). */
typedef struct {
  uint32_t process,root,exec_context,exec_heap_ours,fiber,pp_active,spans,resolved;
  uint32_t length,heap_open,span_note,error;
} dc_goal_root_member_row;
volatile struct {uint32_t version,count;dc_goal_root_member_row row[4];} dc_goal_root_member_probe={.version=1};
#define ROOT_PROBE(field,value) (probe_row?(probe_row->field=(uint32_t)(value)):0u)
#else
#define ROOT_PROBE(field,value) 0u
#endif
static int note_root_member(dc_goal_runtime *rt,uint32_t process,dc_goal_process_span *span) {
  enum dc_goal_error prior=rt->error;dc_goal_process_heap candidate;const unsigned char *fields;
  uint32_t context,length;int root;
#ifdef DC_ARENA_JAK_PLAYER
  volatile dc_goal_root_member_row *probe_row=&dc_goal_root_member_probe.row[dc_goal_root_member_probe.count++&3u];
  memset((void *)probe_row,0xff,sizeof *probe_row);
  probe_row->process=process;probe_row->exec_context=execution_context;
  probe_row->exec_heap_ours=execution_heap.runtime==rt;probe_row->fiber=dc_goal_fiber_current()!=0;
  probe_row->pp_active=dc_goal_pp_scope_active(rt)!=0;probe_row->spans=dc_goal_process_spans_registered(rt);
#endif
  root=root_context(rt,&context);
  (void)ROOT_PROBE(root,root);
  if(!(root||DC_GOAL_BOUND_SPAWN_ADMIT(rt)) || process<4 || !(fields=dc_goal_resolve(rt,process,108)))return 0;
  (void)ROOT_PROBE(resolved,1);
  memcpy(&length,fields+68,4);
  (void)ROOT_PROBE(length,length);
  if(length>UINT32_MAX-112u){if(prior==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  rt->error=DC_GOAL_OK;
  if(!dc_goal_process_heap_open(&candidate,rt,process,length+112u)){(void)ROOT_PROBE(heap_open,0);(void)ROOT_PROBE(error,rt->error);return 0;}
  (void)ROOT_PROBE(heap_open,1);
  if(!dc_goal_process_span_note(rt,process,length+112u,span)){(void)ROOT_PROBE(span_note,0);(void)ROOT_PROBE(error,rt->error);return 0;}
  (void)ROOT_PROBE(span_note,1);
  rt->error=prior;return 1;
}
static int process_held(uint32_t process);
static void release_moved_fibers(dc_goal_runtime *rt,uint32_t process);
int dc_goal_enter_known_scope(dc_goal_runtime *rt,uint32_t process) {
  dc_goal_process_span span;enum dc_goal_error prior;uint32_t site;
  if(!rt || active_runtime!=rt){if(rt&&rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  prior=rt->error;site=dc_goal_error_site;
  if(!dc_goal_process_span_lookup(rt,process,&span)){
    rt->error=prior;dc_goal_error_site=site;  /* a miss is not yet a refusal */
    if(!note_root_member(rt,process,&span)){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  } else {
    /* A record larger than its row is a newer process the dead pool carved at
     * the same address as a shrunk one (a record only ever shrinks below its
     * row). Noting it drops the stale row when nothing holds that process. */
    const unsigned char *fields=dc_goal_resolve(rt,process,108);uint32_t length=0;
    if(fields)memcpy(&length,fields+68,4);
    rt->error=prior;dc_goal_error_site=site;
    if(fields && length<=UINT32_MAX-112u && span.bytes<length+112u &&
       !note_root_member(rt,process,&span)){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
    /* A record smaller than its row was shrunk (shrink-heap) after the row was
     * noted, and no scope was left since to shrink the row. A spawned fiber
     * imported for an earlier process at this address is released first; with
     * nothing holding the process, the row shrinks to the record now. */
    if(fields && length<=UINT32_MAX-112u && span.bytes>length+112u){
      release_moved_fibers(rt,process);
      if(!process_held(process)){
        if(!dc_goal_process_span_shrink(rt,&span,length+112u,NULL))return 0;
        span.bytes=length+112u;
      }
    }
  }
  return dc_goal_enter_lexical_scope(rt,process,span.bytes);
}
/* Original dead-pool-heap shrink-heap (run-function-in-process, once an init's
 * go left the process waiting-to-run) makes the process's own record smaller than
 * the extent noted when its scope was first entered. The pool then carves the
 * next process right after the shrunk heap. So when a process's last scope is
 * left, and no catch frame or fiber holds it, its ledger row shrinks to its
 * record: allocated-length plus the 112-byte process. */
static int process_held(uint32_t process);
static int shrink_left_span(dc_goal_runtime *rt,uint32_t process) {
  dc_goal_pp_scope_stack *stack=dc_goal_pp_scope_active(rt);dc_goal_process_span span;
  enum dc_goal_error prior=rt->error;uint32_t site=dc_goal_error_site,length;unsigned i;
  const unsigned char *fields;
  if(!stack || process_held(process))return 1;
  for(i=0;i<stack->depth;++i)if(stack->frames[i].lexical_process==process)return 1;
  if(!(fields=dc_goal_resolve(rt,process,108)) || !dc_goal_process_span_lookup(rt,process,&span)) {
    rt->error=prior;dc_goal_error_site=site;return 1;  /* not a noted process */
  }
  memcpy(&length,fields+68,4);
#ifdef DC_ARENA_JAK_PLAYER
  if(length<=UINT32_MAX-112u && span.bytes>length+112u)
    dc_arena_jak_player_note_shrink(rt,process,span.bytes);
#endif
  if(length>UINT32_MAX-112u || span.bytes<=length+112u)return 1;
  return dc_goal_process_span_shrink(rt,&span,length+112u,NULL);
}
int dc_goal_leave_known_scope(dc_goal_runtime *rt,uint32_t previous_process) {
  dc_goal_pp_scope_stack *stack;dc_goal_pp_scope_frame *top;uint32_t process;
  if(!rt || active_runtime!=rt){if(rt&&rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  stack=dc_goal_pp_scope_active(rt);
  if(!stack || !stack->depth){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  top=&stack->frames[stack->depth-1];
  /* The outermost frame restores its scheduler (#f at the root, which owns no
   * allocation record); a nested one restores the enclosing lexical process. */
  if((stack->depth==1?top->scheduler_process:top->saved_allocation.process)!=previous_process){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  process=top->lexical_process;
  return dc_goal_leave_lexical_scope(rt,process,top->lexical_bytes) && shrink_left_span(rt,process);
}

#ifdef DC_ARENA_JAK_PLAYER
/* Hung-dispatcher heartbeat. goal_fiber.c / goal_main_thread.c are not built
 * with this flag; the bridges below are. seq is published last. A frozen seq
 * means the loop left these bridges. The ring slot of that sample is
 * (seq-1)&7. --dump dc_arena_jak_player_heartbeat 180 */
enum {
  DC_JAK_BEAT_RESUME_BEFORE=1,
  DC_JAK_BEAT_RESUME_YIELD=2,
  DC_JAK_BEAT_RESUME_RETURN=3,
  DC_JAK_BEAT_SUSPEND=4,
  DC_JAK_BEAT_SCOPE_ENTER=5,
  DC_JAK_BEAT_SCOPE_LEAVE=6,
  DC_JAK_BEAT_RESET_BEFORE=7,
  DC_JAK_BEAT_RESET_AFTER=8,
  DC_JAK_BEAT_PAD=9,
  DC_JAK_BEAT_RESUME_FAIL=10
};
enum {DC_JAK_BEAT_RING=8};
typedef struct {
  uint32_t seq,site,process,name,thread,phase,event,depth,status;
  uint32_t ring_process[DC_JAK_BEAT_RING];
  uint32_t ring_site[DC_JAK_BEAT_RING];
  uint32_t ring_name[DC_JAK_BEAT_RING];
  char type_name[24];  /* the type symbol name of process */
  char process_name[24];  /* the process name string */
} dc_jak_player_heartbeat;
volatile dc_jak_player_heartbeat dc_arena_jak_player_heartbeat;
typedef char dc_arena_jak_player_heartbeat_bytes[sizeof(dc_jak_player_heartbeat)==180?1:-1];
/* Read-only shape of *active-pool* at a yield. Brother and child are ppointers.
 * The word at a ppointer is the object; that object's self (+24) is the process
 * execute-process-tree recurses on. error and dc_goal_error_site are restored. */
enum {DC_JAK_TREE_CAP=256,DC_JAK_TREE_KEEP=16,DC_JAK_TREE_CHILD=1,DC_JAK_TREE_BROTHER=2};
enum {DC_JAK_PT_BROTHER=12,DC_JAK_PT_CHILD=16,DC_JAK_PT_SELF=24,DC_JAK_PT_STATUS=32};
typedef struct {
  uint32_t process;
  uint32_t type_name; /* symbol word at the type object +0; 0 if unread */
  uint32_t status;    /* process+32 */
} dc_jak_tree_visit;
typedef struct {
  uint32_t seq;          /* heartbeat seq of this yield */
  uint32_t nodes;        /* visits, including the repeated node on a cycle */
  uint32_t max_depth;    /* *active-pool* is depth 0 */
  uint32_t cycle;        /* 1 if a process address occurred twice */
  uint32_t stop;         /* 0 done, 1 cycle, 2 cap, 3 unresolved link */
  uint32_t root;         /* *active-pool* symbol value */
  uint32_t from_process; /* node whose child or brother held the edge */
  uint32_t from_type;
  uint32_t from_status;
  uint32_t to_process;   /* process that edge dereferenced to */
  uint32_t to_type;
  uint32_t to_status;
  uint32_t link;         /* 1 child, 2 brother, 0 none */
  uint32_t fault_pp;     /* ppointer that did not become a process; else 0 */
  dc_jak_tree_visit visit[DC_JAK_TREE_KEEP];
} dc_jak_player_tree_walk;
volatile dc_jak_player_tree_walk dc_arena_jak_player_tree_walk;
typedef char dc_arena_jak_player_tree_walk_bytes[sizeof(dc_jak_player_tree_walk)==248?1:-1];
static int jak_tree_u32(dc_goal_runtime *rt,uint32_t addr,uint32_t *out) {
  const unsigned char *p;
  if(addr<4u || addr>UINT32_MAX-3u) return 0;
  p=dc_goal_resolve(rt,addr,4);
  if(!p) return 0;
  memcpy(out,p,4); return 1;
}
static int jak_tree_object(dc_goal_runtime *rt,uint32_t process,unsigned char raw[36]) {
  const unsigned char *p;
  if(process<4u || process>UINT32_MAX-35u) return 0;
  p=dc_goal_resolve(rt,process,36);
  if(!p) return 0;
  memcpy(raw,p,36); return 1;
}
static uint32_t jak_tree_type_name(dc_goal_runtime *rt,uint32_t process) {
  uint32_t type=0,symbol=0; const unsigned char *p;
  if(process<8u || process>UINT32_MAX-3u) return 0;
  p=dc_goal_resolve(rt,process-4u,4);
  if(!p) return 0;
  memcpy(&type,p,4);
  if(type<4u || type>UINT32_MAX-3u) return 0;
  p=dc_goal_resolve(rt,type,4);
  if(!p) return 0;
  memcpy(&symbol,p,4); return symbol;
}
static uint32_t jak_tree_at(const unsigned char raw[36],unsigned off) {
  uint32_t v; memcpy(&v,raw+off,4); return v;
}
static int jak_tree_seen(const uint32_t *seen,uint32_t n,uint32_t process) {
  uint32_t i; for(i=0;i<n;++i) if(seen[i]==process) return 1; return 0;
}
/* 1 = process, 0 = could not. *slot is the word at the ppointer. */
static int jak_tree_deref(dc_goal_runtime *rt,uint32_t pp,uint32_t *process,uint32_t *slot) {
  uint32_t obj=0,self=0;
  *process=0; *slot=0;
  if(!jak_tree_u32(rt,pp,&obj)) return 0;
  *slot=obj;
  if(!obj || obj==rt->s7 || obj>UINT32_MAX-27u || !jak_tree_u32(rt,obj+DC_JAK_PT_SELF,&self)) return 0;
  *process=self;
  return self && self!=rt->s7;
}
static int jak_tree_note(dc_jak_player_tree_walk *out,uint32_t *seen,uint32_t *unique,
                         uint32_t process,uint32_t type,uint32_t status,uint32_t depth,
                         uint32_t from_process,uint32_t from_type,uint32_t from_status,uint32_t link) {
  if(jak_tree_seen(seen,*unique,process)) {
    out->cycle=1; out->stop=1; out->nodes=*unique+1u; out->link=link;
    if(depth>out->max_depth) out->max_depth=depth;
    out->from_process=from_process; out->from_type=from_type; out->from_status=from_status;
    out->to_process=process; out->to_type=type; out->to_status=status;
    if(*unique<DC_JAK_TREE_KEEP) {
      out->visit[*unique].process=process; out->visit[*unique].type_name=type;
      out->visit[*unique].status=status;
    }
    return 1;
  }
  if(*unique>=DC_JAK_TREE_CAP) { out->stop=2; out->nodes=*unique; return 1; }
  seen[*unique]=process;
  if(*unique<DC_JAK_TREE_KEEP) {
    out->visit[*unique].process=process; out->visit[*unique].type_name=type;
    out->visit[*unique].status=status;
  }
  if(depth>out->max_depth) out->max_depth=depth;
  *unique+=1u; out->nodes=*unique; return 0;
}
/* *kernel-context* words at each tree walk: relocating-process/min/max/offset
 * show whether dead-pool-heap compaction relocated a process.
 * Words 16..79: 256 bytes from the stop-3 fault target minus 4.
 * Words 80..143: 16-byte type names of the first 16 walk visits.
 * --dump dc_arena_jak_player_kernel_context 576 */
volatile uint32_t dc_arena_jak_player_kernel_context[144];
/* Method 5 (asize-of) and 7 (relocate) of the relocated types, and the GOAL
 * function names. --dump dc_arena_jak_player_methods 448 */
volatile struct { uint32_t type,m4,m7; char n4[24],n7[24]; } dc_arena_jak_player_methods[7];
/* Code registry entries of the original kernel asize-of methods (0 = absent):
 * process, cpu-thread, dead-pool-heap, then the registry count. */
volatile uint32_t dc_arena_jak_player_asize_entries[4];
#ifdef DC_JAK_HEAVY_PROBES
static void jak_asize_entries(dc_goal_runtime *rt) {
  static const char *const want[3]={"(method asize-of process)","(method asize-of cpu-thread)","(method asize-of dead-pool-heap)"};
  uint32_t at=rt->code_registry_head,i,k;
  dc_arena_jak_player_asize_entries[3]=rt->code_registry_count;
  for(i=0;i<rt->code_registry_count && at;++i) {
    uint32_t record[4],offset=at-rt->base;const char *n;
    if(at<rt->base || offset>rt->used || 16u>rt->used-offset)return;
    memcpy(record,rt->memory+offset,16);
    n=dc_goal_symbol_name(rt,record[2]);
    for(k=0;k<3u;++k)if(n && !strcmp(n,want[k]))dc_arena_jak_player_asize_entries[k]=record[0];
    at=record[3];
  }
}
static void jak_methods(dc_goal_runtime *rt) {
#ifdef DC_JAK_HEAVY_PROBES
  jak_asize_entries(rt);
#endif
  static const char *const names[7]={"process","process-drawable","manipy","hud","hud-fuel-cell","cpu-thread","trsqv"};
  unsigned i;
  for(i=0;i<7u;++i) {
    uint32_t s=dc_goal_find(rt,names[i]),type=s?jak_guest_word(rt,s):0;
    dc_arena_jak_player_methods[i].type=type;
    dc_arena_jak_player_methods[i].m4=type?dc_goal_get_method(rt,type,5):0;
    dc_arena_jak_player_methods[i].m7=type?dc_goal_get_method(rt,type,7):0;
    jak_copy_name(dc_arena_jak_player_methods[i].n4,jak_function_name(rt,dc_arena_jak_player_methods[i].m4),24);
    jak_copy_name(dc_arena_jak_player_methods[i].n7,jak_function_name(rt,dc_arena_jak_player_methods[i].m7),24);
  }
}
#endif
static void jak_kernel_context(dc_goal_runtime *rt) {
#ifdef DC_JAK_HEAVY_PROBES
  jak_methods(rt);
#endif
  uint32_t symbol=dc_goal_find(rt,"*kernel-context*"),value,i;
  value=symbol?jak_guest_word(rt,symbol):0;
  dc_arena_jak_player_kernel_context[0]=value;
  for(i=1;i<16;++i)dc_arena_jak_player_kernel_context[i]=value?jak_guest_word(rt,value+4u*(i-1u)):0;
}
/* Relocations seen at heartbeat sites and inside the default asize-of the
 * original process relocate loop calls: a row when relocating-process, min
 * or offset changes. --dump dc_arena_jak_player_reloc 1160 */
typedef struct { uint32_t seq,site,process,min,max,offset,ppointer,rec_process,self; } dc_jak_reloc_row;
volatile struct { uint32_t count,pad; dc_jak_reloc_row row[32]; } dc_arena_jak_player_reloc;
typedef char dc_jak_reloc_bytes[sizeof(dc_arena_jak_player_reloc)==1160?1:-1];
static uint32_t jak_reloc_last[3];
static void jak_reloc_note(dc_goal_runtime *rt,uint32_t site) {
  static uint32_t symbol; uint32_t context,proc,min,offset,n;
  dc_jak_reloc_row row;
  if(!rt)return;
  if(!symbol){enum dc_goal_error e=rt->error;symbol=dc_goal_find(rt,"*kernel-context*");rt->error=e;}
  if(!symbol || (context=jak_guest_word(rt,symbol))==0xffffffffu)return;
  proc=jak_guest_word(rt,context+24u); min=jak_guest_word(rt,context+28u); offset=jak_guest_word(rt,context+36u);
  if(proc==jak_reloc_last[0] && min==jak_reloc_last[1] && offset==jak_reloc_last[2])return;
  jak_reloc_last[0]=proc; jak_reloc_last[1]=min; jak_reloc_last[2]=offset;
  row.seq=dc_arena_jak_player_heartbeat.seq; row.site=site; row.process=proc; row.min=min;
  row.max=jak_guest_word(rt,context+32u); row.offset=offset;
  row.ppointer=jak_guest_word(rt,proc+20u); row.rec_process=jak_guest_word(rt,row.ppointer);
  row.self=jak_guest_word(rt,proc+24u);
  n=dc_arena_jak_player_reloc.count++;
  {const uint32_t *w=(const uint32_t *)(const void *)&row;volatile uint32_t *d=(volatile uint32_t *)&dc_arena_jak_player_reloc.row[n&31u];unsigned i;
   for(i=0;i<9u;++i)d[i]=w[i];}
}
/* Every unique walk node (up to 32) with its type name and the name of its
 * state, and its status symbol. --dump dc_arena_jak_player_nodes 2176 */
volatile struct { uint32_t process,state; char type[20],state_name[24],status[16]; } dc_arena_jak_player_nodes[32];
/* PC sampler: the watchdog interval timer records the interrupted PC and PR,
 * with the GOAL function holding each. Shows where a hang without bridge calls
 * spins. Build with -DDC_JAK_HEAVY_PROBES (they slow every yield).
 * --dump dc_arena_jak_player_pc 560 */
#include <arch/irq.h>
volatile struct {
  uint32_t count,pad[3];
  struct { uint32_t pc,pr; } ring[32];
  struct { uint32_t pc,pr; char pc_fn[32],pr_fn[32]; } named[4];
} dc_arena_jak_player_pc;
static irq_cb_t jak_pc_chained;
static dc_goal_runtime *jak_pc_runtime;
static void jak_pc_sample(irq_t code,irq_context_t *cx,void *data) {
  dc_goal_runtime *rt=jak_pc_runtime;uint32_t n,slot;
  if(cx) {
    n=dc_arena_jak_player_pc.count++;
    dc_arena_jak_player_pc.ring[n&31u].pc=cx->pc;dc_arena_jak_player_pc.ring[n&31u].pr=cx->pr;
    if(rt && !(n&15u)) {
      slot=(n>>4)&3u;
      dc_arena_jak_player_pc.named[slot].pc=cx->pc;dc_arena_jak_player_pc.named[slot].pr=cx->pr;
      jak_copy_name(dc_arena_jak_player_pc.named[slot].pc_fn,jak_function_name(rt,cx->pc),32);
      jak_copy_name(dc_arena_jak_player_pc.named[slot].pr_fn,jak_function_name(rt,cx->pr),32);
    }
  }
  (void)data;
  if(jak_pc_chained.hdl)jak_pc_chained.hdl(code,cx,jak_pc_chained.data);
}
/* Chains the KOS TMU0 handler (the scheduler tick): record, then run it. */
void dc_arena_jak_player_pc_start(dc_goal_runtime *rt) {
  jak_pc_runtime=rt;jak_pc_chained=irq_get_handler(EXC_TMU0_TUNI0);
  irq_set_handler(EXC_TMU0_TUNI0,jak_pc_sample,NULL);
}
static void jak_tree_walk(dc_goal_runtime *rt) {
  enum dc_goal_error saved=rt->error; uint32_t saved_site=dc_goal_error_site;
  dc_jak_player_tree_walk out; unsigned char raw[36];
  static uint32_t seen[DC_JAK_TREE_CAP];
  static struct { uint32_t pp,depth,from_process,from_type,from_status,link; } frame[DC_JAK_TREE_CAP];
  uint32_t unique=0,sp=0,symbol,root=0,proc,slot,pp,brother,child,type,status,i;
  uint32_t words[sizeof(dc_jak_player_tree_walk)/4u];
  memset(&out,0,sizeof out);
  out.seq=dc_arena_jak_player_heartbeat.seq;
  jak_kernel_context(rt);
  symbol=dc_goal_find(rt,"*active-pool*");
  if(!symbol || !jak_tree_u32(rt,symbol,&root) || !root || root==rt->s7 || !jak_tree_object(rt,root,raw)) {
    out.root=root; out.stop=3; out.to_process=root; goto publish;
  }
  out.root=root;
  type=jak_tree_type_name(rt,root); status=jak_tree_at(raw,DC_JAK_PT_STATUS);
  if(jak_tree_note(&out,seen,&unique,root,type,status,0,0,0,0,0)) goto publish;
  child=jak_tree_at(raw,DC_JAK_PT_CHILD);
  if(child && child!=rt->s7) {
    frame[0].pp=child; frame[0].depth=1; frame[0].from_process=root;
    frame[0].from_type=type; frame[0].from_status=status; frame[0].link=DC_JAK_TREE_CHILD;
    sp=1;
  }
  while(sp) {
    pp=frame[sp-1u].pp;
    if(!pp || pp==rt->s7) { --sp; continue; }
    proc=0; slot=0;
    if(!jak_tree_deref(rt,pp,&proc,&slot) || !jak_tree_object(rt,proc,raw)) {
      out.stop=3; out.fault_pp=pp; out.to_process=proc?proc:slot; out.link=frame[sp-1u].link;
      out.from_process=frame[sp-1u].from_process; out.from_type=frame[sp-1u].from_type;
      out.from_status=frame[sp-1u].from_status; break;
    }
    {uint32_t depth=frame[sp-1u].depth,from_process=frame[sp-1u].from_process;
     uint32_t from_type=frame[sp-1u].from_type,from_status=frame[sp-1u].from_status;
     uint32_t link=frame[sp-1u].link;
     brother=jak_tree_at(raw,DC_JAK_PT_BROTHER);
     type=jak_tree_type_name(rt,proc); status=jak_tree_at(raw,DC_JAK_PT_STATUS);
     child=jak_tree_at(raw,DC_JAK_PT_CHILD);
     /* Save the next brother before descending, as execute-process-tree does. */
     frame[sp-1u].pp=brother; frame[sp-1u].link=DC_JAK_TREE_BROTHER;
     if(jak_tree_note(&out,seen,&unique,proc,type,status,depth,from_process,from_type,from_status,link))
       break;
     if(child && child!=rt->s7) {
       if(sp>=DC_JAK_TREE_CAP) { out.stop=2; break; }
       frame[sp].pp=child; frame[sp].depth=depth+1u; frame[sp].from_process=proc;
       frame[sp].from_type=type; frame[sp].from_status=status; frame[sp].link=DC_JAK_TREE_CHILD;
       ++sp;
     }}
  }
publish:
#ifdef DC_JAK_HEAVY_PROBES
  for(i=0;i<32u;++i) {
    uint32_t p=i<unique?seen[i]:0,st=p?jak_guest_word(rt,p+52u):0;
    dc_arena_jak_player_nodes[i].process=p; dc_arena_jak_player_nodes[i].state=st;
    jak_copy_name(dc_arena_jak_player_nodes[i].type,p?jak_type_name(rt,jak_guest_word(rt,p-4u)):NULL,20);
    jak_copy_name(dc_arena_jak_player_nodes[i].state_name,
                  st && st!=rt->s7 && st!=0xffffffffu?dc_goal_symbol_name(rt,jak_guest_word(rt,st)):NULL,24);
    jak_copy_name(dc_arena_jak_player_nodes[i].status,p?dc_goal_symbol_name(rt,jak_guest_word(rt,p+32u)):NULL,16);
  }
#endif
  if(out.stop==3 && out.to_process>=4u)
    for(i=0;i<64u;++i)dc_arena_jak_player_kernel_context[16u+i]=jak_guest_word(rt,out.to_process-4u+4u*i);
#ifdef DC_JAK_HEAVY_PROBES
  for(i=0;i<16u && i<out.nodes;++i) {
    const char *n=dc_goal_symbol_name(rt,out.visit[i].type_name);char name[16];unsigned k;
    memset(name,0,sizeof name);
    for(k=0;n && n[k] && k<15u;++k)name[k]=n[k];
    for(k=0;k<4u;++k){uint32_t w;memcpy(&w,name+4u*k,4);dc_arena_jak_player_kernel_context[80u+4u*i+k]=w;}
  }
#endif
  memcpy(words,&out,sizeof out);
  for(i=0;i<sizeof words/sizeof words[0];++i) ((volatile uint32_t *)&dc_arena_jak_player_tree_walk)[i]=words[i];
  rt->error=saved; dc_goal_error_site=saved_site;
}
static void jak_beat(uint32_t site,uint32_t process,uint32_t thread,uint32_t phase,uint32_t event) {
  uint32_t seq=dc_arena_jak_player_heartbeat.seq+1u,slot=(seq-1u)&(DC_JAK_BEAT_RING-1u);
  uint32_t depth=0,status=0,name=0;dc_goal_runtime *rt=active_runtime;
  if(rt) {
    dc_goal_pp_scope_stack *stack=dc_goal_pp_scope_active(rt);
    enum dc_goal_error saved=rt->error;unsigned char *fields;
    if(stack)depth=stack->depth;
    if(process>=4u && process<=UINT32_MAX-35u && (fields=dc_goal_resolve(rt,process,36))) {
      memcpy(&name,fields,4);memcpy(&status,fields+32,4);
    }
    {/* type tag at process-4, its name symbol at type+0; the process name
      * field (process+0) is a GOAL string: length word, then bytes. */
     const unsigned char *tag=process>=8u?dc_goal_resolve(rt,process-4u,4):NULL,*type=NULL,*text=NULL;
     uint32_t type_address=0,symbol=0,length=0;const char *n=NULL;size_t k=0;
     if(tag){memcpy(&type_address,tag,4);type=type_address>=4u?dc_goal_resolve(rt,type_address,4):NULL;}
     if(type){memcpy(&symbol,type,4);n=dc_goal_symbol_name(rt,symbol);}
     for(;n && n[k] && k<23;++k)dc_arena_jak_player_heartbeat.type_name[k]=n[k];
     for(;k<24;++k)dc_arena_jak_player_heartbeat.type_name[k]=0;
     if(name>=4u && dc_goal_resolve(rt,name,4)){memcpy(&length,dc_goal_resolve(rt,name,4),4);
       if(length<64u)text=dc_goal_resolve(rt,name+4u,length?length:1u);}
     for(k=0;text && k<length && k<23;++k)dc_arena_jak_player_heartbeat.process_name[k]=(char)text[k];
     for(;k<24;++k)dc_arena_jak_player_heartbeat.process_name[k]=0;}
    rt->error=saved;
  }
  jak_reloc_note(rt,site);
  dc_arena_jak_player_heartbeat.site=site;
  dc_arena_jak_player_heartbeat.process=process;
  dc_arena_jak_player_heartbeat.name=name;
  dc_arena_jak_player_heartbeat.thread=thread;
  dc_arena_jak_player_heartbeat.phase=phase;
  dc_arena_jak_player_heartbeat.event=event;
  dc_arena_jak_player_heartbeat.depth=depth;
  dc_arena_jak_player_heartbeat.status=status;
  dc_arena_jak_player_heartbeat.ring_process[slot]=process;
  dc_arena_jak_player_heartbeat.ring_site[slot]=site;
  dc_arena_jak_player_heartbeat.ring_name[slot]=name;
  dc_arena_jak_player_heartbeat.seq=seq;
  /* Every yield. The image that remains is the yield that never returns. */
  if(site==DC_JAK_BEAT_RESUME_YIELD && rt) jak_tree_walk(rt);
}
#endif
/* Compiled original (set! pp X) ... (set! pp saved) brackets. Both are boxed
 * 0/1 results; the lowerer exits before body memory access on zero. */
static uint64_t enter_process_scope(uint64_t process) {
  dc_goal_runtime *rt=active_runtime;
  if(!rt)return 0;
  if(process>UINT32_MAX){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
#ifdef DC_ARENA_JAK_PLAYER
  jak_beat(DC_JAK_BEAT_SCOPE_ENTER,(uint32_t)process,0,0,0);
#endif
  return (uint64_t)dc_goal_enter_known_scope(rt,(uint32_t)process);
}
static uint64_t leave_process_scope(uint64_t previous_process) {
  dc_goal_runtime *rt=active_runtime;
  if(!rt)return 0;
  if(previous_process>UINT32_MAX){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
#ifdef DC_ARENA_JAK_PLAYER
  {dc_goal_pp_scope_stack *stack=dc_goal_pp_scope_active(rt);uint32_t leaving=0;
   if(stack && stack->depth && stack->depth<=DC_GOAL_PP_SCOPE_MAX_DEPTH)
     leaving=stack->frames[stack->depth-1].lexical_process;
   jak_beat(DC_JAK_BEAT_SCOPE_LEAVE,leaving,(uint32_t)previous_process,0,0);}
#endif
  return (uint64_t)dc_goal_leave_known_scope(rt,(uint32_t)previous_process);
}
int dc_goal_install_scope_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  unsigned char code[2][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
  uint32_t targets[2]={(uint32_t)(uintptr_t)enter_process_scope,(uint32_t)(uintptr_t)leave_process_scope};
  dc_goal_code_export functions[2]={{"dc-c-enter-process-scope",code[0],12,NULL,0},
                                    {"dc-c-leave-process-scope",code[1],12,NULL,0}};
  if(!rt || !result) return 0;
  if(active_runtime!=rt) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  memcpy(code[0]+8,&targets[0],4); memcpy(code[1]+8,&targets[1],4);
  return dc_goal_load_module(rt,functions,2,sync,NULL,result);
}

/* Original (deactivate self) abandons the running thread to return-from-thread.
 * On a running native fiber for the bound scheduler process this finishes the
 * fiber to its root. Without such a kernel continuation (a direct C call) the
 * transfer refuses explicitly (an earlier error wins) and the body exits. */
static uint64_t return_from_thread(void) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_fiber *fiber=dc_goal_fiber_current();
  if(!rt)return 0;
  if(fiber && fiber->runtime==rt && execution_context && execution_heap.runtime==rt &&
     fiber->process==execution_heap.process && rt->error==DC_GOAL_OK)
    dc_goal_fiber_abandon(rt,0); /* returns only when the context is invalid */
  if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_UNSUPPORTED_TRANSFER;
  return 0;
}
/* Original thread-suspend/thread-resume status symbols (process status at +32). */
static void set_process_status(dc_goal_runtime *rt,uint32_t process,const char *name) {
  uint32_t symbol=dc_goal_intern(rt,name);unsigned char *p=dc_goal_resolve(rt,process+32,4);
  if(symbol && p)memcpy(p,&symbol,4);
}
/* Original (suspend): the thread saves itself and returns to the kernel, which
 * resumes it next frame. Natively the bound scheduler process's running fiber
 * yields; live lexical scopes and foreign contexts refuse inside the engine. */
static uint64_t suspend_current(void) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_fiber *fiber=dc_goal_fiber_current();
  if(!rt)return 0;
  if(!fiber || fiber->runtime!=rt || !execution_context || execution_heap.runtime!=rt ||
     fiber->process!=execution_heap.process) {
    if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_UNSUPPORTED_TRANSFER;
    return 0;
  }
  /* Original thread-suspend marks the process suspended before returning. */
  set_process_status(rt,fiber->process,"suspended");
#ifdef DC_ARENA_JAK_PLAYER
  jak_beat(DC_JAK_BEAT_SUSPEND,fiber->process,fiber->thread,(uint32_t)fiber->phase,0);
#endif
  dc_goal_fiber_yield(rt,0);
  return rt->error==DC_GOAL_OK;
}
/* Original cpu-thread resume-hook targets: registered native fibers by thread. */
#define DC_THREAD_FIBERS 200  /* a level's live actors, plus the player, cameras and the display */
static dc_goal_fiber *thread_fibers[DC_THREAD_FIBERS];
int dc_goal_register_thread_fiber(dc_goal_runtime *rt,dc_goal_fiber *fiber) {
  unsigned i,free_slot=DC_THREAD_FIBERS;
  if(!rt || active_runtime!=rt || !fiber || fiber->runtime!=rt || !fiber->thread || dc_goal_fiber_current()) {
    if(rt && rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);
    return 0;
  }
  if(!dc_goal_fiber_ownership_check(fiber,rt))return 0;
  for(i=0;i<DC_THREAD_FIBERS;++i) {
    if(thread_fibers[i] && (thread_fibers[i]==fiber || thread_fibers[i]->thread==fiber->thread)) {
      if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);
      return 0;
    }
    if(!thread_fibers[i] && free_slot==DC_THREAD_FIBERS)free_slot=i;
  }
  if(free_slot==DC_THREAD_FIBERS){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_OUT_OF_MEMORY;return 0;}
  thread_fibers[free_slot]=fiber;return 1;
}
int dc_goal_unregister_thread_fiber(dc_goal_runtime *rt,dc_goal_fiber *fiber) {
  unsigned i;
  if(!rt || dc_goal_fiber_current())return 0;
  for(i=0;i<DC_THREAD_FIBERS;++i)if(thread_fibers[i]==fiber){thread_fibers[i]=NULL;return 1;}
  return 0;
}
/* A process spawned by original GOAL (process-spawn) gets its native main fiber
 * the first time the dispatcher resumes it. Only an armed main thread on the
 * shared fake-scratchpad stack is imported (activate's PC-port stack for
 * process-spawn's default *scratch-memory-top*). The extent is the process's own
 * record (allocated-length + 112), noted in the ledger when one is registered. */
#define DC_SPAWNED_FIBERS 192  /* Geyser Rock with village1: 115 actors born; each slot is 336 bytes of bss */
static dc_goal_fiber spawned_fibers[DC_SPAWNED_FIBERS];
/* The process status word (+32) is the named symbol; no intern. */
static int process_status_is(dc_goal_runtime *rt,uint32_t process,const char *name) {
  enum dc_goal_error prior=rt->error;uint32_t symbol=dc_goal_find(rt,name),status=0;
  const unsigned char *p=symbol?dc_goal_resolve(rt,process+32u,4):NULL;
  if(p)memcpy(&status,p,4);
  rt->error=prior;
  return symbol && p && status==symbol;
}
static dc_goal_fiber *import_spawned(dc_goal_runtime *rt,uint32_t thread) {
  const unsigned char *p;uint32_t process,top,length,base,symbol;dc_goal_process_span span;unsigned i;
  enum dc_goal_error prior=rt->error;dc_goal_fiber *f=NULL;
  if(!(p=dc_goal_resolve(rt,thread+4,28)))return NULL;
  memcpy(&process,p,4);memcpy(&top,p+24,4);
  symbol=dc_goal_find(rt,"*fake-scratchpad-stack*");base=symbol?dc_goal_symbol_value(rt,symbol):0;
  rt->error=prior;
  if(!base || (base&15u) || top!=base+32u*1024u || process<4 || !(p=dc_goal_resolve(rt,process+68,4)))return NULL;
  memcpy(&length,p,4);
  if(length>UINT32_MAX-112u)return NULL;
  for(i=0;i<DC_SPAWNED_FIBERS && !f;++i)if(spawned_fibers[i].runtime!=rt || !spawned_fibers[i].thread)f=&spawned_fibers[i];
  if(!f)return NULL;
  if(dc_goal_process_spans_registered(rt)==1 && !dc_goal_process_span_lookup(rt,process,&span)) {
    rt->error=prior;
    if(!dc_goal_process_span_note(rt,process,length+112u,&span))return NULL;
  }
  rt->error=prior;
  if(!dc_goal_fiber_init_main(f,rt,process,length+112u,thread,base,32u*1024u)){memset(f,0,sizeof *f);return NULL;}
  if(!dc_goal_register_thread_fiber(rt,f)){memset(f,0,sizeof *f);return NULL;}
  return f;
}
/* The original dead pool is allocating over a noted process, so the process
 * that was there died or moved: original compaction (actors-update's compact
 * of the dead-pool-heap) copies a live process lower and repoints its pool
 * record, leaving a whole stale copy behind. A live process's ppointer (+20)
 * names a record that names the process back; the old copy's no longer does.
 * An imported spawned fiber still registered there (its stack is that memory)
 * is released, as thread_resume releases one whose thread address comes back;
 * the moved process imports a fresh fiber when its new thread first resumes.
 * The running fiber stays. */
static void release_moved_fibers(dc_goal_runtime *rt,uint32_t process){
  dc_goal_fiber *current=dc_goal_fiber_current();enum dc_goal_error prior=rt->error;
  uint32_t ppointer=0,named=0;const unsigned char *p;unsigned i;
  if(process>=4u && (p=dc_goal_resolve(rt,process+20u,4))){memcpy(&ppointer,p,4);
    if(ppointer && (p=dc_goal_resolve(rt,ppointer,4)))memcpy(&named,p,4);}
  rt->error=prior;
  {uint32_t pid=0;const unsigned char *q=process>=4u?dc_goal_resolve(rt,process+36u,4):NULL;if(q)memcpy(&pid,q,4);rt->error=prior;
  for(i=0;i<DC_THREAD_FIBERS;++i){
    dc_goal_fiber *fiber=thread_fibers[i];
    /* still that live process: its ppointer names it and the fiber was imported
     * for its PID (a newer process carved at the same address has another) */
    if(named==process && !process_status_is(rt,process,"dead") && (!fiber || !fiber->main_import_pid || fiber->main_import_pid==pid))continue;
    if(fiber && fiber->process==process && fiber!=current && fiber->phase!=DC_FIBER_RUNNING &&
       fiber>=spawned_fibers && fiber<spawned_fibers+DC_SPAWNED_FIBERS){
      /* Births run inside a fiber, where dc_goal_unregister_thread_fiber refuses;
       * this one is not running, so its registry slot is cleared directly. */
      thread_fibers[i]=NULL;memset(fiber,0,sizeof *fiber);
    }
  }}
}
/* Original thread-resume: called by the original kernel-dispatcher with the
 * scheduler's current-process already set to the thread's process. Natively the
 * registered fiber steps under an explicit execution binding; the dispatcher's
 * current-process value is restored afterwards for its own cleanup. */
/* Counters: trans hooks run (and finished) as temporary fibers; suspended main
 * fibers re-imported after an original set-to-run. */
volatile uint32_t dc_goal_trans_fibers,dc_goal_main_rearms;
static dc_goal_fiber hook_fiber;
static void rearm_main(dc_goal_runtime *rt,dc_goal_fiber *fiber,uint32_t trans);
static uint64_t thread_resume(uint64_t thread) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_fiber *fiber=NULL;
  uint32_t symbol,context,current;unsigned char *fields;uint64_t out=0;unsigned i;
  if(!rt)return 0;
  for(i=0;i<DC_THREAD_FIBERS;++i)if(thread_fibers[i] && thread_fibers[i]->thread==thread)fiber=thread_fibers[i];
  /* A go another process delivered (an event: enter-state's branch for a
   * process other than the current one) re-armed this main thread with
   * set-to-run while its fiber was suspended; import the armed image again so
   * this resume runs enter-state (rearm_main). */
  if(fiber && fiber->phase==DC_FIBER_SUSPENDED && fiber->main_import_self==(uintptr_t)fiber &&
     !dc_goal_fiber_current() && !execution_context)
    rearm_main(rt,fiber,0);
  /* A dead process's imported fiber stays registered by thread address. When
   * dead-pool-heap carves a new process in the same memory, its armed main
   * thread has that address too: a spawned fiber that can no longer resume,
   * or one suspended while the process says waiting-to-run, belongs to the
   * earlier process. Release it and import the new activation. */
  if(fiber && fiber>=spawned_fibers && fiber<spawned_fibers+DC_SPAWNED_FIBERS &&
     !dc_goal_fiber_current() && !execution_context &&
     (fiber->phase==DC_FIBER_DONE || fiber->phase==DC_FIBER_FAULT || fiber->phase==DC_FIBER_EMPTY ||
      (fiber->phase==DC_FIBER_SUSPENDED && fiber->process>=4u &&
       process_status_is(rt,fiber->process,"waiting-to-run")))) {
    dc_goal_unregister_thread_fiber(rt,fiber);memset(fiber,0,sizeof *fiber);fiber=NULL;
  }
  if(!fiber && thread<=UINT32_MAX && !dc_goal_fiber_current() && !execution_context)
    fiber=import_spawned(rt,(uint32_t)thread);
  if(!fiber || fiber->runtime!=rt || dc_goal_fiber_current() || execution_context)goto unsupported;
  if(!dc_goal_fiber_ownership_check(fiber,rt))return 0;
  symbol=dc_goal_find(rt,"*kernel-context*");context=symbol?dc_goal_symbol_value(rt,symbol):0;
  if(!context || !(fields=context_fields(rt,context)))goto unsupported;
  memcpy(&current,fields+20,4);
  if(current!=fiber->process)goto unsupported;
  /* Explicit binding requires a false current-process; restore it afterwards. */
  memcpy(fields+20,&rt->s7,4);
  if(!dc_goal_bind_process_context(rt,fiber->process,fiber->reservation)) {
    memcpy(fields+20,&current,4);return 0;
  }
  {uint32_t top=(uint32_t)thread;unsigned char *p=dc_goal_resolve(rt,fiber->process+44,4);if(p)memcpy(p,&top,4);}
  set_process_status(rt,fiber->process,"running");
#ifdef DC_ARENA_LIVE
  {int event;
   dc_arena_live_resume(rt,fiber->process,0);
#ifdef DC_ARENA_JAK_PLAYER
   jak_beat(DC_JAK_BEAT_RESUME_BEFORE,fiber->process,fiber->thread,(uint32_t)fiber->phase,0);
#endif
   event=dc_goal_fiber_step(fiber,0,&out);
#ifdef DC_ARENA_JAK_PLAYER
   jak_beat((event==DC_FIBER_YIELDED && rt->error==DC_GOAL_OK && fiber->phase==DC_FIBER_SUSPENDED)
            ?DC_JAK_BEAT_RESUME_YIELD:DC_JAK_BEAT_RESUME_RETURN,
            fiber->process,fiber->thread,(uint32_t)fiber->phase,(uint32_t)event);
#endif
   if(event==DC_FIBER_YIELDED && rt->error==DC_GOAL_OK && fiber->phase==DC_FIBER_SUSPENDED)
     dc_arena_live_resume(rt,fiber->process,1);}
#elif defined(DC_ARENA_CAMERA)
  {int event;
   dc_arena_camera_resume(rt,fiber->process,0);
   event=dc_goal_fiber_step(fiber,0,&out);
   if(event==DC_FIBER_YIELDED && rt->error==DC_GOAL_OK && fiber->phase==DC_FIBER_SUSPENDED)
     dc_arena_camera_resume(rt,fiber->process,1);}
#else
  dc_goal_fiber_step(fiber,0,&out);
#endif
/* Preserve prior diagnostic site numbers in all existing modes (AI-assisted). */
#ifndef DC_ARENA_LIVE
#line 761
#endif
  {enum dc_goal_error error=rt->error;dc_goal_clear_process_context(rt);rt->error=error;}
  memcpy(fields+20,&current,4);
  return 0;
unsupported:
#ifdef DC_ARENA_JAK_PLAYER
  jak_beat(DC_JAK_BEAT_RESUME_FAIL,fiber?fiber->process:0,thread<=UINT32_MAX?(uint32_t)thread:0,
           fiber?(uint32_t)fiber->phase:0,0);
#endif
  if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_UNSUPPORTED_TRANSFER;
  return 0;
}
/* Original enter-state main-thread branch: reset the main thread's stack and
 * run the new state's code, returning through return-from-thread-dead. On the
 * bound scheduler process's running fiber this restarts the fiber at `code`. */
static uint64_t enter_state_code(uint64_t code,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_fiber *fiber=dc_goal_fiber_current();
  uint64_t arguments[4]={a0,a1,a2,a3};
  if(!rt)return 0;
  if(code>UINT32_MAX){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  if(fiber && fiber->runtime==rt && execution_context && execution_heap.runtime==rt &&
     fiber->process==execution_heap.process && rt->error==DC_GOAL_OK)
    dc_goal_fiber_restart(rt,(uint32_t)code,4,arguments); /* returns only on refusal */
  if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_UNSUPPORTED_TRANSFER;
  return 0;
}
/* Original reset-and-call: make `thread` the top thread of its process, reset the
 * stack to the thread's stack-top and call func, returning to the kernel. Used by
 * the original dispatcher for trans/post hooks on temporary threads. Natively the
 * function runs on the owned working stack of the process's registered main
 * fiber, which is free while that fiber is suspended (its image is in the thread
 * backup). A temporary thread's original in-stack structure stays below the
 * span used. Same explicit binding protocol as thread_resume. */
/* The original post-hook stack: *kernel-dram-stack*, a caller-owned span. */
static uint32_t kernel_stack_base,kernel_stack_bytes;
int dc_goal_register_kernel_stack(dc_goal_runtime *rt,uint32_t base,uint32_t bytes) {
  if(!rt || active_runtime!=rt || dc_goal_fiber_current() || (base&15u) || (bytes&15u) || bytes<4096u ||
     bytes>UINT32_MAX-base || (bytes && !dc_goal_resolve(rt,base,bytes))) {
    if(rt && rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);
    return 0;
  }
  kernel_stack_base=base;kernel_stack_bytes=bytes;return 1;
}
void dc_goal_clear_kernel_stack(void){kernel_stack_base=kernel_stack_bytes=0;}
/* The temporary trans fiber's view is the process's main thread (the original
 * trans thread lives inside the main stack, which a thread view refuses), so
 * its entry is this trampoline: it makes the trans thread the top thread again,
 * as original reset-and-call does, and calls the hook. Installed on first use
 * as its own one-function module. */
static uint32_t trans_process,trans_thread,trans_func,trans_entry;
static uint64_t run_trans_hook(void) {
  dc_goal_runtime *rt=active_runtime;unsigned char *q;
  if(!rt || !(q=dc_goal_resolve(rt,trans_process+44,4)))return 0;
  memcpy(q,&trans_thread,4);
  return ((uint64_t (*)(void))(uintptr_t)trans_func)();
}
static void sync_trampoline(uint32_t allocation,uint32_t bytes,void *unused){
  (void)unused;
#ifdef __DREAMCAST__
  dcache_flush_range(allocation,bytes);icache_flush_range(allocation,bytes);
#else
  (void)allocation;(void)bytes;
#endif
}
static uint32_t trans_trampoline(dc_goal_runtime *rt) {
  static const unsigned char stub[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  unsigned char code[12];uint32_t target=(uint32_t)(uintptr_t)run_trans_hook,symbol;
  dc_goal_code_export function={"dc-c-run-trans-hook",code,12,NULL,0};dc_goal_module_result result;
  enum dc_goal_error prior=rt->error;
  if(trans_entry)return trans_entry;
  memcpy(code,stub,12);memcpy(code+8,&target,4);
  if(dc_goal_load_module(rt,&function,1,sync_trampoline,NULL,&result) && (symbol=dc_goal_find(rt,"dc-c-run-trans-hook")))
    trans_entry=dc_goal_symbol_value(rt,symbol);
  rt->error=prior;
  return trans_entry;
}
/* Original set-to-run from a trans hook's go re-arms the suspended main thread:
 * rreg0 enter-state, pc set-to-run-bootstrap, sp its stack top. The native
 * fiber still holds the old continuation, so the armed image is imported
 * again in place, as at the first import, and the next resume runs
 * enter-state on the main thread. enter-state reaches set-to-run here through
 * its branch for a process other than the current one, which restores the
 * status it found: 'running, which this bridge set for the hook, where the
 * original kernel leaves the status it dispatched on. set-to-run's own
 * 'waiting-to-run is put back for the import. The trans thread is the top
 * thread until the kernel deletes it; the import sees the main thread there,
 * which that delete leaves (trans 0: called from thread-resume, where the main
 * thread already is the top thread). */
static void rearm_main(dc_goal_runtime *rt,dc_goal_fiber *fiber,uint32_t trans) {
  enum dc_goal_error prior=rt->error;
  uint32_t boot,pc=0,sp=0,process=fiber->process,reservation=fiber->reservation,main=fiber->thread,base=fiber->stack_base,bytes=fiber->stack_bytes;
  unsigned char *top=dc_goal_resolve(rt,process+44,4);const unsigned char *q=dc_goal_resolve(rt,main+20,8);
  boot=dc_goal_find(rt,"set-to-run-bootstrap");boot=boot?dc_goal_symbol_value(rt,boot):0;
  if(q){memcpy(&pc,q,4);memcpy(&sp,q+4,4);}
  /* A go is pending: go stores next-state (+72) and enter-state makes it the
   * state (+52) only when it runs on the main thread. The GOAL thread header
   * stays armed after every native import, so pc alone shows no new arming. */
  {const unsigned char *st=dc_goal_resolve(rt,process+52,4),*nx=dc_goal_resolve(rt,process+72,4);uint32_t state=0,next=0;
   if(st)memcpy(&state,st,4);
   if(nx)memcpy(&next,nx,4);
   if(!st || !nx || next==state || next==rt->s7 || !next){rt->error=prior;return;}}
  /* and still pending: original enter-state sets process-mask going (bit 10,
   * mask at +4) on entry and clears it only when it enters the new state on the
   * main thread. next-state != state alone also holds for initialize-go
   * leftovers and change-state-no-go field writes (camera slaves), and the
   * GOAL header stays armed after every import, so pc cannot tell a new arming. */
  {const unsigned char *m=dc_goal_resolve(rt,process+4,4);uint32_t mask=0;
   if(m)memcpy(&mask,m,4);
   if(!m || !(mask&0x400u)){rt->error=prior;return;}}
  rt->error=prior;
  if(!top || !boot || fiber->main_import_self!=(uintptr_t)fiber || fiber->phase!=DC_FIBER_SUSPENDED ||
     pc!=boot || sp!=base+bytes)return;
  if(!dc_goal_fiber_ownership_check(fiber,rt)){rt->error=prior;return;}  /* before the old fiber is discarded */
  set_process_status(rt,process,"waiting-to-run");
  memcpy(top,&main,4);
  if(!dc_goal_main_thread_check(rt,process,reservation,main,base,bytes,1,boot)){
    if(trans)memcpy(top,&trans,4);
    rt->error=prior;return;}
  rt->error=prior;
  dc_goal_unregister_thread_fiber(rt,fiber);memset(fiber,0,sizeof *fiber);
  if(!dc_goal_fiber_init_main(fiber,rt,process,reservation,main,base,bytes) || !dc_goal_register_thread_fiber(rt,fiber)){
    memset(fiber,0,sizeof *fiber);if(trans)memcpy(top,&trans,4);
    if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_UNSUPPORTED_TRANSFER;
    return;
  }
  if(trans)memcpy(top,&trans,4);
  ++dc_goal_main_rearms;
}
static uint64_t reset_and_call(uint64_t thread,uint64_t func) {
  int trans_went=0;
  dc_goal_runtime *rt=active_runtime;
  dc_goal_fiber *fiber=NULL;
  uint32_t symbol,context,current,process=0,top,base,bytes;unsigned char *fields,*p;uint64_t out=0;unsigned i;
  if(!rt)return 0;
  if(thread>UINT32_MAX || func>UINT32_MAX || dc_goal_fiber_current() || execution_context)goto unsupported;
  if(!(p=dc_goal_resolve(rt,(uint32_t)thread+4,4)))goto unsupported;
  memcpy(&process,p,4);
  for(i=0;i<DC_THREAD_FIBERS;++i)if(thread_fibers[i] && thread_fibers[i]->process==process)fiber=thread_fibers[i];
  if(!fiber || fiber->runtime!=rt || (fiber->phase!=DC_FIBER_SUSPENDED && fiber->phase!=DC_FIBER_NEW))goto unsupported;
  if(!dc_goal_fiber_ownership_check(fiber,rt))return 0;
  if(!(p=dc_goal_resolve(rt,(uint32_t)thread+28,4)))goto unsupported;
  memcpy(&top,p,4);
  /* Trans threads use the main stack; post threads the registered DRAM stack. */
  if(top==fiber->stack_base+fiber->stack_bytes)base=fiber->stack_base;
  else if(kernel_stack_bytes && top==kernel_stack_base+kernel_stack_bytes)base=kernel_stack_base;
  /* Original *kernel-dram-stack* is (&+ *dram-stack* DPROCESS_STACK_SIZE), from
   * the array's tagged pointer, so it can sit up to 15 bytes above the aligned
   * end of the array's data. The call runs at that aligned end, inside the span. */
  else if(kernel_stack_bytes && top>kernel_stack_base+kernel_stack_bytes &&
          top-(kernel_stack_base+kernel_stack_bytes)<16u){base=kernel_stack_base;top=base+kernel_stack_bytes;}
  else goto unsupported;
  /* Keep the temporary thread's structure (object at thread, 128 bytes from its
   * tag at thread-4) below the call stack when it lives in that stack. */
  if((uint32_t)thread>=base && (uint32_t)thread<top)base=((uint32_t)thread+124u+15u)&~15u;
  if(base>=top || top-base<4096u)goto unsupported;
  bytes=top-base;
  symbol=dc_goal_find(rt,"*kernel-context*");context=symbol?dc_goal_symbol_value(rt,symbol):0;
  if(!context || !(fields=context_fields(rt,context)))goto unsupported;
  memcpy(&current,fields+20,4);
  if(current!=process)goto unsupported;
  memcpy(fields+20,&rt->s7,4);
  if(!dc_goal_bind_process_context(rt,process,fiber->reservation)){memcpy(fields+20,&current,4);return 0;}
  {uint32_t value=(uint32_t)thread;unsigned char *q=dc_goal_resolve(rt,process+44,4);if(q)memcpy(q,&value,4);}
  set_process_status(rt,process,"running");
#ifdef DC_ARENA_JAK_PLAYER
  jak_beat(DC_JAK_BEAT_RESET_BEFORE,process,(uint32_t)thread,(uint32_t)fiber->phase,(uint32_t)func);
#endif
  /* A trans hook (on the main stack) runs as a temporary fiber. An original go
   * from it re-arms the main thread with set-to-run: enter-state's branch for a
   * process other than the current one then returns into the hook, and its
   * branch for a thread other than the main one jumps to return-from-thread,
   * which finishes this fiber. Either way rearm_main then imports the armed main
   * thread. Post hooks (kernel stack) return normally from their go and stay a
   * call. */
  if(top==fiber->stack_base+fiber->stack_bytes) {
    enum dc_goal_error prior=rt->error;
    uint32_t entry=trans_trampoline(rt),main_thread=fiber->thread;unsigned char *q=dc_goal_resolve(rt,process+44,4);
    trans_process=process;trans_thread=(uint32_t)thread;trans_func=(uint32_t)func;
    if(q)memcpy(q,&main_thread,4);  /* the view's thread while the fiber starts */
    if(entry && q && dc_goal_fiber_init(&hook_fiber,rt,process,fiber->reservation,main_thread,fiber->stack_base,fiber->stack_bytes,entry)) {
      int event=dc_goal_fiber_step(&hook_fiber,0,&out);
      if(event==DC_FIBER_YIELDED && rt->error==DC_GOAL_OK)rt->error=DC_GOAL_UNSUPPORTED_TRANSFER; /* trans may not suspend */
      if(event==DC_FIBER_COMPLETED && hook_fiber.phase==DC_FIBER_DONE)++dc_goal_trans_fibers;
      trans_went=event==DC_FIBER_COMPLETED && rt->error==DC_GOAL_OK;
      {enum dc_goal_error e=rt->error;dc_goal_fiber_clear(&hook_fiber);rt->error=e;}
    } else {
      dc_goal_fiber_clear(&hook_fiber);rt->error=prior;
      if(q)memcpy(q,&trans_thread,4);
      if(!dc_goal_call_on_stack(rt,(uint32_t)func,base,bytes,&out))out=0;
    }
  } else if(!dc_goal_call_on_stack(rt,(uint32_t)func,base,bytes,&out))out=0;
#ifdef DC_ARENA_JAK_PLAYER
  jak_beat(DC_JAK_BEAT_RESET_AFTER,process,(uint32_t)thread,(uint32_t)fiber->phase,(uint32_t)func);
#endif
  {enum dc_goal_error error=rt->error;dc_goal_clear_process_context(rt);rt->error=error;}
  memcpy(fields+20,&current,4);
  if(trans_went)rearm_main(rt,fiber,(uint32_t)thread);
  return out;
unsupported:
  if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_UNSUPPORTED_TRANSFER;
  return 0;
}
/* Original return-from-thread-dead: (deactivate pp), then return to the kernel.
 * Deactivating the running process normally abandons inside deactivate itself. */
static uint64_t return_from_thread_dead(void) {
  dc_goal_runtime *rt=active_runtime;
  uint64_t process,ignored=0;uint32_t type,method;
  if(!rt)return 0;
  process=current_process();
  type=process?dc_goal_type_of(rt,(uint32_t)process):0;
  method=type?dc_goal_get_method(rt,type,10):0;
  if(!process || !method || !dc_goal_scalar_call(method,1,&process,&ignored)){
    if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_UNSUPPORTED_TRANSFER;
    return 0;
  }
  return return_from_thread();
}
/* Original (method thread-suspend cpu-thread), the default suspend-hook. */
static uint64_t thread_suspend(uint64_t thread) {
  (void)thread; /* the running fiber's own thread; checked by the engine */
  return suspend_current();
}
int dc_goal_install_transfer_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  enum{N=7};
  unsigned char code[N][12];
  const unsigned char stub[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  uint32_t targets[N]={(uint32_t)(uintptr_t)return_from_thread,(uint32_t)(uintptr_t)suspend_current,
                       (uint32_t)(uintptr_t)enter_state_code,(uint32_t)(uintptr_t)thread_resume,
                       (uint32_t)(uintptr_t)reset_and_call,(uint32_t)(uintptr_t)return_from_thread_dead,
                       (uint32_t)(uintptr_t)thread_suspend};
  dc_goal_code_export functions[N]={{"dc-c-return-from-thread",code[0],12,NULL,0},
                                    {"dc-c-suspend",code[1],12,NULL,0},
                                    {"dc-c-enter-state-code",code[2],12,NULL,0},
                                    {"dc-c-thread-resume",code[3],12,NULL,0},
                                    {"dc-c-reset-and-call",code[4],12,NULL,0},
                                    {"dc-c-return-from-thread-dead",code[5],12,NULL,0},
                                    {"dc-c-thread-suspend",code[6],12,NULL,0}};
  unsigned i;
  if(!rt || !result) return 0;
  if(active_runtime!=rt) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  for(i=0;i<N;++i){memcpy(code[i],stub,12);memcpy(code[i]+8,&targets[i],4);}
  return dc_goal_load_module(rt,functions,N,sync,NULL,result);
}

/* Desktop C++-kernel services the original kernel startup calls (AI-assisted).
 * _format is a bounded subset; unknown directives refuse with BAD_ARGUMENT.
 * Variadic arguments arrive in integer slots, floats as their bit pattern. */
static char format_text[DC_GOAL_FORMAT_CAPTURE];
static uint32_t format_length;
static int format_put(char *out,uint32_t *n,uint32_t cap,const char *s,uint32_t k) {
  if(k>cap-*n)return 0;
  memcpy(out+*n,s,k);*n+=k;return 1;
}
static int format_object(dc_goal_runtime *rt,char *out,uint32_t *n,uint32_t cap,uint64_t value,int quoted) {
  char tmp[48];uint32_t v=(uint32_t)value,type;const char *name;enum dc_goal_error prior=rt->error;
  if(value>UINT32_MAX){snprintf(tmp,sizeof tmp,"%lld",(long long)value);return format_put(out,n,cap,tmp,strlen(tmp));}
  if(v==rt->s7)return format_put(out,n,cap,"#f",2);
  if(v==rt->s7+8u)return format_put(out,n,cap,"#t",2);
  /* Symbols live in the symbol table, not as tagged basics. */
  if((name=dc_goal_symbol_name(rt,v)))return format_put(out,n,cap,name,strlen(name));
  rt->error=prior;
  if(dc_goal_resolve(rt,v&~3u,4) && (v&15u)==4 && dc_goal_resolve(rt,v-4,4)) {
    type=dc_goal_type_of(rt,v);
    if(type==rt->string_type){
      uint32_t length;const char *text;memcpy(&length,dc_goal_resolve(rt,v,4),4);
      text=dc_goal_resolve(rt,v+4,length+1);
      /* The length word is the allocated size; desktop prints up to the NUL. */
      if(text){uint32_t used=0;while(used<length && text[used])++used;
               return (!quoted||format_put(out,n,cap,"\"",1)) && format_put(out,n,cap,text,used) &&
                      (!quoted||format_put(out,n,cap,"\"",1));}
    }
    if(type==rt->symbol_type && (name=dc_goal_symbol_name(rt,v)))return format_put(out,n,cap,name,strlen(name));
    if(dc_goal_type_of(rt,type)==rt->type_type){
      uint32_t type_name=0;unsigned char *p=dc_goal_resolve(rt,type,4);
      if(p){memcpy(&type_name,p,4);}
      name=type_name?dc_goal_symbol_name(rt,type_name):NULL;rt->error=prior;
      snprintf(tmp,sizeof tmp,"#<%.24s @ #x%lx>",name?name:"object",(unsigned long)v);
      return format_put(out,n,cap,tmp,strlen(tmp));
    }
  }
  rt->error=prior;
  snprintf(tmp,sizeof tmp,"%ld",(long)(int32_t)v);return format_put(out,n,cap,tmp,strlen(tmp));
}
static uint64_t format_native(uint64_t dest,uint64_t fmt,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5) {
  dc_goal_runtime *rt=active_runtime;const uint64_t args[6]={a0,a1,a2,a3,a4,a5};
  char out[512];uint32_t n=0,i=0,length,arg=0;const char *text;
  if(!rt)return 0;
  if(fmt>UINT32_MAX || dc_goal_type_of(rt,(uint32_t)fmt)!=rt->string_type)goto bad;
  memcpy(&length,dc_goal_resolve(rt,(uint32_t)fmt,4),4);
  if(!(text=dc_goal_resolve(rt,(uint32_t)fmt+4,length+1)))goto bad;
  while(i<length){
    char c=text[i++],tmp[48];unsigned width=0;
    if(c!='~'){if(!format_put(out,&n,sizeof out,&c,1))goto bad;continue;}
    while(i<length && text[i]>='0' && text[i]<='9')width=width*10+(unsigned)(text[i++]-'0');
    if(i>=length)goto bad;
    c=text[i++];
    if(c=='%'){if(!format_put(out,&n,sizeof out,"\n",1))goto bad;continue;}
    if(c=='~'){if(!format_put(out,&n,sizeof out,"~",1))goto bad;continue;}
    if(c=='T'||c=='t'){if(!format_put(out,&n,sizeof out,"\t",1))goto bad;continue;}
    if(arg>=6)goto bad;
    switch(c){
    case 'A':case 'a':case 'S':case 's':
      /* kprint.cpp: ~A prints the object (a string with quotes); ~S prints a string's characters. */
      if(!format_object(rt,out,&n,sizeof out,args[arg++],c=='A'||c=='a')){goto bad;}
      continue;
    case 'D':case 'd':snprintf(tmp,sizeof tmp,"%*lld",(int)width,(long long)args[arg++]);break;
    case 'X':case 'x':snprintf(tmp,sizeof tmp,width?"%0*llx":"%*llx",(int)width,(unsigned long long)args[arg++]);break;
    case 'F':case 'f':{uint32_t bits=(uint32_t)args[arg++];float value;memcpy(&value,&bits,4);
      snprintf(tmp,sizeof tmp,"%.4f",(double)value);break;}
    default:goto bad;
    }
    if(!format_put(out,&n,sizeof out,tmp,strlen(tmp)))goto bad;
  }
  if(dest==rt->s7){if(n>=sizeof out)goto bad;out[n]=0;return dc_goal_string(rt,out);}
  /* Desktop kprint: a string destination is appended to (kstrncat, bounded by
   * the string's allocated length); a file-stream is written to its file, and a
   * stream with no open file (no storage here) takes nothing. #t and 0 print. */
  if(dest!=rt->s7+DC_FIX_SYM_TRUE && dest!=0) {
    uint32_t type=basic_type(rt,dest),limit,have=0;char *text_out;const unsigned char *q;
    if(type==rt->string_type) {
      if(!(q=object_bytes(rt,(uint32_t)dest,4)))goto bad;
      memcpy(&limit,q,4);
      if(limit>UINT32_MAX-5u || !(text_out=(char *)object_bytes(rt,(uint32_t)dest+4,limit+1)))goto bad;
      while(have<limit && text_out[have])++have;
      for(i=0;i<n && have<limit;++i)text_out[have++]=out[i];
      text_out[have]=0;
      return 0;
    }
    if(type && type==dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_FILE_STREAM_TYPE)) {
      int32_t file;
      if(!(q=object_bytes(rt,(uint32_t)dest+12,4)))goto bad;
      memcpy(&file,q,4);
      if(file<0)return 0;  /* nothing open: desktop sceWrite fails the same way */
      goto bad;            /* no native files are ever opened */
    }
    goto bad;              /* desktop asserts on an unknown destination */
  }
  if(n>sizeof format_text-format_length)format_length=0; /* bounded ring: newest output wins */
  if(n<=sizeof format_text){memcpy(format_text+format_length,out,n);format_length+=n;}
#ifdef __DREAMCAST__
  dbgio_write_buffer_xlat((const uint8_t *)out,(int)n);
#endif
  return rt->s7;
bad:
  if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);
  return 0;
}
/* Desktop without a debug segment (MasterDebug off, as on Dreamcast) leaves the
 * 'debug symbol's kheap null, and kmalloc puts a null-heap request on the
 * global heap. So 'debug allocates globally exactly while it has no heap. */
static int debug_heap_absent(dc_goal_runtime *rt,uint64_t heap){
  return heap==rt->s7+DC_FIX_SYM_DEBUG_HEAP && !dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_DEBUG_HEAP);
}
static uint64_t malloc_native(uint64_t heap,uint64_t bytes) {
  dc_goal_runtime *rt=active_runtime;
  if(!rt)return 0;
  if(debug_heap_absent(rt,heap))heap=rt->s7+DC_FIX_SYM_GLOBAL_HEAP;
  if(heap!=rt->s7+DC_FIX_SYM_GLOBAL_HEAP || !bytes || bytes>UINT32_MAX){
    if(rt->error==DC_GOAL_OK){(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);}
    return 0;
  }
  return dc_goal_alloc(rt,(uint32_t)bytes);
}
/* Native counterpart of kmalloc (game/kernel/common/kmalloc.cpp) for the global
 * heap only: the arena. Memory is always zeroed (covering KMALLOC_MEMSET); the
 * align-64/align-256 flags round the address up within a padded allocation.
 * Any other kheap refuses explicitly until native kheaps exist. */
static uint64_t kmalloc_native(uint64_t heap,uint64_t bytes,uint64_t flags,uint64_t name){
  dc_goal_runtime *rt=active_runtime;
  uint32_t global=rt?rt->s7+DC_FIX_SYM_GLOBAL_HEAP:0,align,address;
  (void)name;
  if(!rt)return 0;
  if((heap!=global && heap!=dc_goal_symbol_value(rt,global)) || !bytes || bytes>UINT32_MAX-256u){
    if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);
    return 0;
  }
  align=(flags&0x100u)?256u:(flags&0x40u)?64u:16u;
  address=dc_goal_alloc(rt,(uint32_t)bytes+align-16u);
  if(!address)return 0;
  return (address+align-1u)&~(align-1u);
}
/* An object a bridge may write: in the arena, or a GOAL stack object in a live
 * caller frame (above this bridge's own frame, within 64 KiB of it), as
 * (new 'stack-no-clear ...) objects passed down the call chain are. */
static void *bridge_object(dc_goal_runtime *rt,uint64_t address,uint32_t bytes){
  volatile uint32_t marker=0;uintptr_t sp=(uintptr_t)&marker;void *p;enum dc_goal_error prior=rt->error;
  if(address>UINT32_MAX || !bytes)return NULL;
  if((p=dc_goal_resolve(rt,(uint32_t)address,bytes)))return p;
  rt->error=prior;
  if(address>sp && address+bytes<=sp+0x10000u)return (void*)(uintptr_t)address;
  return NULL;
}
/* Native counterpart of scf-get-time (DecodeTime -> sceCdReadClock): the local
 * time as BCD, exactly as desktop libscf.cpp fills the 8-byte sceCdCLOCK. */
static uint8_t bcd(int v){return (uint8_t)((v%10)+(v/10)*16);}
static uint64_t scf_get_time_native(uint64_t clock){
  dc_goal_runtime *rt=active_runtime;uint8_t *out;time_t t;struct tm *date;
  if(!rt || !(out=bridge_object(rt,clock,8))){if(rt)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  t=time(0);date=localtime(&t);
  if(!date){memset(out,0,8);return 0;}
  out[0]=0;out[1]=bcd(date->tm_sec);out[2]=bcd(date->tm_min);out[3]=bcd(date->tm_hour);
  out[4]=bcd((date->tm_yday-date->tm_wday+7)/7);out[5]=bcd(date->tm_mday);
  out[6]=bcd(date->tm_mon+1);out[7]=bcd(date->tm_year-100);
  return 0;
}
/* __pc-get-mips2c: the PC port's C++ translations of original MIPS assembly
 * (game/mips2c). Those built for SH-4 (build_mips2c.py, mips2c_dc.cpp) answer
 * dc_mips2c_find with a C entry that has the GOAL function's native signature;
 * the lookup returns a GOAL function object that jumps to it, made once per
 * name ("dc-mips2c:NAME"). A name not built yet still gets the stub that faults
 * explicitly (UNSUPPORTED_TRANSFER) if it is ever called. Nothing pretends. */
static uint32_t mips2c_requests,mips2c_native;
static dc_goal_cache_sync mips2c_sync;  /* the boot's, from install_kernel_services */
uint32_t __attribute__((weak)) dc_mips2c_find(const char *name){(void)name;return 0;}
uint32_t dc_goal_mips2c_current_process(void){return (uint32_t)current_process();}
static uint64_t mips2c_missing_native(void){
  if(active_runtime)active_runtime->error=DC_GOAL_UNSUPPORTED_TRANSFER;
  return 0;
}
static uint64_t pc_get_mips2c_native(uint64_t name){
  dc_goal_runtime *rt=active_runtime;uint32_t stub,length,target,symbol;const char *text;char label[96];
  enum dc_goal_error prior;
  if(!rt)return 0;
  ++mips2c_requests;
  /* A GOAL string: allocated length, then the NUL-terminated text. */
  if(name<=UINT32_MAX && (text=dc_goal_resolve(rt,(uint32_t)name,4))){
    memcpy(&length,text,4);
    if(length<64 && (text=dc_goal_resolve(rt,(uint32_t)name+4u,length+1u)) && memchr(text,0,length+1u) &&
       (target=dc_mips2c_find(text))!=0){
      snprintf(label,sizeof label,"dc-mips2c:%s",text);
      prior=rt->error;symbol=dc_goal_find(rt,label);rt->error=prior;
      if(symbol && (stub=dc_goal_symbol_value(rt,symbol)) && dc_goal_type_of(rt,stub)==rt->function_type)return stub;
      {unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};dc_goal_code_export function={label,code,12,NULL,0};
       dc_goal_module_result loaded;
       memcpy(code+8,&target,4);
       if(dc_goal_load_module(rt,&function,1,mips2c_sync,NULL,&loaded) &&
          (stub=dc_goal_symbol_value(rt,dc_goal_find(rt,label)))){++mips2c_native;return stub;}
       return 0;}
    }
  }
  rt->error=DC_GOAL_OK;
  stub=dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-mips2c-missing"));
  if(!stub){rt->error=DC_GOAL_BAD_FUNCTION;return 0;}
  return stub;
}
uint32_t dc_goal_mips2c_native(void){return mips2c_native;}
uint32_t dc_goal_mips2c_requests(void){return mips2c_requests;}
/* Original kscheme nothing: a function that does nothing. */
static uint64_t nothing_native(void){return 0;}
/* Desktop kmachine Decode* system-config services with kboot's masterConfig
 * defaults: language English (Language::English 0), aspect 4:3 (SCE_ASPECT_43),
 * volume 100, territory SCEA (0), timeouts 0 (demo-only). */
static uint64_t scf_language_native(void){return 0;}
static uint64_t scf_aspect_native(void){return 0;}
static uint64_t scf_volume_native(void){return 100;}
static uint64_t scf_territory_native(void){return 0;}
static uint64_t scf_timeout_native(void){return 0;}
static uint64_t scf_inactive_timeout_native(void){return 0;}
/* Original kernel cpad-open/cpad-get-data on a 132-byte cpad-info; both return it. */
static uint64_t cpad_open_native(uint64_t info,uint64_t number){
  dc_goal_runtime *rt=active_runtime;uint8_t *cpad;
  if(!rt || info>UINT32_MAX || !(cpad=dc_goal_resolve(rt,(uint32_t)info,132))){if(rt)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  dc_goal_cpad_open(cpad,(int32_t)number);
  return info;
}
static uint64_t cpad_get_data_native(uint64_t info){
  dc_goal_runtime *rt=active_runtime;uint8_t *cpad;
#ifdef DC_ARENA_JAK_PLAYER
  {dc_goal_fiber *beat_fiber=dc_goal_fiber_current();
   jak_beat(DC_JAK_BEAT_PAD,beat_fiber?beat_fiber->process:0,info<=UINT32_MAX?(uint32_t)info:0,
            beat_fiber?(uint32_t)beat_fiber->phase:0,0);}
#endif
  if(!rt || info>UINT32_MAX || !(cpad=dc_goal_resolve(rt,(uint32_t)info,132))){if(rt)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  dc_goal_cpad_get_data(cpad);
#ifdef DC_ARENA_LIVE
  dc_arena_live_pad(rt,(uint32_t)info);
#elif defined(DC_ARENA_CAMERA)
  dc_arena_camera_pad(rt,(uint32_t)info);
#endif
  return info;
}
/* Preserve prior diagnostic site numbers in all existing modes (AI-assisted). */
#ifndef DC_ARENA_LIVE
#line 1115
#endif
/* PS2 MMI 128-bit integer ops called by lowered code with frame-slot pointers. */
static uint64_t mmi_native(uint32_t op,uint32_t dst,uint32_t a,uint32_t b){
  if(!dc_goal_mmi(op,(void*)(uintptr_t)dst,(const void*)(uintptr_t)a,b?(const void*)(uintptr_t)b:NULL) &&
     active_runtime)(DC_GOAL_NOTE_SITE(1),active_runtime->error=DC_GOAL_BAD_ARGUMENT);
  return 0;
}
uint32_t dc_goal_format_capture(const char **text){if(text)*text=format_text;return format_length;}
void dc_goal_format_capture_clear(void){format_length=0;}
/* Main-thread backup floor. A suspended native SH-4 main thread needs 348
 * bytes; dreamcast_thread_context.gc makes 512 (PROCESS_STACK_SAVE_SIZE) the
 * default every activate reserves. Original stack-size-set! calls size backups
 * for PS2 frames (time-of-day asks for 128), and native frames are larger: a
 * camera that asks for 512 suspends with 668 bytes in use. So this counterpart
 * passes the unchanged original method max(2 * request, 512). */
#define DC_MAIN_BACKUP_FLOOR 512u
#define DC_MAIN_BACKUP_SCALE 2u
static uint32_t original_stack_size_set;
static uint64_t stack_size_set_floor(uint64_t thread,uint64_t bytes) {
  typedef uint64_t (*setter)(uint64_t,uint64_t);
  if(!original_stack_size_set){if(active_runtime)active_runtime->error=DC_GOAL_BAD_FUNCTION;return 0;}
  if((int64_t)bytes<(int64_t)(DC_MAIN_BACKUP_FLOOR/DC_MAIN_BACKUP_SCALE))bytes=DC_MAIN_BACKUP_FLOOR;
  else if(bytes<=0x10000u)bytes*=DC_MAIN_BACKUP_SCALE;
  {uint64_t value=((setter)(uintptr_t)original_stack_size_set)(thread,bytes);
   dc_goal_fiber *fiber=dc_goal_fiber_current();
   /* A running fiber resizing its own main thread: its image follows. */
   if(fiber && fiber->thread==thread && active_runtime && active_runtime->error==DC_GOAL_OK)
     dc_goal_fiber_backup_resized(fiber);
   return value;}
}
int dc_goal_install_backup_floor(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  uint32_t target=(uint32_t)(uintptr_t)stack_size_set_floor,symbol,shim,thread,cpu,body;
  dc_goal_code_export function={"dc-c-stack-size-set-floor",code,12,NULL,0};
  enum {STACK_SIZE_SET=9};
  if(!rt || !result || active_runtime!=rt){if(rt)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  symbol=dc_goal_find(rt,"(method stack-size-set! thread)");body=symbol?dc_goal_symbol_value(rt,symbol):0;
  thread=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_THREAD_TYPE);
  symbol=dc_goal_find(rt,"cpu-thread");cpu=symbol?dc_goal_symbol_value(rt,symbol):0;
  if(!body || !thread || !cpu || dc_goal_get_method(rt,thread,STACK_SIZE_SET)!=body ||
     dc_goal_get_method(rt,cpu,STACK_SIZE_SET)!=body){(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  memcpy(code+8,&target,4);
  if(!dc_goal_load_module(rt,&function,1,sync,NULL,result))return 0;
  shim=dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-stack-size-set-floor"));
  original_stack_size_set=body;
  return dc_goal_set_method(rt,thread,STACK_SIZE_SET,shim) && dc_goal_set_method(rt,cpu,STACK_SIZE_SET,shim);
}
int dc_goal_install_kernel_services(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  const unsigned char stub[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  unsigned char code[16][12];
  uint32_t targets[16]={(uint32_t)(uintptr_t)format_native,(uint32_t)(uintptr_t)malloc_native,
                       (uint32_t)(uintptr_t)nothing_native,(uint32_t)(uintptr_t)mmi_native,
                       (uint32_t)(uintptr_t)cpad_open_native,(uint32_t)(uintptr_t)cpad_get_data_native,
                       (uint32_t)(uintptr_t)kmalloc_native,(uint32_t)(uintptr_t)scf_get_time_native,
                       (uint32_t)(uintptr_t)pc_get_mips2c_native,(uint32_t)(uintptr_t)mips2c_missing_native,
                       (uint32_t)(uintptr_t)scf_language_native,(uint32_t)(uintptr_t)scf_aspect_native,
                       (uint32_t)(uintptr_t)scf_volume_native,(uint32_t)(uintptr_t)scf_territory_native,
                       (uint32_t)(uintptr_t)scf_timeout_native,(uint32_t)(uintptr_t)scf_inactive_timeout_native};
  dc_goal_code_export functions[16]={{"_format",code[0],12,NULL,0},{"malloc",code[1],12,NULL,0},
                                    {"nothing",code[2],12,NULL,0},{"dc-c-mmi",code[3],12,NULL,0},
                                    {"cpad-open",code[4],12,NULL,0},{"cpad-get-data",code[5],12,NULL,0},
                                    {"kmalloc",code[6],12,NULL,0},{"scf-get-time",code[7],12,NULL,0},
                                    {"__pc-get-mips2c",code[8],12,NULL,0},{"dc-c-mips2c-missing",code[9],12,NULL,0},
                                    {"scf-get-language",code[10],12,NULL,0},{"scf-get-aspect",code[11],12,NULL,0},
                                    {"scf-get-volume",code[12],12,NULL,0},{"scf-get-territory",code[13],12,NULL,0},
                                    {"scf-get-timeout",code[14],12,NULL,0},
                                    {"scf-get-inactive-timeout",code[15],12,NULL,0}};
  unsigned i;
  if(!rt || !result) return 0;
  if(active_runtime!=rt) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  for(i=0;i<16;++i){memcpy(code[i],stub,12);memcpy(code[i]+8,&targets[i],4);}
  mips2c_sync=sync;
  return dc_goal_load_module(rt,functions,16,sync,NULL,result);
}

/* PC-port kernel services (desktop kmachine pc_*), with the answers desktop
 * gives when it has no main display: the Dreamcast has no PC window, display
 * manager, keyboard, mouse or writable storage. Getters return those defaults,
 * setters change nothing, and file opens fail (AI-assisted). */
static uint64_t pc_symbol(const char *name){
  dc_goal_runtime *rt=active_runtime;enum dc_goal_error prior;uint32_t symbol;
  if(!rt)return 0;
  prior=rt->error;symbol=dc_goal_intern(rt,name);rt->error=prior;
  return symbol;
}
static uint64_t pc_false(void){return active_runtime?active_runtime->s7:0;}
static uint64_t pc_get_os_native(void){return pc_false();}          /* not windows/linux/darwin */
static uint64_t pc_display_mode_native(void){return pc_symbol("windowed");}
static uint64_t pc_zero_native(void){return 0;}                     /* refresh rate; void setters */
/* kmemcard.cpp MC_set_language: the C kernel only records the language. */
static uint32_t kernel_language;
static uint64_t kset_language_native(uint64_t language){kernel_language=(uint32_t)language;return 0;}
/* kmachine.cpp pc-rand: a 32-bit pseudo-random number (desktop uses mt19937;
 * GOAL only mixes it into rand-vu, so a xorshift generator stands in). */
/* kmachine.cpp __read-ee-timer: the 300 MHz EE cycle count since start,
 * ns * 3 / 10, from KOS's nanosecond clock. */
static uint64_t read_ee_timer_native(void){
#ifdef __DREAMCAST__
  return timer_ns_gettime64()*3u/10u;
#else
  return 0;
#endif
}
/* Kernel string->symbol (kscheme.cpp intern): the symbol a GOAL string names,
 * interned if new. Original initialize-skeleton-by-name finds *<name>-sg* so. */
static uint64_t string_to_symbol_native(uint64_t string){
  dc_goal_runtime *rt=active_runtime;char name[256];uint32_t length=0,used=0,s=(uint32_t)string;
  const unsigned char *p,*text;
  if(!rt)return 0;
  if(string>UINT32_MAX || (s&15u)!=4u || !dc_goal_resolve(rt,s-4u,4) || dc_goal_type_of(rt,s)!=rt->string_type ||
     !(p=dc_goal_resolve(rt,s,4))){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  memcpy(&length,p,4);
  if(!(text=dc_goal_resolve(rt,s+4u,length?length:1))){if(rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  while(used<length && used<sizeof name-1u && text[used]){name[used]=(char)text[used];++used;}
  name[used]=0;
  return dc_goal_intern(rt,name);
}
static uint32_t pc_rand_state=2463534242u;
static uint64_t pc_rand_native(void){
  uint32_t x=pc_rand_state;x^=x<<13;x^=x>>17;x^=x<<5;pc_rand_state=x;return x;
}
static uint64_t pc_filepath_exists_native(uint64_t path){(void)path;return pc_false();}
static uint64_t pc_resolution_native(uint64_t w,uint64_t h){(void)w;(void)h;return pc_false();}
static float pc_axis_scale_native(void){return 1.33f;}
/* kopen: mode, name, flags 0 and the open's result; nothing can be opened. */
static uint64_t pc_kopen_native(uint64_t fs,uint64_t name,uint64_t mode){
  dc_goal_runtime *rt=active_runtime;unsigned char *fields;int32_t none=-1;uint32_t zero=0,m=(uint32_t)mode,n=(uint32_t)name;
  if(!rt || basic_type(rt,fs)!=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_FILE_STREAM_TYPE) ||
     !(fields=object_bytes(rt,(uint32_t)fs,16))) {
    if(rt && rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);
    return 0;
  }
  memcpy(fields,&zero,4);memcpy(fields+4,&m,4);memcpy(fields+8,&n,4);memcpy(fields+12,&none,4);
  return fs;
}
static uint64_t pc_kclose_native(uint64_t fs){return fs;}
dc_goal_rpc_hook dc_goal_rpc_handler;
dc_goal_link_hook dc_goal_link_begin_handler;
dc_goal_rpc_hook dc_goal_str_handler;
dc_goal_link_hook dc_goal_link_handler;
/* The IOP's STR server (RPC-LOAD-STR, port 4) streams spooled animation chunks
 * from .STR files. An ELF that serves them sets dc_goal_str_handler (the level
 * ELF, level_str.c). Without one every load-chunk-msg is answered with
 * load-msg-result error (uint16 at +2), so original str-load-status reports
 * 'error and external-art-buffer drops the request instead of linking a buffer
 * nothing was read into. --dump dc_goal_str_rpc_refused 4 */
volatile uint32_t dc_goal_str_rpc_refused;
static uint64_t rpc_call_native(uint64_t port,uint64_t fno,uint64_t async,uint64_t send,
                                uint64_t send_bytes,uint64_t recv,uint64_t recv_bytes) {
  if(port==4 && active_runtime && dc_goal_str_handler)
    return dc_goal_str_handler(port,fno,async,send,send_bytes,recv,recv_bytes);
  if(port==4 && active_runtime){
    unsigned char *reply=recv<=UINT32_MAX && recv_bytes>=4?dc_goal_resolve(active_runtime,(uint32_t)recv,4):NULL;
    if(reply){reply[2]=1;reply[3]=0;++dc_goal_str_rpc_refused;}
    return 0;
  }
  return dc_goal_rpc_handler?dc_goal_rpc_handler(port,fno,async,send,send_bytes,recv,recv_bytes):0;
}
static uint64_t link_begin_native(uint64_t data,uint64_t name,uint64_t length,uint64_t heap,uint64_t flags) {
  if(dc_goal_link_begin_handler)return dc_goal_link_begin_handler(data,name,length,heap,flags);
  if(active_runtime && active_runtime->error==DC_GOAL_OK)active_runtime->error=DC_GOAL_UNSUPPORTED_TRANSFER;
  return 0;
}
/* The C kernel's link (klink.cpp link_and_exec through its GOAL wrapper): data,
 * name, size, heap, flags; external-art-buffer links each streamed chunk with
 * it. Forwarded to dc_goal_link_handler; without one it refuses. */
static uint64_t link_native(uint64_t data,uint64_t name,uint64_t length,uint64_t heap,uint64_t flags) {
  if(dc_goal_link_handler)return dc_goal_link_handler(data,name,length,heap,flags);
  if(active_runtime && active_runtime->error==DC_GOAL_OK)active_runtime->error=DC_GOAL_UNSUPPORTED_TRANSFER;
  return 0;
}
/* The hooked linker links each object completely in link-begin. */
static uint64_t link_resume_native(void) {
  if(dc_goal_link_begin_handler)return 1;
  if(active_runtime && active_runtime->error==DC_GOAL_OK)active_runtime->error=DC_GOAL_UNSUPPORTED_TRANSFER;
  return 0;
}
/* Desktop pc_memmove (kmachine.cpp): PC ultimate-memcpy is (__mem-move dst src
 * size). Original process relocate moves an image of 2560 bytes or more with it,
 * so without it dead-pool-heap compaction updates the record but copies nothing.
 * Both guest ranges must resolve; a refused range faults and copies nothing. */
/* --dump dc_goal_mem_move_probe 24: calls, refused, last dst, src, size, error. */
volatile uint32_t dc_goal_mem_move_probe[6];
static uint64_t pc_mem_move_native(uint64_t dst,uint64_t src,uint64_t size) {
  dc_goal_runtime *rt=active_runtime;unsigned char *to,*from;
  dc_goal_mem_move_probe[0]++;dc_goal_mem_move_probe[2]=(uint32_t)dst;dc_goal_mem_move_probe[3]=(uint32_t)src;dc_goal_mem_move_probe[4]=(uint32_t)size;
  if(!rt || !size)return 0;
  if(dst>UINT32_MAX || src>UINT32_MAX || size>UINT32_MAX){(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  to=dc_goal_resolve(rt,(uint32_t)dst,(uint32_t)size);from=to?dc_goal_resolve(rt,(uint32_t)src,(uint32_t)size):NULL;
  if(to && from)memmove(to,from,(size_t)size);
  else {dc_goal_mem_move_probe[1]++;dc_goal_mem_move_probe[5]=(uint32_t)rt->error;}
  return 0;
}
#define DC_PC_SERVICES 57
/* Coexistence only stashes requests on the GOAL stack (AI-assisted), ISC. */
#ifdef DC_ARENA_COEXIST
uint64_t dc_arena_syncv(void);
uint64_t dc_arena_chain(uint64_t,uint64_t);
#endif
#ifdef DC_ARENA_GAME
void dc_arena_game_bind(dc_goal_runtime *);
#endif
int dc_goal_install_pc_services(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
#ifdef DC_ARENA_GAME
  dc_arena_game_bind(rt);
#endif
  static const char *const void_setters[]={
    "pc-set-mouse-options!","pc-set-mouse-camera-sens!","pc-set-auto-hide-cursor!","pc-set-window-size!",
    "pc-set-frame-rate","pc-ignore-background-controller-events!","pc-get-active-display-size",
    "pc-get-window-size","pc-get-window-scale","pc-set-vsync","pc-set-msaa","pc-set-letterbox",
    "pc-set-game-resolution","pc-set-gfx-hack","pc-set-collision","pc-set-collision-mode",
    "pc-set-collision-wireframe","pc-set-controller-led!","pc-renderer-tree-set-lod","pc-discord-rpc-set",
    "pc-discord-rpc-update","pc-sound-set-fade-hack","pc-sound-set-flava-hack",
    /* No IOP: sound and loader RPCs are dropped (rpc-busy? 0, rpc-call returns at
     * once with nothing received) until a Dreamcast AICA sound driver serves them. */
    "rpc-call","rpc-busy?",
    /* PS2 graphics/DMA library calls (kmachine.cpp: sceGs*, sceDmaSync,
     * FlushCache) answer as desktop does with no renderer attached: syncv and
     * sync-path 0, dma-sync 0 (finished), the rest do nothing. A PowerVR renderer
     * will give syncv a real vertical blank. mc-run with no card operation
     * pending does nothing (MC_run's callback is null). */
    "syncv","sync-path","reset-graph","reset-path","put-display-env","flush-cache","dma-sync","mc-run",
    /* The PC renderer's level texture list, and the frame's DMA chain handed to
     * the renderer (display-sync). No renderer to tell yet: the PowerVR
     * renderer will consume the chain here. */
    "__pc-set-levels","__send-gfx-dma-chain"};
  const unsigned char stub[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  unsigned char code[DC_PC_SERVICES][12];uint32_t targets[DC_PC_SERVICES];
  dc_goal_code_export functions[DC_PC_SERVICES];unsigned i,k=0;
  if(!rt || !result) return 0;
  if(active_runtime!=rt) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  {static const char *const names[]={"pc-get-os","pc-get-display-mode","pc-get-active-display-refresh-rate",
     "pc-filepath-exists?","pc-mkdir-file-path","pc-is-supported-resolution?","pc-get-keyboard-enabled?",
     "pc-get-pressure-sensitivity-enabled?","pc-get-axis-scale","file-stream-open"};
   const uint32_t fns[]={(uint32_t)(uintptr_t)pc_get_os_native,(uint32_t)(uintptr_t)pc_display_mode_native,
     (uint32_t)(uintptr_t)pc_zero_native,(uint32_t)(uintptr_t)pc_filepath_exists_native,
     (uint32_t)(uintptr_t)pc_filepath_exists_native,(uint32_t)(uintptr_t)pc_resolution_native,
     (uint32_t)(uintptr_t)pc_false,(uint32_t)(uintptr_t)pc_false,
     (uint32_t)(uintptr_t)pc_axis_scale_native,(uint32_t)(uintptr_t)pc_kopen_native};
   for(i=0;i<sizeof names/sizeof names[0];++i){functions[k]=(dc_goal_code_export){names[i],code[k],12,NULL,0};targets[k++]=fns[i];}}
  functions[k]=(dc_goal_code_export){"file-stream-close",code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)pc_kclose_native;
  functions[k]=(dc_goal_code_export){"kset-language",code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)kset_language_native;
  functions[k]=(dc_goal_code_export){"pc-rand",code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)pc_rand_native;
  functions[k]=(dc_goal_code_export){"string->symbol",code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)string_to_symbol_native;
  functions[k]=(dc_goal_code_export){"__read-ee-timer",code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)read_ee_timer_native;
  functions[k]=(dc_goal_code_export){"__mem-move",code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)pc_mem_move_native;
  functions[k]=(dc_goal_code_export){"link-begin",code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)link_begin_native;
  functions[k]=(dc_goal_code_export){"link-resume",code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)link_resume_native;
  functions[k]=(dc_goal_code_export){"link",code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)link_native;
  /* Kernel loado (kscheme.cpp) loads an object file by name from the host. A
   * retail disc kernel has nothing to load it from: #f, which original
   * texture-page-login treats as "not loaded" (0 would be relocated). */
  functions[k]=(dc_goal_code_export){"loado",code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)pc_false;
  for(i=0;i<sizeof void_setters/sizeof void_setters[0];++i){
    functions[k]=(dc_goal_code_export){void_setters[i],code[k],12,NULL,0};targets[k++]=(uint32_t)(uintptr_t)pc_zero_native;
    if(!strcmp(void_setters[i],"rpc-call"))targets[k-1]=(uint32_t)(uintptr_t)rpc_call_native;
#ifdef DC_ARENA_COEXIST
    if(!strcmp(void_setters[i],"syncv"))targets[k-1]=(uint32_t)(uintptr_t)dc_arena_syncv;
    if(!strcmp(void_setters[i],"__send-gfx-dma-chain"))targets[k-1]=(uint32_t)(uintptr_t)dc_arena_chain;
#endif
  }
  if(k>DC_PC_SERVICES){(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
  for(i=0;i<k;++i){memcpy(code[i],stub,12);memcpy(code[i]+8,&targets[i],4);}
  if(!dc_goal_load_module(rt,functions,k,sync,NULL,result))return 0;
  /* Desktop InitMachine_PCPort's strings. Settings would live on the VMU (KOS
   * /vmu/a1); with no storage access yet, pc-filepath-exists? stays #f. */
  {static const char *const globals[3][2]={{"*pc-user-dir-base-path*","/vmu/a1"},
     {"*pc-settings-folder*","/vmu/a1"},{"*pc-settings-built-sha*","dreamcast"}};
   for(i=0;i<3;++i){uint32_t text=dc_goal_string(rt,globals[i][1]);
     if(!text || !dc_goal_set_symbol(rt,dc_goal_intern(rt,globals[i][0]),text))return 0;}}
  return 1;
}

int dc_goal_install_process_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  uint32_t target=(uint32_t)(uintptr_t)current_process;
  dc_goal_code_export function={"dc-c-current-process",code,12,NULL,0};
  if(!rt || !result) return 0;
  if(active_runtime!=rt) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  memcpy(code+8,&target,4);
  return dc_goal_load_module(rt,&function,1,sync,NULL,result);
}

uint32_t dc_goal_construct_main_thread(dc_goal_runtime *rt,uint32_t process,uint32_t reservation_bytes,
                                       uint32_t stack_base,uint32_t stack_bytes,uint32_t name,uint32_t backup_bytes) {
  typedef uint64_t (*constructor)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
  dc_goal_process_heap heap;
  dc_goal_code_entry_info provenance;
  dc_goal_type_fields *schema;
  unsigned char *fields;
  uint32_t symbol,type,thread_type,top,cur,main_thread,top_thread,body,bytes;
  if(!rt || active_runtime!=rt) { if(rt) (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  if(!dc_goal_process_heap_open(&heap,rt,process,reservation_bytes)) return 0;
  symbol=dc_goal_find(rt,"cpu-thread"); type=symbol?dc_goal_symbol_value(rt,symbol):0;
  thread_type=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_THREAD_TYPE);
  if(!type || dc_goal_type_of(rt,type)!=rt->type_type ||
     !(schema=dc_goal_resolve(rt,type,sizeof(*schema))) || schema->allocated_size!=128 ||
     schema->num_methods!=12 || schema->parent!=thread_type || dc_goal_type_of(rt,name)!=rt->symbol_type) {
    rt->error=DC_GOAL_BAD_TYPE; return 0;
  }
  if(!stack_bytes || ((stack_base|stack_bytes|backup_bytes)&15u) || stack_bytes<16 ||
     stack_bytes>UINT32_MAX-stack_base || backup_bytes>stack_bytes || backup_bytes>UINT32_MAX-128u) {
    (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0;
  }
  if(!dc_goal_resolve(rt,stack_base,stack_bytes)) return 0;
  if(stack_base<process-4+reservation_bytes && stack_base+stack_bytes>process-4) {
    (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0;
  }
  fields=dc_goal_resolve(rt,process,108);
  if(!fields) return 0;
  memcpy(&main_thread,fields+40,4); memcpy(&top_thread,fields+44,4);
  if(main_thread!=rt->s7 || top_thread!=rt->s7) { (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT); return 0; }
  memcpy(&cur,fields+84,4); memcpy(&top,fields+80,4); bytes=128+backup_bytes;
  if(bytes>=top-cur) { rt->error=DC_GOAL_OUT_OF_MEMORY; return 0; }
  symbol=dc_goal_find(rt,"(method new cpu-thread)"); body=symbol?dc_goal_symbol_value(rt,symbol):0;
  if(!body || dc_goal_type_of(rt,body)!=rt->function_type || !dc_goal_resolve(rt,body,2)) {
    rt->error=DC_GOAL_BAD_MODULE; return 0;
  }
  if(!dc_goal_code_entry(rt,body,&provenance) || provenance.symbol!=symbol) {
    rt->error=DC_GOAL_BAD_MODULE;return 0;
  }
  /* Original main-thread branch bumps heap-cur and initializes data only.
   * Reject every unsupported temporary-thread path before entering its body. */
  return (uint32_t)((constructor)(uintptr_t)body)(rt->s7+DC_FIX_SYM_PROCESS_TYPE,type,process,name,
                                                 backup_bytes,stack_base+stack_bytes);
}

/* Explicit cooperative-yield bootstrap bridge (AI-assisted). */
static uint64_t fiber_yield(uint64_t value) {
 dc_goal_fiber *fiber=dc_goal_fiber_current();
 return dc_goal_fiber_yield(active_runtime?active_runtime:(fiber?fiber->runtime:NULL),value);
}
int dc_goal_install_fiber_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
 unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 dc_goal_code_export function={"dc-c-fiber-yield",code,12,NULL,0};
 uint32_t target=(uint32_t)(uintptr_t)fiber_yield;
 if(!rt || active_runtime!=rt) {if(rt)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
 memcpy(code+8,&target,4);return dc_goal_load_module(rt,&function,1,sync,NULL,result);
}

/* First activation is deliberately narrower than original process.activate.
 * The original body remains unchanged; inspect every supported dependency
 * before its status/PID/heap/tree writes. Caller owns all pinned spans. */
static uint32_t activation_word(const unsigned char *p,unsigned off) {
 uint32_t v;memcpy(&v,p+off,4);return v;
}
static int activation_overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
 return an && bn && (a<=b?b-a<an:a-b<bn);
}
static int activation_write_alias(uint32_t process,uint32_t reservation,uint32_t dest,uint32_t ctx,
                                   uint32_t address,uint32_t bytes) {
 return activation_overlap(process-4,reservation,address,bytes) ||
  activation_overlap(dest-4,32,address,bytes) || activation_overlap(ctx-4,48,address,bytes);
}
uint32_t dc_goal_activate_process(dc_goal_runtime *rt,uint32_t process,uint32_t reservation,
                                 uint32_t destination,uint32_t stack_base,uint32_t stack_bytes,uint32_t name) {
 typedef uint64_t (*activate)(uint64_t,uint64_t,uint64_t,uint64_t);
 const char *names[]={"(method activate process)","(method new cpu-thread)","mem-set32!","change-parent"};
 dc_goal_process_heap heap;
 dc_goal_code_entry_info code[4];
 dc_goal_type_fields *schema;
 unsigned char *p,*d,*c;
 uint32_t process_type,tree_type,cpu,thread_type,symbol,context,body[4],i,name_type,type_bytes,tree_bytes,cpu_bytes,dead,value;
 if(!rt || active_runtime!=rt) {if(rt)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
 if(dc_goal_fiber_current())goto bad;
 if(!dc_goal_process_heap_open(&heap,rt,process,reservation))return 0;
 process_type=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_PROCESS_TYPE);
 schema=dc_goal_resolve(rt,process_type,sizeof *schema);
 if(!schema || schema->heap_base!=0)goto bad;
 type_bytes=20u+4u*schema->num_methods;
 p=dc_goal_resolve(rt,process,108);if(!p)return 0;
 dead=dc_goal_find(rt,"dead");
 if(!dead || activation_word(p,32)!=dead || activation_word(p,40)!=rt->s7 ||
    activation_word(p,44)!=rt->s7 || activation_word(p,8)!=rt->s7 ||
    activation_word(p,12)!=rt->s7 || activation_word(p,16)!=rt->s7 ||
    activation_word(p,20)!=process+24 || activation_word(p,24)!=process ||
    activation_word(p,84)!=process+108)goto bad;
 /* Original activate constructs 128+256 bytes at the reset heap base. */
 if(activation_word(p,80)-(process+108)<=384u)goto oom;
 symbol=dc_goal_find(rt,"process-tree");tree_type=symbol?dc_goal_symbol_value(rt,symbol):0;
 if(!tree_type || dc_goal_type_of(rt,destination)!=tree_type)goto bad;
 schema=dc_goal_resolve(rt,tree_type,sizeof *schema);
 if(!schema || schema->allocated_size!=32)goto bad;
 tree_bytes=20u+4u*schema->num_methods;
 d=dc_goal_resolve(rt,destination,28);if(!d)return 0;
 if(!(activation_word(d,4)&256u) || activation_word(d,8)!=rt->s7 ||
    activation_word(d,12)!=rt->s7 || activation_word(d,16)!=rt->s7 ||
    activation_word(d,20)!=destination+24 || activation_word(d,24)!=destination)goto bad;
 name_type=dc_goal_type_of(rt,name);
 if(name_type!=rt->symbol_type && name_type!=rt->string_type)goto bad;
 symbol=dc_goal_find(rt,"*kernel-context*");context=symbol?dc_goal_symbol_value(rt,symbol):0;
 c=context_fields(rt,context);
 if(!c || activation_word(c,20)!=rt->s7 || activation_word(c,24)!=rt->s7 ||
    activation_word(c,12)>=INT32_MAX)goto bad;
 if(activation_overlap(process-4,reservation,destination-4,32) ||
    activation_overlap(process-4,reservation,context-4,48) ||
    activation_overlap(destination-4,32,context-4,48))goto bad;
 symbol=dc_goal_find(rt,"cpu-thread");cpu=symbol?dc_goal_symbol_value(rt,symbol):0;
 thread_type=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_THREAD_TYPE);
 if(!cpu || dc_goal_type_of(rt,cpu)!=rt->type_type)goto bad;
 schema=dc_goal_resolve(rt,cpu,sizeof *schema);
 if(!schema || schema->allocated_size!=128 || schema->num_methods!=12 || schema->parent!=thread_type)goto bad;
 cpu_bytes=20u+4u*schema->num_methods;
 if(stack_bytes<4096 || ((stack_base|stack_bytes)&15u) || stack_bytes>UINT32_MAX-stack_base)goto bad;
 if(!dc_goal_resolve(rt,stack_base,stack_bytes))return 0;
 if(activation_overlap(stack_base,stack_bytes,process-4,reservation) ||
    activation_overlap(stack_base,stack_bytes,destination-4,32) ||
    activation_overlap(stack_base,stack_bytes,context-4,48) ||
    activation_overlap(stack_base,stack_bytes,rt->table,DC_GOAL_SYMBOL_BYTES) ||
    activation_overlap(stack_base,stack_bytes,process_type-4,type_bytes) ||
    activation_overlap(stack_base,stack_bytes,tree_type-4,tree_bytes) ||
    activation_overlap(stack_base,stack_bytes,cpu-4,cpu_bytes))goto bad;
 /* Body write spans must also exclude namespace, schemas and dependency code. */
 if(activation_write_alias(process,reservation,destination,context,rt->table,DC_GOAL_SYMBOL_BYTES) ||
    activation_write_alias(process,reservation,destination,context,process_type-4,type_bytes) ||
    activation_write_alias(process,reservation,destination,context,tree_type-4,tree_bytes) ||
    activation_write_alias(process,reservation,destination,context,cpu-4,cpu_bytes))goto bad;
 for(i=0;i<4;++i) {
  symbol=dc_goal_find(rt,names[i]);body[i]=symbol?dc_goal_symbol_value(rt,symbol):0;
  if(!dc_goal_code_entry(rt,body[i],&code[i]))return 0;
  if(code[i].symbol!=symbol) {rt->error=DC_GOAL_BAD_FUNCTION;return 0;}
  if(activation_overlap(stack_base,stack_bytes,body[i]-4,code[i].bytes+4) ||
     activation_write_alias(process,reservation,destination,context,body[i]-4,code[i].bytes+4))goto bad;
 }
 if(dc_goal_get_method(rt,cpu,0)!=body[1])goto bad;
 rt->error=DC_GOAL_OK;
 value=(uint32_t)((activate)(uintptr_t)body[0])(process,destination,name,stack_base+stack_bytes);
 return rt->error==DC_GOAL_OK?value:0;
bad:
 (DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;
oom:
 rt->error=DC_GOAL_OUT_OF_MEMORY;return 0;
}

int dc_goal_resize_main_backup(dc_goal_runtime *rt,uint32_t process,uint32_t reservation,
                               uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,uint32_t backup_bytes) {
 typedef void (*setter)(uint64_t,uint64_t);
 dc_goal_code_entry_info code;
 uint32_t symbol,body;
 if(!rt || active_runtime!=rt || dc_goal_fiber_current()) {if(rt)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
 if(!dc_goal_thread_resize_check(rt,process,reservation,thread,stack_base,stack_bytes,backup_bytes))return 0;
 symbol=dc_goal_find(rt,"(method stack-size-set! thread)");body=symbol?dc_goal_symbol_value(rt,symbol):0;
 if(!dc_goal_code_entry(rt,body,&code))return 0;
 if(code.symbol!=symbol) {rt->error=DC_GOAL_BAD_FUNCTION;return 0;}
 if(activation_overlap(process-4,reservation,body-4,code.bytes+4)) {(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
 rt->error=DC_GOAL_OK;
 ((setter)(uintptr_t)body)(thread,backup_bytes);
 return rt->error==DC_GOAL_OK;
}

/* First original initialization catch adapter: root C stack only, inside a
 * registered lexical pp bracket. That bracket blocks all native stack
 * transfers. No fiber/suspended catch anchor is inferred from this C chain. */
typedef struct native_catch_frame {
 dc_goal_catch_raw raw;
 struct native_catch_frame *previous;
 dc_goal_runtime *runtime;
 dc_goal_process_span ownership;
 dc_goal_pp_scope_stack *scopes;
 dc_goal_pp_scope_frame prefix[DC_GOAL_PP_SCOPE_MAX_DEPTH];
 uint32_t process,pid,object,type,name,next,owner,base,bytes,expected_sp;
 unsigned depth,thrown;
 uint32_t function;uint64_t arguments[6];
} native_catch_frame;
static native_catch_frame *native_catch_top;
static uint32_t native_catch_owner;
static int catch_bad(dc_goal_runtime *rt){if(rt && rt->error==DC_GOAL_OK)(DC_GOAL_NOTE_SITE(1),rt->error=DC_GOAL_BAD_ARGUMENT);return 0;}
static uint32_t catch_word(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static void catch_put(void *p,uint32_t v){memcpy(p,&v,4);}
static uint32_t catch_physical(uint32_t p){return p&0x1fffffffu;}
static int catch_stack_range(uint32_t base,uint32_t bytes,uint32_t p,uint32_t n){
 /* Only the actual native P0/P1 RAM aliases are supported. Masking an arbitrary
  * P2/P3/P4 address and then dereferencing the original would be unsafe. */
 if(((p&0xe0000000u)!=0 && (p&0xe0000000u)!=0x80000000u) ||
    ((base&0xe0000000u)!=0 && (base&0xe0000000u)!=0x80000000u))return 0;
 base=catch_physical(base);p=catch_physical(p);
 return bytes<=0x20000000u-base && p>=base && n<=bytes && p-base<=bytes-n;
}
/* The stack a catch lives on: a running fiber's working stack (owner is the
 * fiber, tagged with bit 0), else the current KOS thread's stack. A catch body
 * cannot yield (its pp bracket blocks native stack transfers), so a catch made
 * on a fiber is thrown to and returned from on that same fiber. */
static int catch_stack_owner(uint32_t *owner,uint32_t *base,uint32_t *bytes){
 dc_goal_fiber *fiber=dc_goal_fiber_current();kthread_t *thread;
 if(fiber){*owner=(uint32_t)(uintptr_t)fiber|1u;*base=fiber->stack_base;*bytes=fiber->stack_bytes;return 1;}
 if(!(thread=(kthread_t *)(uintptr_t)dc_goal_kos_stack_owner()))return 0;
 *owner=(uint32_t)(uintptr_t)thread;*base=(uint32_t)(uintptr_t)thread->stack;*bytes=thread->stack_size;
 return 1;
}
static int catch_same_stack(const native_catch_frame *f){
 uint32_t owner,base,bytes;
 return f->owner && catch_stack_owner(&owner,&base,&bytes) && owner==f->owner && base==f->base && bytes==f->bytes;
}
static int catch_auth(native_catch_frame *f){
 dc_goal_runtime *rt=f->runtime;dc_goal_pp_scope_stack *scopes;uint32_t process;const void *p;
 if(rt!=active_runtime || !catch_same_stack(f) || !(scopes=dc_goal_pp_scope_active(rt)) ||
    scopes!=f->scopes || scopes->depth<f->depth || !f->depth ||
    memcmp(scopes->frames,f->prefix,f->depth*sizeof f->prefix[0]) ||
    !dc_goal_process_span_check(rt,&f->ownership))return catch_bad(rt);
 process=(uint32_t)current_process();
 if(process!=f->process || !(p=dc_goal_resolve(rt,process+36,4)) || catch_word(p)!=f->pid)return catch_bad(rt);
 if(!catch_stack_range(f->base,f->bytes,f->object-4,176))return catch_bad(rt);
 p=(const void *)(uintptr_t)(f->object-4);
 if(catch_word(p)!=f->type || catch_word((const unsigned char *)p+4)!=f->name ||
    catch_word((const unsigned char *)p+8)!=f->next)return catch_bad(rt);
 return 1;
}
static int catch_unwind_scopes(native_catch_frame *f){
 dc_goal_pp_scope_stack *scopes=f->scopes;
 while(scopes->depth>f->depth){
  dc_goal_pp_scope_frame *top=&scopes->frames[scopes->depth-1];
  if(!dc_goal_leave_lexical_scope(f->runtime,top->lexical_process,top->lexical_bytes))return 0;
 }
 return (uint32_t)current_process()==f->process?1:catch_bad(f->runtime);
}
static uint64_t catch_call_body(void *v){
 typedef uint64_t (*six)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
 native_catch_frame *f=v;
 return ((six)(uintptr_t)f->function)(f->arguments[0],f->arguments[1],f->arguments[2],
                                    f->arguments[3],f->arguments[4],f->arguments[5]);
}
static uint64_t new_catch_frame(uint64_t allocation,uint64_t type,uint64_t name,uint64_t function,uint64_t params){
 dc_goal_runtime *rt=active_runtime;native_catch_frame frame;dc_goal_pp_scope_stack *scopes;
 dc_goal_code_entry_info code;struct dc_goal_native_regs caller;
 uint32_t process,sp;unsigned char *top;uint64_t value;dc_goal_type_fields *schema;
 if(!rt || rt->error!=DC_GOAL_OK)return 0;
 memset(&frame,0,sizeof frame);
 if(allocation>UINT32_MAX || type>UINT32_MAX || name>UINT32_MAX || function>UINT32_MAX || params>UINT32_MAX ||
    ((uint32_t)allocation&15u) || ((uint32_t)params&7u) ||
    !(scopes=dc_goal_pp_scope_active(rt)) || !scopes->depth ||
    !catch_stack_owner(&frame.owner,&frame.base,&frame.bytes))return catch_bad(rt);
 if(native_catch_top && native_catch_owner!=frame.owner)return catch_bad(rt);
 __asm__ volatile("mov r15,%0":"=r"(sp));
 if(!catch_stack_range(frame.base,frame.bytes,(uint32_t)allocation,176) ||
    !catch_stack_range(frame.base,frame.bytes,(uint32_t)params,48) ||
    catch_physical((uint32_t)allocation)<catch_physical(sp) || catch_physical((uint32_t)params)<catch_physical(sp) ||
    activation_overlap(catch_physical((uint32_t)allocation),176,catch_physical((uint32_t)params),48))return catch_bad(rt);
 process=(uint32_t)current_process();
 if(!process || !dc_goal_process_span_lookup(rt,process,&frame.ownership) ||
    frame.ownership.bytes!=active_process_heap.allocation_bytes)return catch_bad(rt);
 if(type!=dc_goal_symbol_value(rt,dc_goal_find(rt,"catch-frame")) || dc_goal_type_of(rt,(uint32_t)type)!=rt->type_type ||
    !(schema=dc_goal_resolve(rt,(uint32_t)type,sizeof *schema)) || schema->allocated_size!=176 ||
    schema->heap_base || schema->num_methods!=9 ||
    schema->parent!=dc_goal_symbol_value(rt,dc_goal_find(rt,"stack-frame")) ||
    !dc_goal_code_entry(rt,(uint32_t)function,&code))return catch_bad(rt);
 if(!dc_goal_symbol_name(rt,(uint32_t)name))return 0;
 frame.runtime=rt;frame.process=process;frame.pid=catch_word(dc_goal_resolve(rt,process+36,4));
 frame.type=(uint32_t)type;frame.name=(uint32_t)name;frame.object=(uint32_t)allocation+4;frame.function=(uint32_t)function;
 frame.scopes=scopes;frame.depth=scopes->depth;memcpy(frame.prefix,scopes->frames,frame.depth*sizeof frame.prefix[0]);
 memcpy(frame.arguments,(const void *)(uintptr_t)(uint32_t)params,sizeof frame.arguments);
 top=dc_goal_resolve(rt,process+88,4);if(!top)return 0;frame.next=catch_word(top);
 /* Original raw field offsets include the basic header; native saved state is
  * private C storage, never guest SP/RA fields. The guest diagnostic slots zero. */
 memset((void *)(uintptr_t)(uint32_t)allocation,0,176);
 catch_put((void *)(uintptr_t)(uint32_t)allocation,frame.type);
 catch_put((void *)(uintptr_t)(frame.object),frame.name);catch_put((void *)(uintptr_t)(frame.object+4),frame.next);
 catch_put(top,frame.object);frame.previous=native_catch_top;native_catch_top=&frame;native_catch_owner=frame.owner;
 if(!dc_goal_native_regs_snapshot(&caller)){native_catch_top=frame.previous;native_catch_owner=frame.previous?frame.owner:0;catch_put(top,frame.next);return catch_bad(rt);}
 frame.expected_sp=caller.sp-4;
 value=dc_goal_catch_invoke_raw(&frame.raw,catch_call_body,&frame);
 if(native_catch_top!=&frame || !catch_auth(&frame) || frame.scopes->depth!=frame.depth ||
    catch_word(top)!=(frame.thrown?frame.next:frame.object)){
  enum dc_goal_error prior=rt->error;
  /* Our stack objects expire on every C return, including faults. Restore the
   * old chain only while the same physical allocation is still owned. A stale
   * or retired allocation is never written through a saved process pointer. */
  if(catch_same_stack(&frame) && dc_goal_process_span_check(rt,&frame.ownership))catch_put(top,frame.next);
  native_catch_top=frame.previous;native_catch_owner=frame.previous?frame.owner:0;
  if(prior!=DC_GOAL_OK)rt->error=prior;
  catch_bad(rt);return 0;
 }
 native_catch_top=frame.previous;native_catch_owner=frame.previous?frame.owner:0;
 catch_put(top,frame.next);return rt->error==DC_GOAL_OK?value:0;
}
static int process_held(uint32_t process){
 const native_catch_frame *f;unsigned i;
 for(f=native_catch_top;f;f=f->previous)if(f->process==process)return 1;
 for(i=0;i<DC_THREAD_FIBERS;++i)if(thread_fibers[i] && thread_fibers[i]->process==process)return 1;
 return 0;
}
static uint64_t catch_throw_dispatch(uint64_t object,uint64_t value){
 dc_goal_runtime *rt=active_runtime;native_catch_frame *f;
 if(!rt || rt->error!=DC_GOAL_OK)return 0;
 {uint32_t owner,base,bytes;
  if(object>UINT32_MAX || !native_catch_top || !native_catch_owner ||
     !catch_stack_owner(&owner,&base,&bytes) || owner!=native_catch_owner)return catch_bad(rt);}
 /* Only trusted active C descriptors are traversed. A guest pointer is a key,
  * never a descriptor address. No discarded inner descriptor is retained. */
 for(f=native_catch_top;f && f->object!=(uint32_t)object;f=f->previous){}
 if(!f || !catch_auth(f) || f->raw.regs.pr!=(uint32_t)(uintptr_t)dc_goal_catch_invoke_return ||
    f->raw.regs.sp!=f->expected_sp || (f->raw.regs.sp&3u) || (f->raw.regs.fpscr&0x00380000u) ||
    !catch_stack_range(f->base,f->bytes,f->raw.regs.sp,4) || !catch_unwind_scopes(f))return catch_bad(rt);
 catch_put(dc_goal_resolve(rt,f->process+88,4),f->next);
 native_catch_top=f;f->thrown=1;f->raw.result=value;dc_goal_catch_escape_raw(&f->raw);
}
int dc_goal_install_catch_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result){
 unsigned char code[2][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
 uint32_t targets[2]={(uint32_t)(uintptr_t)new_catch_frame,(uint32_t)(uintptr_t)catch_throw_dispatch};
 dc_goal_code_export exports[2]={{"dc-c-catch-frame-new",code[0],12,NULL,0},{"throw-dispatch",code[1],12,NULL,0}};
 uint32_t type;
 if(!rt || !result || rt!=active_runtime || native_catch_top || dc_goal_fiber_current())return catch_bad(rt);
 type=dc_goal_define_type(rt,"catch-frame",dc_goal_symbol_value(rt,dc_goal_find(rt,"stack-frame")),176,0,9);
 if(!type)return 0;
 memcpy(code[0]+8,&targets[0],4);memcpy(code[1]+8,&targets[1],4);
 if(!dc_goal_load_module(rt,exports,2,sync,NULL,result))return 0;
 return dc_goal_set_method(rt,type,0,dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-catch-frame-new")));
}
