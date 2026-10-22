/* Null/false calls leave through restored native caller frames (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "build/call_guard_fixture.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef uint64_t (*eight)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef float (*floating)(float);
typedef void (*effect)(uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t bits(float value) { uint32_t result; memcpy(&result,&value,4); return result; }
uint32_t dc_runtime_call_guard_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                    unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result module;
  const char *targets[]={"dc-call-int-target","dc-call-float-target","dc-call-void-target","dc-call-eight-target"};
  const char *valid[]={"dc-call-guard-identity","dc-call-guard-float-identity","dc-call-guard-effect","dc-call-guard-last"};
  uint32_t target[4],after,effects,method_type,object,old_method,used;
  uint32_t i,j,invalid[2]={0,rt->s7};
  const uint64_t value=UINT64_C(0x12345678abcdef01);
  volatile uint32_t stack_guard[4]={0x01234567,0x89abcdef,0x76543210,0xfedcba98};
  one integer,method;
  two dynamic;
  eight stacked;
  floating fp;
  effect no_result;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module(rt,goal_call_guard_fixture_exports,GOAL_CALL_GUARD_FIXTURE_EXPORT_COUNT,
                           sync,NULL,&module),1,"typed missing-call probes link against installed strict fault bridge");
  if(!module.functions) return 0;
  CHECK(module.functions,9,"five caller probes and four typed real implementations");
  after=dc_goal_find(rt,"dc-call-after"); effects=dc_goal_find(rt,"dc-call-effects");
  integer=(one)(uintptr_t)entry(rt,"dc-call-guard-int"); fp=(floating)(uintptr_t)entry(rt,"dc-call-guard-float");
  no_result=(effect)(uintptr_t)entry(rt,"dc-call-guard-void"); stacked=(eight)(uintptr_t)entry(rt,"dc-call-guard-eight");
  dynamic=(two)(uintptr_t)entry(rt,"dc-call-guard-dynamic");
  for(i=0;i<4;++i) target[i]=dc_goal_find(rt,targets[i]);
  method_type=entry(rt,"dc-native-record");
  object=dc_goal_new_basic(rt,method_type,16);
  CHECK(object!=0,1,"virtual call probe uses existing compiler-defined class");
  if(!object) return module.code_bytes;
  old_method=dc_goal_get_method(rt,method_type,9); method=(one)(uintptr_t)entry(rt,"dc-type-score");
  used=rt->used;
  for(i=0;i<2;++i) {
    for(j=0;j<4;++j) CHECK(dc_goal_set_symbol(rt,target[j],invalid[i]),1,"live typed function cell set to missing/false sentinel");
    dc_goal_set_symbol(rt,after,0); dc_goal_set_symbol(rt,effects,0);
    CHECK(integer(value),0,"missing integer target defines both return halves as zero");
    CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"missing integer call reports explicit bad-function status");
    CHECK(dc_goal_symbol_value(rt,after),0,"missing integer target skips continuation side effects");
    CHECK(bits(fp(1.25f)),0,"missing float target defines FR0 as positive zero");
    CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"missing float call reports explicit status");
    CHECK(dc_goal_symbol_value(rt,after),0,"missing float call skips continuation");
    no_result(77);
    CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"missing void call reports status without inventing a result");
    CHECK(dc_goal_symbol_value(rt,after),0,"missing void call skips caller continuation");
    CHECK(dc_goal_symbol_value(rt,effects),0,"missing void implementation has no effects");
    CHECK(stacked(1,2,3,4,5,6,7,value),0,"missing eight-argument target exits with outgoing stack still reserved");
    CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"missing stack call explicit status");
    CHECK(dc_goal_symbol_value(rt,after),0,"missing stack target skips continuation");
    CHECK(stack_guard[0],0x01234567,"stack exit preserves caller guard zero");
    CHECK(stack_guard[1],0x89abcdef,"stack exit preserves caller guard one");
    CHECK(stack_guard[2],0x76543210,"stack exit preserves caller guard two");
    CHECK(stack_guard[3],0xfedcba98,"stack exit preserves caller guard three");
    CHECK(dynamic(invalid[i],value),0,"missing dynamic function argument exits safely");
    CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"dynamic missing call explicit status");
    CHECK(dc_goal_symbol_value(rt,after),0,"dynamic missing call skips continuation");
    CHECK(dc_goal_set_method(rt,method_type,9,invalid[i]),1,"virtual method set to absent/false sentinel");
    CHECK(method(object),0,"absent virtual method exits through ordinary caller epilogue");
    CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"absent virtual method explicit status");
  }
  for(i=0;i<4;++i) CHECK(dc_goal_set_symbol(rt,target[i],entry(rt,valid[i])),1,"valid typed function binding restored");
  rt->error=DC_GOAL_OK;
  CHECK(integer(value),value+1,"integer call recovers through restored live binding");
  CHECK(dc_goal_symbol_value(rt,after),1,"valid integer call executes continuation");
  dc_goal_set_symbol(rt,after,0);
  CHECK(bits(fp(1.25f)),bits(2.75f),"valid floating call and arithmetic recover");
  CHECK(dc_goal_symbol_value(rt,after),1,"valid float call executes continuation");
  dc_goal_set_symbol(rt,after,0); no_result(77);
  CHECK(dc_goal_symbol_value(rt,effects),77,"valid void callee executes its side effect");
  CHECK(dc_goal_symbol_value(rt,after),1,"valid void call executes caller continuation");
  dc_goal_set_symbol(rt,after,0);
  CHECK(stacked(1,2,3,4,5,6,7,value),value+1,"eight-argument call recovers and preserves high return half");
  CHECK(dc_goal_symbol_value(rt,after),1,"valid stack call executes continuation");
  CHECK(dynamic(entry(rt,"dc-call-guard-identity"),value),value+1,"valid dynamic function argument executes");
  CHECK(dc_goal_set_method(rt,method_type,9,old_method),1,"virtual method binding restored");
  CHECK(method(object),9,"valid virtual dispatch recovers after missing targets");
  CHECK(rt->used,used,"fault paths and recovered calls allocate no runtime memory");
  CHECK(rt->error,DC_GOAL_OK,"successful recovery leaves explicit clear status");
#undef CHECK
  return module.code_bytes;
}
