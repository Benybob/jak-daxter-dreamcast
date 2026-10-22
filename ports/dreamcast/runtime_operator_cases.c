/* Distinct original GOAL arithmetic symbols in one native module (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/original_operators.h"
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef float (*one_float)(float);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static float as_float(uint32_t bits) { float result; memcpy(&result,&bits,4); return result; }
static uint32_t bits(float value) { uint32_t result; memcpy(&result,&value,4); return result; }
uint32_t dc_runtime_operator_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                   unsigned *id,dc_goal_cache_sync sync) {
  static const uint64_t pairs[8][2]={{0,1},{1,2},{INT32_MAX,7},{UINT64_C(0x100000001),3},
    {UINT64_MAX,2},{(uint64_t)-42,5},{UINT64_C(0x8000000000000000),7},{1234,(uint64_t)-13}};
  static const uint32_t inputs[]={0x3f800000u,0x40000000u,0xc0000000u,0x40800000u,0,0x80000000u,0x7f800000u,0xff800000u};
  /* Original compile_divide emits signed maximum finite for a zero divisor. */
  static const uint32_t expected[]={0x3f800000u,0x3f000000u,0xbf000000u,0x3e800000u,0x7f7fffffu,0xff7fffffu,0,0x80000000u};
  dc_goal_module_result module;
  two add,subtract,multiply,divide,mod,rem; one_float reciprocal;
  unsigned i; uint64_t a,b; int32_t x,y,product;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module_data(rt,goal_original_operators_exports,GOAL_ORIGINAL_OPERATORS_EXPORT_COUNT,
        GOAL_ORIGINAL_OPERATORS_DATA,GOAL_ORIGINAL_OPERATORS_DATA_COUNT,sync,NULL,&module),1,"colliding original symbol spellings link together");
  if (!module.functions) return 0;
  CHECK(module.functions,7,"seven distinct original arithmetic function exports");
  add=(two)(uintptr_t)entry(rt,"+"); subtract=(two)(uintptr_t)entry(rt,"-");
  multiply=(two)(uintptr_t)entry(rt,"*"); divide=(two)(uintptr_t)entry(rt,"/");
  mod=(two)(uintptr_t)entry(rt,"mod"); rem=(two)(uintptr_t)entry(rt,"rem");
  reciprocal=(one_float)(uintptr_t)entry(rt,"1/");
  for (i=0;i<8;++i) {
    a=pairs[i][0]; b=pairs[i][1]; x=(int32_t)(uint32_t)a; y=(int32_t)(uint32_t)b;
    product=(int32_t)(uint32_t)((uint64_t)(uint32_t)a*(uint32_t)b);
    CHECK(add(a,b),a+b,"original plus retains 64-bit wrap semantics");
    CHECK(subtract(a,b),a-b,"original minus retains 64-bit wrap semantics");
    CHECK(multiply(a,b),(uint64_t)(int64_t)product,"original multiply uses signed low-32 result");
    CHECK(divide(a,b),(uint64_t)(int64_t)(x/y),"original slash uses signed low-32 division");
    CHECK(mod(a,b),(uint64_t)(int64_t)(x%y),"original mod uses signed low-32 remainder");
    CHECK(rem(a,b),(uint64_t)(int64_t)(x%y),"original rem retains separate symbol binding");
  }
  for (i=0;i<8;++i) CHECK(bits(reciprocal(as_float(inputs[i]))),expected[i],"original leading-digit reciprocal symbol executes");
  CHECK(rt->error,DC_GOAL_OK,"native source names retain distinct bindings without runtime errors");
#undef CHECK
  return module.code_bytes;
}
