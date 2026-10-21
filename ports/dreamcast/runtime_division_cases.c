/* Native division, original wrappers and explicit fault status (AI-assisted), ISC. */
#include <stdint.h>
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "division_vectors.h"
#include "build/division_fixture.h"
#include "build/original_division.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_division_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                   unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result bridge,fixture,original;
  const char *names[4]={"dc-div-signed","dc-div-signed-rem","dc-div-unsigned","dc-div-unsigned-rem"};
  two functions[4],source[3],mix,call;
  one same,seven,error_bridge;
  uint32_t used=rt->used,symbols=rt->symbol_count;
  unsigned i,j,kind;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module(rt,goal_division_fixture_exports,GOAL_DIVISION_FIXTURE_EXPORT_COUNT,sync,NULL,&fixture),0,"division module requires linked fault bridge");
  CHECK(rt->error,DC_GOAL_BAD_MODULE,"missing integer bridge is explicit link failure");
  CHECK(rt->used,used,"missing division bridge rolls back code allocation");
  CHECK(rt->symbol_count,symbols,"missing division bridge rolls back names");
  CHECK(dc_goal_find(rt,"dc-div-signed"),0,"failed division module publishes no callable code");
  if (!dc_goal_install_integer_bridge(rt,sync,&bridge)) {
    CHECK(rt->error,0,"integer bridge install"); return 0;
  }
  CHECK(bridge.functions,1,"one boxed integer fault bridge");
  if (!dc_goal_load_module(rt,goal_division_fixture_exports,GOAL_DIVISION_FIXTURE_EXPORT_COUNT,sync,NULL,&fixture)) {
    CHECK(rt->error,0,"division fixture load"); return 0;
  }
  CHECK(fixture.functions,8,"signed/unsigned division fixtures");
  if (!dc_goal_load_module(rt,goal_original_division_exports,GOAL_ORIGINAL_DIVISION_EXPORT_COUNT,sync,NULL,&original)) {
    CHECK(rt->error,0,"original division functions load"); return 0;
  }
  CHECK(original.functions,3,"unchanged original quotient/mod/rem exports");
  for (kind=0;kind<4;++kind) functions[kind]=(two)(uintptr_t)entry(rt,names[kind]);
  source[0]=(two)(uintptr_t)entry(rt,"/"); source[1]=(two)(uintptr_t)entry(rt,"mod"); source[2]=(two)(uintptr_t)entry(rt,"rem");
  mix=(two)(uintptr_t)entry(rt,"dc-div-mix"); call=(two)(uintptr_t)entry(rt,"dc-div-call");
  same=(one)(uintptr_t)entry(rt,"dc-div-same"); seven=(one)(uintptr_t)entry(rt,"dc-div-seven");
  error_bridge=(one)(uintptr_t)entry(rt,"dc-c-integer-error");
  for (i=0;i<DC_DIV_VALUE_COUNT;++i) {
    uint64_t a=dc_div_values[i];
    for (j=0;j<DC_DIV_VALUE_COUNT;++j) {
      uint64_t b=dc_div_values[j];
      for (kind=0;kind<4;++kind) if (dc_div_valid(kind,a,b))
        CHECK(functions[kind](a,b),dc_div_expected(kind,a,b),"native low-32 quotient/remainder exact bits");
      if (dc_div_valid(0,a,b)) {
        uint64_t q=dc_div_expected(0,a,b),r=dc_div_expected(1,a,b);
        CHECK(source[0](a,b),q,"original quotient function");
        CHECK(source[1](a,b),r,"original mod function signed remainder");
        CHECK(source[2](a,b),r,"original rem function signed remainder");
        CHECK(mix(a,b),q+r,"live result survives second native division");
        CHECK(call(a,b),q+(a^UINT64_C(0x1020304050607080)),"live 64-bit local survives linked division call");
      }
    }
    if ((uint32_t)a) CHECK(same(a),1,"same virtual input is dividend and divisor");
    CHECK(seven(a),dc_div_expected(0,a,7),"constant divisor through original typed IR");
  }
  for (kind=0;kind<4;++kind) for (i=0;i<2;++i) {
    uint64_t b=i?UINT64_C(0x100000000):0;
    rt->error=DC_GOAL_OK;
    CHECK(functions[kind](7,b),0,"divide-by-zero returns explicit bootstrap sentinel");
    CHECK(rt->error,DC_GOAL_DIVIDE_BY_ZERO,"divide-by-zero status uses low-32 divisor");
  }
  for (i=0;i<5;++i) {
    two function=i<2?functions[i]:source[i-2];
    rt->error=DC_GOAL_OK;
    CHECK(function(UINT64_C(0x1234567880000000),UINT64_C(0x00000000ffffffff)),0,"signed overflow returns explicit bootstrap sentinel");
    CHECK(rt->error,DC_GOAL_INTEGER_OVERFLOW,"signed overflow fault status");
  }
  rt->error=DC_GOAL_OK;
  CHECK(same(UINT64_C(0x100000000)),0,"aliased low-32 zero dividend/divisor fault");
  CHECK(rt->error,DC_GOAL_DIVIDE_BY_ZERO,"aliased zero operand fault status");
  CHECK(error_bridge(3),0,"unknown fault kind returns sentinel");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"unknown fault kind rejected");
  rt->error=DC_GOAL_OK;
  CHECK(functions[0](7,3),2,"division succeeds after caller clears fault status");
  CHECK(rt->error,DC_GOAL_OK,"valid division leaves error status clear");
#undef CHECK
  return bridge.code_bytes+fixture.code_bytes+original.code_bytes;
}
