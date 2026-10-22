/* Native long-branch register/stack/flag and relocation checks (AI-assisted), ISC. */
#include <stdint.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/far_fixture.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef uint64_t (*zero)(void);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_far_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                              unsigned *id,dc_goal_cache_sync sync) {
  static const uint64_t values[]={0,1,UINT64_MAX,UINT64_C(0x8000000000000000),
                                  UINT64_C(0x12345678abcdef01),UINT64_C(0x100000001)};
  dc_goal_module_result module; one forward,backward,flag0,flag1; two conditional; unsigned i;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module(rt,goal_far_fixture_exports,GOAL_FAR_FIXTURE_EXPORT_COUNT,sync,NULL,&module),1,"native far-branch probes link after address rebasing");
  if (!module.functions) return 0;
  CHECK(module.functions,6,"six far-branch ABI and control-flow probes");
  forward=(one)(uintptr_t)entry(rt,"dc-far-forward"); backward=(one)(uintptr_t)entry(rt,"dc-far-backward");
  flag0=(one)(uintptr_t)entry(rt,"dc-far-flag0"); flag1=(one)(uintptr_t)entry(rt,"dc-far-flag1");
  conditional=(two)(uintptr_t)entry(rt,"dc-far-conditional");
  for (i=0;i<6;++i) {
    CHECK(forward(values[i]),values[i],"far forward jump preserves both return halves and stack");
    CHECK(backward(values[i]),values[i],"far backward jump preserves both return halves and stack");
    CHECK(conditional(0,values[i]),values[i],"taken far conditional preserves native argument/result");
    CHECK(conditional(UINT64_MAX,values[i]),values[i]+1,"untaken far conditional continues with carry arithmetic");
    CHECK(flag0(values[i]),values[i]&UINT32_MAX,"far branch preserves clear T flag");
    CHECK(flag1(values[i]),(values[i]&UINT32_MAX)|UINT64_C(0x100000000),"far branch preserves set T flag");
  }
  CHECK(((zero)(uintptr_t)entry(rt,"dc-far-literal"))(),rt->s7+8u,"far branch and intermediate symbol relocation share layout");
  CHECK(rt->error,DC_GOAL_OK,"far-branch probes complete with runtime state intact");
#undef CHECK
  return module.code_bytes;
}
