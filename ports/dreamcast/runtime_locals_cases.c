/* Execute genuine named-local frontend output with the SH-4 ABI (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/locals_fixture.h"
typedef uint64_t (*zero)(void);
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef uint64_t (*eight)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef float (*floats)(float,float);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t float_bits(float value) { uint32_t bits; memcpy(&bits,&value,4); return bits; }
uint32_t dc_runtime_locals_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                 unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result module;
  const uint64_t values[]={0,1,UINT64_MAX,123456,(uint64_t)-98765,UINT64_C(0x100000001)};
  uint32_t type,record; int32_t field; unsigned i;
  one basic,branch,score; two mutate; eight stack; floats floating;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module_data(rt,goal_locals_fixture_exports,GOAL_LOCALS_FIXTURE_EXPORT_COUNT,
        GOAL_LOCALS_FIXTURE_DATA,GOAL_LOCALS_FIXTURE_DATA_COUNT,sync,NULL,&module),1,
        "named-local functions link without invented arguments");
  if (!module.functions) return 0;
  CHECK(module.functions,7,"six named-local scalar functions and one method");
  CHECK(((zero)(uintptr_t)entry(rt,"dc-local-zero"))(),73,"zero arguments with one named local");
  basic=(one)(uintptr_t)entry(rt,"dc-local-basic"); branch=(one)(uintptr_t)entry(rt,"dc-local-branch");
  mutate=(two)(uintptr_t)entry(rt,"dc-local-mutate"); stack=(eight)(uintptr_t)entry(rt,"dc-local-eight");
  floating=(floats)(uintptr_t)entry(rt,"dc-local-float");
  for (i=0;i<sizeof(values)/sizeof(values[0]);++i) {
    CHECK(basic(values[i]),values[i]+1,"single incoming argument and assigned local");
    CHECK(mutate(values[i],values[(i+1)%6]),values[i]*2,"parameter mutation preserves separate named locals");
    CHECK(branch(values[i]),(int64_t)values[i]<0?values[i]-10:values[i]+10,"local assigned on both branch paths");
  }
  CHECK(stack(1,2,3,4,5,6,7,8),(uint64_t)-16,"eight original arguments keep GCC stack positions");
  CHECK(stack(UINT64_C(0x100000001),2,3,4,5,6,7,UINT64_C(0x200000008)),
        UINT64_C(0x100000001)+2+3+4-5-6-7-UINT64_C(0x200000008),"named locals preserve 64-bit stack arguments");
  CHECK(float_bits(floating(1.25f,2.5f)),float_bits(7.5f),"float named local follows verified FR argument ABI");
  CHECK(float_bits(floating(-4.0f,1.0f)),float_bits(-6.0f),"negative float local computation");
  CHECK(float_bits(floating(0.0f,0.0f)),float_bits(0.0f),"zero float local computation");
  type=dc_goal_define_type(rt,"dc-local-record",rt->basic_type,8,0,12);
  CHECK(type!=0,1,"synthetic named-local method owner type");
  if (!type) return module.code_bytes;
  record=dc_goal_new_basic(rt,type,8);
  CHECK(record!=0,1,"synthetic named-local method instance");
  if (!record) return module.code_bytes;
  score=(one)(uintptr_t)entry(rt,"(method score dc-local-record)");
  field=-37; memcpy(dc_goal_resolve(rt,record,4),&field,4);
  CHECK(score(record),(uint64_t)-24,"compiled method receives this separately from scratch local");
  field=INT32_MAX-1; memcpy(dc_goal_resolve(rt,record,4),&field,4);
  CHECK(score(record),UINT64_C(2147483659),"method local retains widened integer result");
  CHECK(dc_goal_type_of(rt,record),type,"method leaves native object header intact");
  CHECK(rt->error,DC_GOAL_OK,"named-local diagnostic completes without runtime errors");
#undef CHECK
  return module.code_bytes;
}
