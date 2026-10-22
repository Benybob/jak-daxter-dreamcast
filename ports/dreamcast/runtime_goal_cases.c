/* Execute compiled GOAL against native runtime objects (AI-assisted), ISC. */
#include <kos.h>
#include <arch/cache.h>
#include <stdint.h>
#include <stdio.h>
#include "goal_module.h"
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "kernel_type_cases.h"
#include "build/runtime_fixture.h"
#include "build/initializer_fixture.h"
#include "build/runtime_identity.h"
#define goal_identity goal_identity_baseline
#include "build/runtime_identity_baseline.h"
#undef goal_identity

typedef uint64_t (*goal_zero)(void);
typedef uint64_t (*goal_one)(uint64_t);
uint32_t dc_runtime_memory_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_static_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_float_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_bridge_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_quoted_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_void_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_compiled_type_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_shift_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_division_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_native_file_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_locals_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_early_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_sort_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_operator_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_fixed_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_far_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_dependency_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_mixed_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_validation_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
uint32_t dc_runtime_kernel_globals_cases(dc_goal_runtime *,dc_runtime_check,void *,unsigned *,dc_goal_cache_sync);
static void sync_code(uint32_t allocation,uint32_t bytes,void *context) {
  (void)context;
  dcache_flush_range(allocation,bytes);
  icache_flush_range(allocation,bytes);
}
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_goal_cases(void *memory,uint32_t capacity,dc_runtime_check check,
                                void *context,char *description,size_t description_bytes) {
  dc_goal_runtime rt;
  dc_goal_module_result fixture,identity,baseline,initialization;
  uint32_t read,write,call_read,counter,optimized,string,pair,code_bytes;
  uint64_t values[7];
  unsigned id=81,i;
#define CHECK(actual,expected,label) check((uint64_t)(actual),(uint64_t)(expected),++id,label,context)
  if (!dc_goal_init_native(&rt,memory,capacity)) {
    CHECK(0,1,"compiled runtime initialization"); return 0;
  }
  CHECK(rt.base&0xe0000000u,0,"native GOAL uses low cached P0 addresses");
  if (!dc_goal_load_module(&rt,goal_runtime_fixture_exports,GOAL_RUNTIME_FIXTURE_EXPORT_COUNT,
                          sync_code,NULL,&fixture)) {
    CHECK(rt.error,0,"compiled fixture module load"); return 0;
  }
  CHECK(fixture.functions,8,"runtime function exports");
  CHECK(fixture.temporary_bytes,262144,"module temporary symbol snapshot");
  if (!dc_goal_load_module(&rt,goal_runtime_identity_exports,GOAL_RUNTIME_IDENTITY_EXPORT_COUNT,
                          sync_code,NULL,&identity)) {
    CHECK(rt.error,0,"original identity load"); return 0;
  }
  optimized=entry(&rt,"identity");
  CHECK(sizeof(goal_identity),6,"original identity code density");
  CHECK(identity.functions,2,"original identity plus top-level function");
  CHECK(dc_goal_type_of(&rt,optimized),rt.function_type,"native code function tag");
  dc_goal_set_symbol(&rt,dc_goal_find(&rt,"identity"),entry(&rt,"dc-runtime-constant"));
  CHECK(dc_goal_run_initializer(&rt,GOAL_RUNTIME_IDENTITY_INITIALIZER,&values[0]),1,"original definition startup executes");
  CHECK(values[0],optimized,"original definition returns its native function object");
  CHECK(entry(&rt,"identity"),optimized,"original definition installs its function binding");
  read=entry(&rt,"dc-runtime-read"); write=entry(&rt,"dc-runtime-write");
  call_read=entry(&rt,"dc-runtime-call-read"); counter=dc_goal_find(&rt,"dc-runtime-counter");
  CHECK(((goal_zero)(uintptr_t)read)(),0,"zero initialized symbol cell");
  dc_goal_set_symbol(&rt,counter,17);
  CHECK(((goal_zero)(uintptr_t)read)(),17,"compiled read sees C mutation");
  CHECK(((goal_one)(uintptr_t)write)((uint64_t)-3),(uint64_t)-3,"compiled write result");
  CHECK(dc_goal_symbol_value(&rt,counter),0xfffffffdu,"compiled write updates native cell");
  CHECK(((goal_zero)(uintptr_t)read)(),(uint64_t)-3,"signed 32-bit symbol load");
  CHECK(((goal_one)(uintptr_t)write)(UINT64_C(0x100000001)),UINT64_C(0x100000001),"write preserves full return value");
  CHECK(((goal_zero)(uintptr_t)read)(),1,"symbol cell retains low 32 bits");
  CHECK(((goal_zero)(uintptr_t)entry(&rt,"dc-runtime-false"))(),rt.s7,"compiled false pointer");
  CHECK(((goal_zero)(uintptr_t)entry(&rt,"dc-runtime-true"))(),rt.s7+8u,"compiled true pointer");
  CHECK(((goal_zero)(uintptr_t)entry(&rt,"dc-runtime-empty"))(),rt.s7-10u,"compiled empty pair pointer");
  CHECK(((goal_one)(uintptr_t)entry(&rt,"dc-runtime-not"))(rt.s7),rt.s7+8u,"not false");
  CHECK(((goal_one)(uintptr_t)entry(&rt,"dc-runtime-not"))(0),rt.s7,"integer zero is truthy");
  CHECK(((goal_one)(uintptr_t)entry(&rt,"dc-runtime-not"))(rt.s7+8u),rt.s7,"not true");
  CHECK(((goal_one)(uintptr_t)entry(&rt,"dc-runtime-not"))(rt.s7-10u),rt.s7,"empty pair is truthy");
  CHECK(((goal_zero)(uintptr_t)call_read)(),1,"native call via live function cell");
  dc_goal_set_symbol(&rt,dc_goal_find(&rt,"dc-runtime-read"),entry(&rt,"dc-runtime-constant"));
  CHECK(((goal_zero)(uintptr_t)call_read)(),99,"native call observes function rebinding");
  dc_goal_set_symbol(&rt,dc_goal_find(&rt,"dc-runtime-read"),read);
  dc_goal_set_symbol(&rt,counter,57);
  CHECK(((goal_zero)(uintptr_t)call_read)(),57,"native call after restoring function cell");
  string=dc_goal_string(&rt,"native SH-4 GOAL");
  pair=dc_goal_new_pair(&rt,string,rt.s7-10u);
  values[0]=rt.s7; values[1]=rt.s7+8u; values[2]=rt.s7-10u;
  values[3]=0x12345678u; values[4]=string; values[5]=pair;
  values[6]=UINT64_C(0x12345678abcdef01);
  for (i=0;i<7;++i) CHECK(((goal_one)(uintptr_t)optimized)(values[i]),values[i],"original optimized identity preserves value");
  if (!dc_goal_load_module(&rt,goal_runtime_identity_baseline_exports,
                          GOAL_RUNTIME_IDENTITY_BASELINE_EXPORT_COUNT,sync_code,NULL,&baseline)) {
    CHECK(rt.error,0,"baseline original identity load"); return 0;
  }
  CHECK(baseline.code_bytes,72,"original identity unoptimized size");
  for (i=0;i<7;++i) CHECK(((goal_one)(uintptr_t)entry(&rt,"identity"))(values[i]),
                         ((goal_one)(uintptr_t)optimized)(values[i]),"original baseline and optimized identity agree");
  code_bytes=fixture.code_bytes+identity.code_bytes+baseline.code_bytes;
  dc_goal_set_symbol(&rt,dc_goal_intern(&rt,"dc-init-external"),9);
  if (!dc_goal_load_module(&rt,goal_initializer_fixture_exports,GOAL_INITIALIZER_FIXTURE_EXPORT_COUNT,
                          sync_code,NULL,&initialization)) {
    CHECK(rt.error,0,"initializer module load"); return 0;
  }
  CHECK(initialization.functions,5,"initializer and four original frontend functions loaded");
  CHECK(entry(&rt,"dc-init-value"),0,"linking does not implicitly run initialization");
  CHECK(entry(&rt,"dc-init-external"),9,"linking leaves external global unchanged");
  read=entry(&rt,"dc-init-read");
  dc_goal_set_symbol(&rt,dc_goal_find(&rt,"dc-init-read"),entry(&rt,"dc-runtime-constant"));
  values[0]=UINT64_C(0xfeedfeedfeedfeed);
  CHECK(dc_goal_run_initializer(&rt,GOAL_INITIALIZER_FIXTURE_INITIALIZER,&values[0]),1,"native top-level execution");
  CHECK(values[0],rt.s7,"original top-level return value");
  CHECK(entry(&rt,"dc-init-value"),42,"nonzero initializer and startup call");
  CHECK(entry(&rt,"dc-init-truth"),rt.s7+8u,"tagged global initializer");
  CHECK(entry(&rt,"dc-init-runs"),1,"startup expression execution");
  CHECK(entry(&rt,"dc-init-external"),10,"startup mutates existing global");
  CHECK(entry(&rt,"dc-init-via-call"),42,"initializer observes called function result");
  CHECK(entry(&rt,"dc-init-read"),read,"top-level restores original direct function address");
  CHECK(entry(&rt,"dc-init-alias"),read,"initialized function alias");
  CHECK(((goal_zero)(uintptr_t)entry(&rt,"dc-init-alias-value"))(),read,"compiled typed function value return");
  CHECK(((goal_zero)(uintptr_t)entry(&rt,"dc-init-alias"))(),42,"initialized alias executes native code");
  CHECK(((goal_zero)(uintptr_t)entry(&rt,"dc-init-result"))(),84,"initialized globals used by compiled code");
  values[0]=UINT64_C(0xfeedfeedfeedfeed);
  CHECK(dc_goal_run_initializer(&rt,"dc-init-missing",&values[0]),0,"missing initializer is rejected");
  CHECK(values[0],UINT64_C(0xfeedfeedfeedfeed),"failed initializer leaves output unchanged");
  CHECK(dc_goal_run_initializer(&rt,"#f",&values[0]),0,"non-function initializer rejected");
  code_bytes+=initialization.code_bytes;
  code_bytes+=dc_runtime_memory_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_static_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_float_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_bridge_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_quoted_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_void_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_compiled_type_cases(&rt,check,context,&id,sync_code);
  dc_kernel_type_cases(&rt,check,context,&id);
  code_bytes+=dc_runtime_shift_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_division_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_native_file_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_locals_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_early_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_sort_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_operator_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_fixed_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_far_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_dependency_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_mixed_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_validation_cases(&rt,check,context,&id,sync_code);
  code_bytes+=dc_runtime_kernel_globals_cases(&rt,check,context,&id,sync_code);
  dc_goal_clear_core_bridges(&rt);
  snprintf(description,description_bytes,"GOAL=%lu init/lists=pass arena=%lu symbols=%lu",
           (unsigned long)code_bytes,(unsigned long)rt.used,(unsigned long)rt.symbol_count);
#undef CHECK
  return code_bytes;
}
