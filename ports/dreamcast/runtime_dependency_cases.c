/* Ordered compile context, late module binding and explicit startup (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/dependency_context.h"
#include "build/dependency_fixture.h"
typedef uint64_t (*one)(uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_dependency_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                     unsigned *id,dc_goal_cache_sync sync) {
  static const uint64_t values[]={0,1,UINT64_MAX,UINT64_C(0x100000001),INT64_MAX,(uint64_t)-98765};
  dc_goal_module_result main_module,dependency;
  uint32_t type,record; int32_t field; one compute,call,forward; unsigned i;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module_data(rt,goal_dependency_fixture_exports,GOAL_DEPENDENCY_FIXTURE_EXPORT_COUNT,
        GOAL_DEPENDENCY_FIXTURE_DATA,GOAL_DEPENDENCY_FIXTURE_DATA_COUNT,sync,NULL,&main_module),1,"main module links with declared but unloaded dependency");
  if (!main_module.functions) return 0;
  CHECK(main_module.functions,4,"only four main functions are exported from context compilation");
  CHECK(entry(rt,"dc-context-helper"),0,"frontend context does not implicitly load native dependency");
  CHECK(dc_goal_load_module_data(rt,goal_dependency_context_exports,GOAL_DEPENDENCY_CONTEXT_EXPORT_COUNT,
        GOAL_DEPENDENCY_CONTEXT_DATA,GOAL_DEPENDENCY_CONTEXT_DATA_COUNT,sync,NULL,&dependency),1,"native context module loads separately");
  if (!dependency.functions) return main_module.code_bytes;
  CHECK(dependency.functions,2,"dependency helper and explicit initializer exports");
  CHECK(entry(rt,"dc-context-seed"),0,"dependency linking does not execute startup");
  CHECK(dc_goal_run_initializer_void(rt,GOAL_DEPENDENCY_CONTEXT_INITIALIZER),1,"separate native dependency startup");
  CHECK(entry(rt,"dc-context-seed"),23,"dependency startup installs original global value");
  type=entry(rt,"dc-context-record");
  CHECK(type!=0,1,"dependency startup installs its declared record type");
  if (!type) return main_module.code_bytes+dependency.code_bytes;
  record=dc_goal_new_basic(rt,type,16);
  CHECK(record!=0,1,"record from context type allocation");
  if (!record) return main_module.code_bytes+dependency.code_bytes;
  field=123456; memcpy(dc_goal_resolve(rt,record,4),&field,4);
  CHECK(((one)(uintptr_t)entry(rt,"dc-context-field"))(record),123456,"main uses original context field layout");
  field=-987; memcpy(dc_goal_resolve(rt,record,4),&field,4);
  CHECK(((one)(uintptr_t)entry(rt,"dc-context-field"))(record),(uint64_t)-987,"context signed field keeps widening");
  compute=(one)(uintptr_t)entry(rt,"dc-context-compute"); call=(one)(uintptr_t)entry(rt,"dc-context-call");
  forward=(one)(uintptr_t)entry(rt,"dc-context-forward");
  memcpy(dc_goal_resolve(rt,record+4,4),&rt->basic_type,4);
  CHECK(forward(record),rt->basic_type,"forward-declared field reference uses original parent scalar ABI");
  for (i=0;i<6;++i) {
    CHECK(compute(values[i]),values[i]+17,"original source context constant expands in main function");
    CHECK(call(values[i]),values[i]+40,"main resolves a separately loaded dependency function");
  }
  CHECK(dc_goal_set_symbol(rt,dc_goal_find(rt,"dc-context-seed"),7),1,"mutate dependency global through its binding");
  CHECK(call(5),29,"main sees mutable state through late dependency call");
  CHECK(entry(rt,"dc-context-helper")!=0,1,"dependency binding installed independently of compiler preloads");
  CHECK(rt->error,DC_GOAL_OK,"dependency context checks complete without runtime errors");
#undef CHECK
  return main_module.code_bytes+dependency.code_bytes;
}
