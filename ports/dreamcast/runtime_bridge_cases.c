/* Original cons macro through native method/C bridge (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "build/bridge_fixture.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef uint64_t (*three)(uint64_t,uint64_t,uint64_t);
typedef uint64_t (*four)(uint64_t,uint64_t,uint64_t,uint64_t);
typedef uint64_t (*eight)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t word_at(dc_goal_runtime *rt,uint32_t at) {
  uint32_t value; memcpy(&value,dc_goal_resolve(rt,at,4),4); return value;
}
uint32_t dc_runtime_bridge_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                  unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result bridges,module;
  const uint64_t payload=UINT64_C(0x12345678abcdef01);
  uint32_t pair,head,method,saved_used,capacity;
#define CHECK(actual,want,label) check((uint64_t)(actual),(uint64_t)(want),++*id,label,context)
#define ONE(name,x) ((one)(uintptr_t)entry(rt,name))((uint64_t)(x))
#define TWO(name,x,y) ((two)(uintptr_t)entry(rt,name))((uint64_t)(x),(uint64_t)(y))
  if (!dc_goal_bind_fixed(rt,DC_FIX_SYM_GLOBAL_HEAP,"global",rt->s7) ||
      !dc_goal_install_core_bridges(rt,sync,&bridges) ||
      !dc_goal_load_module(rt,goal_bridge_fixture_exports,GOAL_BRIDGE_FIXTURE_EXPORT_COUNT,sync,NULL,&module)) {
    CHECK(rt->error,0,"native bridge installation"); return 0;
  }
  CHECK(bridges.functions,3,"three native boxed C bridges");
  CHECK(bridges.code_bytes,36,"compact tail bridge code size");
  CHECK(dc_goal_find(rt,"global"),rt->s7+DC_FIX_SYM_GLOBAL_HEAP,"original fixed global selector slot");
  CHECK(dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_GLOBAL_HEAP),rt->s7,"bridge setup preserves existing global symbol value");
  dc_goal_set_symbol(rt,rt->s7+DC_FIX_SYM_GLOBAL_HEAP,0);
  method=dc_goal_get_method(rt,rt->pair_type,0);
  CHECK(dc_goal_type_of(rt,method),rt->function_type,"native pair constructor method entry");
  CHECK(((three)(uintptr_t)entry(rt,"dc-bridge-third"))(1,2,payload),payload,"incoming third integer argument on stack");
  CHECK(((eight)(uintptr_t)entry(rt,"dc-bridge-eighth"))(1,2,3,4,5,6,7,payload),payload,"incoming eighth integer argument on stack");
  CHECK(ONE("dc-bridge-stack-call",payload),payload,"compiled call marshals six stack arguments");
  pair=(uint32_t)TWO("dc-bridge-cons",rt->s7,rt->s7-10u);
  CHECK(dc_goal_type_of(rt,pair),rt->pair_type,"original cons allocates native pair");
  CHECK(word_at(rt,pair-2),rt->s7,"C bridge receives third argument car");
  CHECK(word_at(rt,pair+2),rt->s7-10u,"C bridge receives fourth argument cdr");
  CHECK(TWO("ref",pair,0),rt->s7,"original ref consumes GOAL-allocated pair");
  head=(uint32_t)TWO("dc-bridge-cons",rt->s7+8u,pair);
  CHECK(ONE("last",head),pair,"original last consumes GOAL-created list");
  CHECK(TWO("member",rt->s7+8u,head),head,"original member on new list head");
  CHECK(TWO("member",rt->s7,head),pair,"original member on new list tail");
  CHECK(ONE("dc-bridge-type",head),rt->pair_type,"typed external C call returns pair type");
  CHECK(ONE("dc-bridge-type",entry(rt,"dc-static-text")),rt->string_type,"typed external C call returns string type");
  CHECK(ONE("dc-bridge-type",(uint64_t)-8),rt->binteger_type,"C bridge handles signed binteger object");
  saved_used=rt->used;
  dc_goal_set_method(rt,rt->pair_type,0,entry(rt,"dc-bridge-return-cdr"));
  CHECK(TWO("dc-bridge-cons",rt->s7+8u,pair),pair,"typed method call observes GOAL method replacement");
  CHECK(rt->used,saved_used,"replacement method does not allocate");
  dc_goal_set_method(rt,rt->pair_type,0,method);
  CHECK(TWO("dc-bridge-cons",rt->s7+8u,pair)!=pair,1,"restored C constructor allocates again");
  capacity=rt->capacity; rt->capacity=rt->used; saved_used=rt->used;
  CHECK(TWO("dc-bridge-cons",rt->s7,rt->s7-10u),0,"GOAL allocation bridge reports exhaustion");
  CHECK(rt->error,DC_GOAL_OUT_OF_MEMORY,"allocation error preserved across native return");
  CHECK(rt->used,saved_used,"failed GOAL allocation leaves arena unchanged");
  rt->capacity=capacity;
  CHECK(((four)(uintptr_t)method)(rt->s7,rt->pair_type,rt->s7,rt->s7-10u),0,"unsupported heap selector rejected");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"unsupported heap has explicit bridge error");
  CHECK(rt->used,saved_used,"unsupported heap does not allocate");
#undef TWO
#undef ONE
#undef CHECK
  return bridges.code_bytes+module.code_bytes;
}
