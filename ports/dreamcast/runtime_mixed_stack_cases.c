/* Native fixed mixed integer-stack and float ABI probes (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/mixed_stack_fixture.h"
static const uint64_t ints[]={0,1,UINT64_MAX,UINT64_C(0x12345678abcdef01),
  UINT64_C(0x8000000000000000),INT64_MAX,UINT64_C(0x100000000),UINT64_C(0xfedcba9876543210)};
static const uint32_t fps[]={0,0x80000000u,0x3fc00000u,0xc0100000u,0x7f800000u,0xff800000u,0x7fc12345u,0xffc12345u};
static float as_float(uint32_t value) { float result; memcpy(&result,&value,4); return result; }
static uint32_t bits(float value) { uint32_t result; memcpy(&result,&value,4); return result; }
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t stack_pointer(void) {
  uint32_t result;
  __asm__ __volatile__("mov r15,%0":"=r"(result));
  return result;
}
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
#include "build/mixed_stack_cases.h"
uint32_t dc_runtime_mixed_stack_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                     unsigned *id,dc_goal_cache_sync sync) {
  typedef uint64_t (*live_fn)(float,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
  typedef uint64_t (*repeat_fn)(uint64_t,float,uint64_t,uint64_t);
  static const unsigned counts[]={0,1,2,3,17,32};
  dc_goal_module_result module; live_fn live; repeat_fn repeat;
  unsigned v,i; uint64_t expected;
  CHECK(dc_goal_load_module(rt,goal_mixed_stack_fixture_exports,GOAL_MIXED_STACK_FIXTURE_EXPORT_COUNT,sync,NULL,&module),1,
        "mixed stack fixtures link");
  if(!module.functions) return 0;
  CHECK(module.functions,226,"mixed fixed stack entry and call probes");
  mixed_stack_probes(rt,check,context,id);
  live=(live_fn)(uintptr_t)entry(rt,"dc-ms-live");
  for(v=0;v<8;++v) {
    expected=ints[(v+7)%8];
    for(i=1;i<8;++i) expected+=ints[(v+i)%8];
    CHECK(live(as_float(fps[v]),ints[(v+1)%8],ints[(v+2)%8],ints[(v+3)%8],ints[(v+4)%8],
               ints[(v+5)%8],ints[(v+6)%8],ints[(v+7)%8]),expected,"live 64-bit values survive mixed stack call");
  }
  repeat=(repeat_fn)(uintptr_t)entry(rt,"dc-ms-repeat");
  for(i=0;i<6;++i) {
    uint64_t n=counts[i];
    expected=n*UINT64_C(0x300000004)+(n?n*(n-1)/2:0);
    CHECK(repeat(n,as_float(0x7fc12345u),UINT64_C(0x100000001),UINT64_C(0x200000003)),expected,
          "repeated mixed stack calls preserve locals and stack cleanup");
  }
  CHECK(rt->error,DC_GOAL_OK,"mixed stack ABI leaves runtime clear");
  return module.code_bytes;
}
#undef CHECK
