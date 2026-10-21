/* Exhaustive count boundaries for original integer shift semantics (AI-assisted), ISC. */
#include <stdint.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "shift_vectors.h"
#include "build/shift_fixture.h"
#include "build/original_shift.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
int dc_load_shift_baseline(dc_goal_runtime *,dc_goal_cache_sync,dc_goal_module_result *);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_shift_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result fixture,original,baseline;
  const char *names[3]={"dc-shift-left","dc-shift-logical","dc-shift-arithmetic"};
  const char *constant_names[3][5]={
    {"dc-shift-left-0","dc-shift-left-1","dc-shift-left-31","dc-shift-left-32","dc-shift-left-63"},
    {"dc-shift-logical-0","dc-shift-logical-1","dc-shift-logical-31","dc-shift-logical-32","dc-shift-logical-63"},
    {"dc-shift-arithmetic-0","dc-shift-arithmetic-1","dc-shift-arithmetic-31","dc-shift-arithmetic-32","dc-shift-arithmetic-63"}};
  const unsigned constants[5]={0,1,31,32,63};
  two functions[3],ash,mix;
  one self,constant_functions[3][5];
  unsigned i,j,kind;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  if (!dc_goal_load_module(rt,goal_shift_fixture_exports,GOAL_SHIFT_FIXTURE_EXPORT_COUNT,sync,NULL,&fixture)) {
    CHECK(rt->error,0,"shift fixture load"); return 0;
  }
  if (!dc_goal_load_module(rt,goal_original_shift_exports,GOAL_ORIGINAL_SHIFT_EXPORT_COUNT,sync,NULL,&original)) {
    CHECK(rt->error,0,"original ash load"); return 0;
  }
  CHECK(fixture.functions,20,"dynamic/constant shifts and call fixture exports");
  CHECK(original.functions,1,"unchanged original ash export");
  ash=(two)(uintptr_t)entry(rt,"ash"); mix=(two)(uintptr_t)entry(rt,"dc-shift-mix");
  self=(one)(uintptr_t)entry(rt,"dc-shift-self");
  for (kind=0;kind<3;++kind) {
    functions[kind]=(two)(uintptr_t)entry(rt,names[kind]);
    for (j=0;j<5;++j) constant_functions[kind][j]=(one)(uintptr_t)entry(rt,constant_names[kind][j]);
  }
  for (i=0;i<DC_SHIFT_VALUE_COUNT;++i) {
    uint64_t value=dc_shift_values[i];
    for (j=0;j<DC_SHIFT_COUNT_COUNT;++j) {
      uint64_t amount=dc_shift_count(j),expected;
      for (kind=0;kind<3;++kind)
        CHECK(functions[kind](value,amount),dc_shift_expected(kind,value,amount),"native shift halves/count mask");
      expected=amount && !(amount>>63)?dc_shift_expected(0,value,amount):dc_shift_expected(2,value,0-amount);
      CHECK(ash(value,amount),expected,"original ash signed direction and six-bit count");
      expected=dc_shift_expected(0,value,amount)^dc_shift_expected(2,value^UINT64_C(0x1020304050607080),amount);
      CHECK(mix(value,amount),expected,"live scalar preserved across linked shift calls");
    }
    CHECK(self(value),dc_shift_expected(0,value,value),"same virtual value supplies shift operand and count");
    for (kind=0;kind<3;++kind) for (j=0;j<5;++j)
      CHECK(constant_functions[kind][j](value),dc_shift_expected(kind,value,constants[j]),"constant shift specialization boundaries");
  }
  if (!dc_load_shift_baseline(rt,sync,&baseline)) {
    CHECK(rt->error,0,"shift baseline module load"); return 0;
  }
  CHECK(baseline.functions,20,"matching non-optimized shift fixture exports");
  CHECK(baseline.code_bytes>fixture.code_bytes,1,"optimized shift module has fewer code bytes");
  for (i=0;i<DC_SHIFT_VALUE_COUNT;++i) {
    uint64_t value=dc_shift_values[i];
    for (j=0;j<DC_SHIFT_COUNT_COUNT;++j) {
      uint64_t amount=dc_shift_count(j);
      for (kind=0;kind<3;++kind)
        CHECK(((two)(uintptr_t)entry(rt,names[kind]))(value,amount),functions[kind](value,amount),"baseline and coalesced dynamic shifts agree");
      CHECK(((two)(uintptr_t)entry(rt,"dc-shift-mix"))(value,amount),mix(value,amount),"baseline and coalesced shift call chains agree");
    }
    CHECK(((one)(uintptr_t)entry(rt,"dc-shift-self"))(value),self(value),"baseline and coalesced self-count shift agree");
    for (kind=0;kind<3;++kind) for (j=0;j<5;++j)
      CHECK(((one)(uintptr_t)entry(rt,constant_names[kind][j]))(value),constant_functions[kind][j](value),"baseline and coalesced constant shift agree");
  }
#undef CHECK
  return fixture.code_bytes+original.code_bytes+baseline.code_bytes;
}
