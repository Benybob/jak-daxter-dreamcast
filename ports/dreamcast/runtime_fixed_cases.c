/* Map explicitly supported desktop symbol-table role onto native s7 (AI-assisted), ISC. */
#include <stdint.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/fixed_fixture.h"
typedef uint64_t (*zero)(void);
typedef uint64_t (*one)(uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_fixed_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                unsigned *id,dc_goal_cache_sync sync) {
  static const uint64_t inputs[]={0,1,UINT64_MAX,INT64_MAX,UINT64_C(0x8000000000000000),
                                  UINT64_C(0x100000001),12345,(uint64_t)-98765};
  dc_goal_module_result module; unsigned i; zero read; one alias;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module_data(rt,goal_fixed_fixture_exports,GOAL_FIXED_FIXTURE_EXPORT_COUNT,
        GOAL_FIXED_FIXTURE_DATA,GOAL_FIXED_FIXTURE_DATA_COUNT,sync,NULL,&module),1,"explicit symbol-table role links");
  if (!module.functions) return 0;
  CHECK(module.functions,4,"four supported fixed-register functions");
  read=(zero)(uintptr_t)entry(rt,"dc-fixed-read"); alias=(one)(uintptr_t)entry(rt,"dc-fixed-alias");
  CHECK(read(),rt->s7,"frontend r14 role maps to native symbol pointer");
  CHECK(((zero)(uintptr_t)entry(rt,"dc-fixed-low"))(),rt->s7-32768u,"original fixed-register subtraction preserves layout expression");
  CHECK(((zero)(uintptr_t)entry(rt,"dc-fixed-high"))(),rt->s7+32768u,"original fixed-register addition preserves layout expression");
  for (i=0;i<8;++i) CHECK(alias(inputs[i]),2*(uint64_t)rt->s7+inputs[i],"identical fixed aliases and real ABI argument remain separate");
  CHECK(read(),rt->s7,"native runtime binding remains unchanged after calls");
  CHECK(rt->error,DC_GOAL_OK,"fixed role checks complete without runtime errors");
#undef CHECK
  return module.code_bytes;
}
