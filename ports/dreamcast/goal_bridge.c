/* Native scalar GOAL -> C tail bridges (AI-assisted), ISC. */
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include "goal_bridge.h"
#include "goal_fiber.h"
#include "goal_thread_resize.h"
#include "goal_main_start.h"
#include "goal_process_heap.h"
#include "pp_scope.h"
#include "process_spans.h"
#include "goal_kos_stack.h"
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
  if(span.bytes!=bytes){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  return 1;
}
static int refuse_live_scope(dc_goal_runtime *rt) {
  if(dc_goal_pp_scope_runtime_clear_allowed(rt))return 0;
  if(rt && rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;
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
  if(!rt || active_runtime!=rt) { if(rt) rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  if(refuse_live_scope(rt))return 0;
  dc_goal_clear_process_context(rt);
  if(!known_span_matches(rt,process,allocation_bytes))return 0;
  if(!dc_goal_process_heap_open(&candidate,rt,process,allocation_bytes)) return 0;
  symbol=dc_goal_find(rt,"*kernel-context*");
  context=symbol?dc_goal_symbol_value(rt,symbol):0;
  fields=context_fields(rt,context);
  if(!fields) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  memcpy(&current,fields+20,4);
  if(current!=rt->s7) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  execution_context=context; execution_heap=candidate; active_process_heap=candidate;
  memcpy(fields+20,&process,4); rt->error=DC_GOAL_OK; return 1;
}
static uint64_t current_process(void) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_process_heap candidate;
  unsigned char *fields;
  uint32_t symbol,current=0,lexical=0;int scoped;
  if(!rt) return 0;
  scoped=dc_goal_pp_scope_resolve(rt,execution_context,execution_heap.process,execution_heap.allocation_bytes,
                                  &active_process_heap,&lexical);
  if(scoped==DC_GOAL_PP_SCOPE_DENIED)return 0;
  if(scoped==DC_GOAL_PP_SCOPE_READY)return lexical; /* retain faults through cleanup */
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
  dc_goal_clear_process_context(rt); rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
}
static uint64_t new_pair(uint64_t heap,uint64_t type,uint64_t car,uint64_t cdr) {
  dc_goal_runtime *rt=active_runtime;
  if (!rt) return 0;
  if (heap!=rt->s7+DC_FIX_SYM_GLOBAL_HEAP || type!=rt->pair_type) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  return dc_goal_new_pair(rt,(uint32_t)car,(uint32_t)cdr);
}
static uint64_t type_of(uint64_t object) {
  return active_runtime?dc_goal_type_of(active_runtime,(uint32_t)object):0;
}
static uint64_t new_type(uint64_t symbol,uint64_t parent,uint64_t flags) {
  return active_runtime?dc_goal_new_type(active_runtime,(uint32_t)symbol,(uint32_t)parent,flags):0;
}
static uint64_t method_set(uint64_t type,uint64_t index,uint64_t method) {
  dc_goal_runtime *rt=active_runtime;
  uint32_t enable;
  if (!rt) return 0;
  if (index>UINT32_MAX || method>UINT32_MAX) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  enable=dc_goal_find(rt,"*enable-method-set*");
  return dc_goal_method_set(rt,(uint32_t)type,(uint32_t)index,(uint32_t)method,
                            enable && dc_goal_symbol_value(rt,enable)!=0);
}
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
  if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;
  return 0; /* nested process ownership needs a verified parent-subspan protocol */
}
static uint32_t global_basic_new(dc_goal_runtime *rt,uint32_t type,uint32_t bytes) {
  int configured=dc_goal_process_spans_registered(rt),tracked=0;uint32_t result;
  if(configured<0)return 0;
  if(configured){tracked=process_family(rt,type);if(tracked<0)return 0;}
  if(tracked && (bytes<112 || dc_goal_process_span_room(rt)!=1)){
    if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;
    return 0;
  }
  result=dc_goal_new_basic(rt,type,bytes);
  if(result && tracked && !dc_goal_process_span_note(rt,result,bytes,NULL))return 0;
  return result;
}
static uint64_t new_basic(uint64_t heap,uint64_t type) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_type_fields *fields;
  if (!rt) return 0;
  if(dc_goal_pp_scope_blocks_transfer(rt) && rt->error!=DC_GOAL_OK)return 0;
  if (type>UINT32_MAX ||
      !dc_goal_is_subtype(rt,(uint32_t)type,rt->basic_type)) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  fields=dc_goal_resolve(rt,(uint32_t)type,sizeof(*fields));
  if(!fields) return 0;
  if(heap==rt->s7+DC_FIX_SYM_GLOBAL_HEAP)
    return global_basic_new(rt,(uint32_t)type,fields->allocated_size);
  if(heap==rt->s7+DC_FIX_SYM_PROCESS_TYPE && active_process_heap.runtime==rt) {
    uint32_t lexical;
    if(!process_heap_type_allowed(rt,(uint32_t)type))return 0;
    if(dc_goal_pp_scope_resolve(rt,execution_context,execution_heap.process,execution_heap.allocation_bytes,
                                &active_process_heap,&lexical)==DC_GOAL_PP_SCOPE_DENIED)return 0;
    return dc_goal_process_new_basic(&active_process_heap,(uint32_t)type,fields->allocated_size);
  }
  rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
}
static uint64_t new_object(uint64_t heap,uint64_t type,uint64_t size) {
  dc_goal_runtime *rt=active_runtime;
  if (!rt) return 0;
  if(dc_goal_pp_scope_blocks_transfer(rt) && rt->error!=DC_GOAL_OK)return 0;
  if (type>UINT32_MAX || size>UINT32_MAX ||
      !dc_goal_is_subtype(rt,(uint32_t)type,rt->basic_type)) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  if(heap==rt->s7+DC_FIX_SYM_GLOBAL_HEAP)
    return global_basic_new(rt,(uint32_t)type,(uint32_t)size);
  if(heap==rt->s7+DC_FIX_SYM_PROCESS_TYPE && active_process_heap.runtime==rt) {
    uint32_t lexical;
    if(!process_heap_type_allowed(rt,(uint32_t)type))return 0;
    if(dc_goal_pp_scope_resolve(rt,execution_context,execution_heap.process,execution_heap.allocation_bytes,
                                &active_process_heap,&lexical)==DC_GOAL_PP_SCOPE_DENIED)return 0;
    return dc_goal_process_new_basic(&active_process_heap,(uint32_t)type,(uint32_t)size);
  }
  rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
}
static uint64_t integer_error(uint64_t kind) {
  if (active_runtime) active_runtime->error=kind==1?DC_GOAL_DIVIDE_BY_ZERO:
                                            (kind==2?DC_GOAL_INTEGER_OVERFLOW:DC_GOAL_BAD_ARGUMENT);
  return 0;
}
static uint64_t missing_call(void) {
  if(active_runtime) active_runtime->error=DC_GOAL_BAD_FUNCTION;
  return 0;
}
/* The original arming body stores this address. Execution through the original
 * architecture-specific bootstrap is unsupported and fails closed. (AI-assisted) */
int dc_goal_install_main_start_guard(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
 unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 dc_goal_code_export function={"set-to-run-bootstrap",code,12,NULL,0};
 uint32_t target=(uint32_t)(uintptr_t)missing_call;
 if(!rt || active_runtime!=rt || dc_goal_fiber_current()) {if(rt)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
 memcpy(code+8,&target,4);return dc_goal_load_module(rt,&function,1,sync,NULL,result);
}
int dc_goal_install_core_bridges(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  /* MOV.L literal,R0; JMP @R0; NOP; alignment NOP; .long C entry.
   * Tail dispatch leaves PR and outgoing GCC stack arguments intact. */
  unsigned char code[3][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},
                            {1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},
                            {1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
  dc_goal_code_export functions[3]={{"dc-c-pair-new",code[0],12,NULL,0},
                                    {"dc-c-type-of",code[1],12,NULL,0},
                                    {"dc-c-call-error",code[2],12,NULL,0}};
  uint32_t target,entry,global,global_value=0;
  if (!rt || !result) return 0;
  if (active_runtime && active_runtime!=rt) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  if(refuse_live_scope(rt))return 0;
  global=dc_goal_find(rt,"global");
  if (global) global_value=dc_goal_symbol_value(rt,global);
  if (!dc_goal_bind_fixed(rt,DC_FIX_SYM_GLOBAL_HEAP,"global",global_value)) return 0;
  target=(uint32_t)(uintptr_t)new_pair; memcpy(code[0]+8,&target,4);
  target=(uint32_t)(uintptr_t)type_of; memcpy(code[1]+8,&target,4);
  target=(uint32_t)(uintptr_t)missing_call; memcpy(code[2]+8,&target,4);
  if (!dc_goal_load_module(rt,functions,3,sync,NULL,result)) return 0;
  entry=dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-pair-new"));
  if (!dc_goal_set_method(rt,rt->pair_type,0,entry)) return 0;
  active_runtime=rt;
  return 1;
}
void dc_goal_clear_core_bridges(dc_goal_runtime *rt) {
  if (active_runtime==rt && rt) {
    dc_goal_pp_scope_stack *stack;
    if(dc_goal_fiber_current()){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return;}
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
  unsigned char code[3][12];
  dc_goal_code_export functions[3]={{"dc-c-type-new",code[0],12,NULL,0},
                                    {"method-set!",code[1],12,NULL,0},
                                    {"dc-c-basic-new",code[2],12,NULL,0}};
  uint32_t targets[3]={(uint32_t)(uintptr_t)new_type,(uint32_t)(uintptr_t)method_set,
                       (uint32_t)(uintptr_t)new_basic};
  unsigned i;
  if (!rt || !result) return 0;
  if (active_runtime!=rt) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  for (i=0;i<3;++i) { memcpy(code[i],stub,12); memcpy(code[i]+8,&targets[i],4); }
  if (!dc_goal_load_module(rt,functions,3,sync,NULL,result)) return 0;
  return dc_goal_set_method(rt,rt->type_type,0,dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-type-new"))) &&
         dc_goal_set_method(rt,rt->basic_type,0,dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-basic-new")));
}
int dc_goal_install_integer_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  uint32_t target=(uint32_t)(uintptr_t)integer_error;
  dc_goal_code_export function={"dc-c-integer-error",code,12,NULL,0};
  if (!rt || !result) return 0;
  if (active_runtime!=rt) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  memcpy(code+8,&target,4);
  return dc_goal_load_module(rt,&function,1,sync,NULL,result);
}


int dc_goal_install_object_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  uint32_t target=(uint32_t)(uintptr_t)new_object;
  dc_goal_code_export function={"dc-c-object-new",code,12,NULL,0};
  if (!rt || !result) return 0;
  if (active_runtime!=rt) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  memcpy(code+8,&target,4);
  if (!dc_goal_load_module(rt,&function,1,sync,NULL,result)) return 0;
  return dc_goal_set_method(rt,rt->object_type,0,dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-object-new")));
}


int dc_goal_bind_process_heap(dc_goal_runtime *rt,uint32_t process,uint32_t allocation_bytes) {
  if(!rt || active_runtime!=rt) {
    if(rt) rt->error=DC_GOAL_BAD_ARGUMENT;
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
  if(!rt || active_runtime!=rt){if(rt && rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  return dc_goal_pp_scope_init(stack,rt,&hooks);
}
int dc_goal_enter_lexical_scope(dc_goal_runtime *rt,uint32_t process,uint32_t bytes) {
  dc_goal_pp_scope_stack *stack;
  if(!rt || active_runtime!=rt || execution_heap.runtime!=rt){if(rt && rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  stack=dc_goal_pp_scope_active(rt);
  if(!stack){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  if(!known_span_matches(rt,process,bytes))return 0;
  return dc_goal_pp_scope_enter(stack,rt,execution_context,execution_heap.process,execution_heap.allocation_bytes,
                                process,bytes,&active_process_heap,&active_process_heap);
}
int dc_goal_leave_lexical_scope(dc_goal_runtime *rt,uint32_t process,uint32_t bytes) {
  dc_goal_pp_scope_stack *stack;
  if(!rt || active_runtime!=rt){if(rt && rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  stack=dc_goal_pp_scope_active(rt);
  if(!stack){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
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
int dc_goal_register_process_spans(dc_goal_runtime *rt,dc_goal_process_spans *table,dc_goal_process_span *rows,uint32_t capacity) {
  const dc_goal_process_span_hooks hooks={dc_goal_kos_stack_owner,spans_quiescent};
  if(!rt || active_runtime!=rt){if(rt&&rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  if(process_family(rt,dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_PROCESS_TYPE))!=1)return 0;
  return dc_goal_process_spans_init(table,rt,rows,capacity,&hooks);
}
int dc_goal_note_process_span(dc_goal_runtime *rt,uint32_t process,uint32_t bytes,dc_goal_process_span *out) {
  if(!rt || active_runtime!=rt){if(rt&&rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  return dc_goal_process_span_note(rt,process,bytes,out);
}
int dc_goal_enter_known_scope(dc_goal_runtime *rt,uint32_t process) {
  dc_goal_process_span span;
  if(!rt || active_runtime!=rt){if(rt&&rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  return dc_goal_process_span_lookup(rt,process,&span) && dc_goal_enter_lexical_scope(rt,process,span.bytes);
}
int dc_goal_leave_known_scope(dc_goal_runtime *rt,uint32_t previous_process) {
  dc_goal_pp_scope_stack *stack;dc_goal_pp_scope_frame *top;
  if(!rt || active_runtime!=rt){if(rt&&rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  stack=dc_goal_pp_scope_active(rt);
  if(!stack || !stack->depth){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  top=&stack->frames[stack->depth-1];
  if(top->saved_allocation.process!=previous_process){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  return dc_goal_leave_lexical_scope(rt,top->lexical_process,top->lexical_bytes);
}

/* Compiled original (set! pp X) ... (set! pp saved) brackets. Both are boxed
 * 0/1 results; the lowerer exits before body memory access on zero. */
static uint64_t enter_process_scope(uint64_t process) {
  dc_goal_runtime *rt=active_runtime;
  if(!rt)return 0;
  if(process>UINT32_MAX){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  return (uint64_t)dc_goal_enter_known_scope(rt,(uint32_t)process);
}
static uint64_t leave_process_scope(uint64_t previous_process) {
  dc_goal_runtime *rt=active_runtime;
  if(!rt)return 0;
  if(previous_process>UINT32_MAX){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  return (uint64_t)dc_goal_leave_known_scope(rt,(uint32_t)previous_process);
}
int dc_goal_install_scope_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  unsigned char code[2][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
  uint32_t targets[2]={(uint32_t)(uintptr_t)enter_process_scope,(uint32_t)(uintptr_t)leave_process_scope};
  dc_goal_code_export functions[2]={{"dc-c-enter-process-scope",code[0],12,NULL,0},
                                    {"dc-c-leave-process-scope",code[1],12,NULL,0}};
  if(!rt || !result) return 0;
  if(active_runtime!=rt) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
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
  dc_goal_fiber_yield(rt,0);
  return rt->error==DC_GOAL_OK;
}
/* Original enter-state main-thread branch: reset the main thread's stack and
 * run the new state's code, returning through return-from-thread-dead. On the
 * bound scheduler process's running fiber this restarts the fiber at `code`. */
static uint64_t enter_state_code(uint64_t code,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_fiber *fiber=dc_goal_fiber_current();
  uint64_t arguments[4]={a0,a1,a2,a3};
  if(!rt)return 0;
  if(code>UINT32_MAX){if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  if(fiber && fiber->runtime==rt && execution_context && execution_heap.runtime==rt &&
     fiber->process==execution_heap.process && rt->error==DC_GOAL_OK)
    dc_goal_fiber_restart(rt,(uint32_t)code,4,arguments); /* returns only on refusal */
  if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_UNSUPPORTED_TRANSFER;
  return 0;
}
int dc_goal_install_transfer_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  unsigned char code[3][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},
                             {1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
  uint32_t targets[3]={(uint32_t)(uintptr_t)return_from_thread,(uint32_t)(uintptr_t)suspend_current,
                       (uint32_t)(uintptr_t)enter_state_code};
  dc_goal_code_export functions[3]={{"dc-c-return-from-thread",code[0],12,NULL,0},
                                    {"dc-c-suspend",code[1],12,NULL,0},
                                    {"dc-c-enter-state-code",code[2],12,NULL,0}};
  unsigned i;
  if(!rt || !result) return 0;
  if(active_runtime!=rt) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  for(i=0;i<3;++i)memcpy(code[i]+8,&targets[i],4);
  return dc_goal_load_module(rt,functions,3,sync,NULL,result);
}

int dc_goal_install_process_bridge(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  uint32_t target=(uint32_t)(uintptr_t)current_process;
  dc_goal_code_export function={"dc-c-current-process",code,12,NULL,0};
  if(!rt || !result) return 0;
  if(active_runtime!=rt) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
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
  if(!rt || active_runtime!=rt) { if(rt) rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
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
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  if(!dc_goal_resolve(rt,stack_base,stack_bytes)) return 0;
  if(stack_base<process-4+reservation_bytes && stack_base+stack_bytes>process-4) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  fields=dc_goal_resolve(rt,process,108);
  if(!fields) return 0;
  memcpy(&main_thread,fields+40,4); memcpy(&top_thread,fields+44,4);
  if(main_thread!=rt->s7 || top_thread!=rt->s7) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
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
 if(!rt || active_runtime!=rt) {if(rt)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
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
 if(!rt || active_runtime!=rt) {if(rt)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
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
 rt->error=DC_GOAL_BAD_ARGUMENT;return 0;
oom:
 rt->error=DC_GOAL_OUT_OF_MEMORY;return 0;
}

int dc_goal_resize_main_backup(dc_goal_runtime *rt,uint32_t process,uint32_t reservation,
                               uint32_t thread,uint32_t stack_base,uint32_t stack_bytes,uint32_t backup_bytes) {
 typedef void (*setter)(uint64_t,uint64_t);
 dc_goal_code_entry_info code;
 uint32_t symbol,body;
 if(!rt || active_runtime!=rt || dc_goal_fiber_current()) {if(rt)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
 if(!dc_goal_thread_resize_check(rt,process,reservation,thread,stack_base,stack_bytes,backup_bytes))return 0;
 symbol=dc_goal_find(rt,"(method stack-size-set! thread)");body=symbol?dc_goal_symbol_value(rt,symbol):0;
 if(!dc_goal_code_entry(rt,body,&code))return 0;
 if(code.symbol!=symbol) {rt->error=DC_GOAL_BAD_FUNCTION;return 0;}
 if(activation_overlap(process-4,reservation,body-4,code.bytes+4)) {rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
 rt->error=DC_GOAL_OK;
 ((setter)(uintptr_t)body)(thread,backup_bytes);
 return rt->error==DC_GOAL_OK;
}
