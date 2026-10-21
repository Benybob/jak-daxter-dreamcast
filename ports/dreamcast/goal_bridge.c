/* Native scalar GOAL -> C tail bridges (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#ifndef __DREAMCAST__
#error These boxed tail bridges require native SH-4 code execution.
#endif
static dc_goal_runtime *active_runtime;
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
static uint64_t new_basic(uint64_t heap,uint64_t type) {
  dc_goal_runtime *rt=active_runtime;
  dc_goal_type_fields *fields;
  if (!rt) return 0;
  if (heap!=rt->s7+DC_FIX_SYM_GLOBAL_HEAP || type>UINT32_MAX ||
      !dc_goal_is_subtype(rt,(uint32_t)type,rt->basic_type)) {
    rt->error=DC_GOAL_BAD_ARGUMENT; return 0;
  }
  fields=dc_goal_resolve(rt,(uint32_t)type,sizeof(*fields));
  return fields?dc_goal_new_basic(rt,(uint32_t)type,fields->allocated_size):0;
}
static uint64_t integer_error(uint64_t kind) {
  if (active_runtime) active_runtime->error=kind==1?DC_GOAL_DIVIDE_BY_ZERO:
                                            (kind==2?DC_GOAL_INTEGER_OVERFLOW:DC_GOAL_BAD_ARGUMENT);
  return 0;
}
int dc_goal_install_core_bridges(dc_goal_runtime *rt,dc_goal_cache_sync sync,dc_goal_module_result *result) {
  /* MOV.L literal,R0; JMP @R0; NOP; alignment NOP; .long C entry.
   * Tail dispatch leaves PR and outgoing GCC stack arguments intact. */
  unsigned char code[2][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},
                            {1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
  dc_goal_code_export functions[2]={{"dc-c-pair-new",code[0],12,NULL,0},
                                    {"dc-c-type-of",code[1],12,NULL,0}};
  uint32_t target,entry,global,global_value=0;
  if (!rt || !result) return 0;
  if (active_runtime && active_runtime!=rt) { rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  global=dc_goal_find(rt,"global");
  if (global) global_value=dc_goal_symbol_value(rt,global);
  if (!dc_goal_bind_fixed(rt,DC_FIX_SYM_GLOBAL_HEAP,"global",global_value)) return 0;
  target=(uint32_t)(uintptr_t)new_pair; memcpy(code[0]+8,&target,4);
  target=(uint32_t)(uintptr_t)type_of; memcpy(code[1]+8,&target,4);
  if (!dc_goal_load_module(rt,functions,2,sync,NULL,result)) return 0;
  entry=dc_goal_symbol_value(rt,dc_goal_find(rt,"dc-c-pair-new"));
  if (!dc_goal_set_method(rt,rt->pair_type,0,entry)) return 0;
  active_runtime=rt;
  return 1;
}
void dc_goal_clear_core_bridges(dc_goal_runtime *rt) {
  if (active_runtime==rt) active_runtime=NULL;
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
