/* Native no-result GOAL calls/startup (AI-assisted), ISC. */
#include <stdint.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/void_fixture.h"
#include "build/void_empty.h"
typedef void (*goal_void_zero)(void);
typedef void (*goal_void_one)(uint64_t);
typedef void (*goal_void_two)(uint64_t,uint64_t);
typedef uint64_t (*goal_zero)(void);
typedef uint64_t (*goal_one)(uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_void_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                              unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result module;
  uint32_t writer,code_bytes;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  if (!dc_goal_load_module(rt,goal_void_fixture_exports,GOAL_VOID_FIXTURE_EXPORT_COUNT,sync,NULL,&module)) {
    CHECK(rt->error,0,"void module load"); return 0;
  }
  CHECK(module.functions,9,"void functions and startup exported");
  code_bytes=module.code_bytes;
  CHECK(entry(rt,"dc-void-state"),0,"void linking defers startup");
  writer=entry(rt,"dc-void-write");
  CHECK(dc_goal_run_initializer_void(rt,GOAL_VOID_FIXTURE_INITIALIZER),1,"no-result top-level executes");
  CHECK(entry(rt,"dc-void-state"),73,"no-result nested startup call side effect");
  ((goal_void_zero)(uintptr_t)entry(rt,"dc-void-empty"))();
  CHECK(entry(rt,"dc-void-state"),73,"empty void body returns normally");
  ((goal_void_one)(uintptr_t)writer)((uint64_t)-5);
  CHECK(entry(rt,"dc-void-state"),0xfffffffbu,"void C-to-GOAL argument and side effect");
  CHECK(((goal_zero)(uintptr_t)entry(rt,"dc-void-read"))(),(uint64_t)-5,"void side effect visible to compiled reader");
  ((goal_void_zero)(uintptr_t)entry(rt,"dc-void-clear"))();
  CHECK(entry(rt,"dc-void-state"),0,"void zero-argument function");
  ((goal_void_one)(uintptr_t)entry(rt,"dc-void-call"))(12);
  CHECK(entry(rt,"dc-void-state"),12,"compiled void caller passes argument");
  ((goal_void_one)(uintptr_t)entry(rt,"dc-void-call"))((uint64_t)-51);
  CHECK(entry(rt,"dc-void-state"),0xffffffcdu,"nested void call signed argument");
  CHECK(((goal_one)(uintptr_t)entry(rt,"dc-void-call-read"))(5),20,"live local survives void call");
  CHECK(entry(rt,"dc-void-state"),8,"value-returning caller observes void effect");
  ((goal_void_two)(uintptr_t)entry(rt,"dc-void-dispatch"))(writer,(uint64_t)-9);
  CHECK(entry(rt,"dc-void-state"),0xfffffff7u,"typed indirect void callback");
  ((goal_void_two)(uintptr_t)entry(rt,"dc-void-dispatch"))(entry(rt,"dc-void-add"),3);
  CHECK(entry(rt,"dc-void-state"),0xfffffffau,"indirect void callback selects second body");
  dc_goal_set_symbol(rt,dc_goal_find(rt,"dc-void-write"),entry(rt,"dc-void-add"));
  ((goal_void_one)(uintptr_t)entry(rt,"dc-void-call"))(2);
  CHECK(entry(rt,"dc-void-state"),0xfffffffcu,"void call observes mutable function binding");
  CHECK(((goal_one)(uintptr_t)entry(rt,"dc-void-call-read"))(5),16,"live value and rebound void call");
  CHECK(entry(rt,"dc-void-state"),4,"rebound void addition side effect");
  CHECK(dc_goal_run_initializer_void(rt,GOAL_VOID_FIXTURE_INITIALIZER),1,"void startup can be explicitly rerun");
  CHECK(entry(rt,"dc-void-state"),73,"void startup retains original source order");
  CHECK(entry(rt,"dc-void-write"),writer,"void startup reinstalls original body");
  CHECK(dc_goal_run_initializer_void(rt,"dc-void-missing"),0,"missing void entry rejected");
  CHECK(dc_goal_run_initializer_void(rt,"#f"),0,"non-function void entry rejected");
  CHECK(dc_goal_run_initializer_void(rt,NULL),0,"null void name rejected");
  CHECK(dc_goal_run_initializer_void(NULL,"dc-void-empty"),0,"null void runtime rejected");
  if (!dc_goal_load_module(rt,goal_void_empty_exports,GOAL_VOID_EMPTY_EXPORT_COUNT,sync,NULL,&module)) {
    CHECK(rt->error,0,"empty initializer module load"); return 0;
  }
  CHECK(module.functions,1,"initializer-only module exports one entry");
  CHECK(module.code_bytes,4,"empty original startup uses RTS and delay slot");
  writer=rt->used;
  CHECK(dc_goal_run_initializer_void(rt,GOAL_VOID_EMPTY_INITIALIZER),1,"empty original top-level executes");
  CHECK(entry(rt,"dc-void-state"),73,"empty startup preserves existing state");
  CHECK(rt->used,writer,"empty startup has no allocation side effect");
#undef CHECK
  return module.code_bytes+code_bytes;
}
