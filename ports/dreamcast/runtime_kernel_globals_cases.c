/* Original contiguous kernel system-global startup prefix (AI-assisted), ISC. */
#include <stdint.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/original_kernel_globals.h"
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_kernel_globals_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                         unsigned *id,dc_goal_cache_sync sync) {
  static const char *const names[]={"*use-old-listener-print*","*kernel-version*","*irx-version*",
    "*kernel-boot-mode*","*kernel-boot-level*","*deci-count*","*last-loado-length*",
    "*last-loado-global-usage*","*last-loado-debug-usage*"};
  dc_goal_module_result module;
  uint64_t value=UINT64_MAX; uint32_t expected[9],i;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module_data(rt,goal_original_kernel_globals_exports,GOAL_ORIGINAL_KERNEL_GLOBALS_EXPORT_COUNT,
        GOAL_ORIGINAL_KERNEL_GLOBALS_DATA,GOAL_ORIGINAL_KERNEL_GLOBALS_DATA_COUNT,sync,NULL,&module),
        1,"original kernel global prefix links without startup");
  if(!module.functions) return 0;
  CHECK(module.functions,1,"original prefix exports only explicit initializer");
  for(i=0;i<9;++i) CHECK(entry(rt,names[i]),0,"linking leaves original kernel globals uninitialized");
  CHECK(dc_goal_find(rt,"*kernel-context*"),0,"prefix excludes later original kernel context");
  CHECK(dc_goal_find(rt,"*dram-stack*"),0,"prefix excludes process stack allocation");
  CHECK(dc_goal_find(rt,"*fake-scratchpad-data*"),0,"prefix excludes desktop scratchpad setup");
  CHECK(dc_goal_find(rt,"*kernel-packages*"),0,"prefix ends before package-list initialization");
  CHECK(dc_goal_run_initializer(rt,GOAL_ORIGINAL_KERNEL_GLOBALS_INITIALIZER,&value),1,
        "explicit native original kernel system-global startup");
  CHECK(value,0,"original final zero global is initializer result");
  expected[0]=rt->s7; expected[1]=((2u<<16)|0u)<<3; expected[2]=((1u<<16)|2u)<<3;
  expected[3]=dc_goal_find(rt,"listener"); expected[4]=rt->s7;
  for(i=5;i<9;++i) expected[i]=0;
  CHECK(expected[3]!=0,1,"original listener mode symbol linked");
  for(i=0;i<9;++i) CHECK(entry(rt,names[i]),expected[i],"original kernel global initialized to its original value");
  CHECK(dc_goal_set_symbol(rt,dc_goal_find(rt,"*deci-count*"),99),1,"kernel counter can be updated after initialization");
  CHECK(entry(rt,"*deci-count*"),99,"updated kernel counter visible");
  CHECK(dc_goal_run_initializer(rt,GOAL_ORIGINAL_KERNEL_GLOBALS_INITIALIZER,&value),1,
        "original prefix initialization explicitly repeats");
  CHECK(entry(rt,"*deci-count*"),0,"original initialization resets kernel counter");
  CHECK(rt->error,DC_GOAL_OK,"original kernel system-global startup leaves runtime clear");
#undef CHECK
  return module.code_bytes;
}
