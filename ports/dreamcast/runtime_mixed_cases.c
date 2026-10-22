/* Fixed-signature mixed integer/float register calls through GCC ABI (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/mixed_fixture.h"
typedef uint64_t (*mixed_int)(float,uint64_t,float,uint64_t);
typedef float (*mixed_float)(float,uint64_t,float,uint64_t);
typedef uint64_t (*eight_int)(float,uint64_t,float,float,uint64_t,float,float,float);
typedef float (*eight_float)(float,uint64_t,float,float,uint64_t,float,float,float);
static uint32_t entry(dc_goal_runtime *rt,const char *name) { return dc_goal_symbol_value(rt,dc_goal_find(rt,name)); }
static uint32_t bits(float value) { uint32_t result; memcpy(&result,&value,4); return result; }
uint32_t dc_runtime_mixed_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                unsigned *id,dc_goal_cache_sync sync) {
  static const uint64_t values[]={0,1,UINT64_MAX,UINT64_C(0x12345678abcdef01),INT64_MAX,UINT64_C(0x8000000000000000)};
  dc_goal_module_result module; unsigned i; uint64_t a,b;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
#define INT(name) ((mixed_int)(uintptr_t)entry(rt,"dc-mixed-" name))(1.25f,a,-2.5f,b)
#define FLOAT(name) ((mixed_float)(uintptr_t)entry(rt,"dc-mixed-" name))(1.25f,a,-2.5f,b)
  CHECK(dc_goal_load_module_data(rt,goal_mixed_fixture_exports,GOAL_MIXED_FIXTURE_EXPORT_COUNT,
        GOAL_MIXED_FIXTURE_DATA,GOAL_MIXED_FIXTURE_DATA_COUNT,sync,NULL,&module),1,"mixed fixed-signature module links");
  if (!module.functions) return 0;
  CHECK(module.functions,10,"ten mixed native entry and call functions");
  for (i=0;i<6;++i) {
    a=values[i]; b=values[(i+1)%6];
    CHECK(INT("left"),a,"first mixed integer pair preserves both halves");
    CHECK(INT("right"),b,"second mixed integer pair ignores float positions");
    CHECK(bits(FLOAT("f0")),bits(1.25f),"first mixed float uses independent FR argument stream");
    CHECK(bits(FLOAT("f1")),bits(-2.5f),"second mixed float uses independent FR argument stream");
    CHECK(INT("sum"),a+b-1,"mixed entry arithmetic and float conversions");
    CHECK(bits(FLOAT("floats")),bits(-1.25f),"mixed float result and arithmetic");
    CHECK(INT("call"),2*a+b-1,"native GOAL mixed call retains live integer across call");
    CHECK(bits(FLOAT("float-call")),bits(-0.75f),"native GOAL mixed call retains float result");
    CHECK(bits(((eight_float)(uintptr_t)entry(rt,"dc-mixed-eight-last"))(1.0f,a,2.0f,3.0f,b,4.0f,5.0f,7.25f)),
          bits(7.25f),"eight mixed arguments select the sixth float register");
    CHECK(((eight_int)(uintptr_t)entry(rt,"dc-mixed-eight-int"))(1.0f,a,2.0f,3.0f,b,4.0f,5.0f,7.25f),
          b,"eight mixed arguments select the second integer pair");
  }
  CHECK(rt->error,DC_GOAL_OK,"mixed register ABI completes without runtime errors");
#undef FLOAT
#undef INT
#undef CHECK
  return module.code_bytes;
}
