/* Native float predicates/conversions and original helpers (AI-assisted), ISC. */
#include <kos.h>
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "float_vectors.h"
#include "build/float_fixture.h"
#include "build/original_math.h"
typedef uint64_t (*comparison)(float,float);
typedef uint64_t (*float_int)(float);
typedef float (*int_float)(uint64_t);
typedef float (*one)(float);
typedef float (*two)(float,float);
typedef float (*three)(float,float,float);
typedef float (*five)(float,float,float,float,float);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static float from_bits(uint32_t bits) { float value; memcpy(&value,&bits,4); return value; }
static uint32_t bits(float value) { uint32_t result; memcpy(&result,&value,4); return result; }
uint32_t dc_runtime_float_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                 unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result fixture,original;
  unsigned i,j;
  const char *names[]={"dc-fp-eq","dc-fp-ne","dc-fp-lt","dc-fp-le","dc-fp-gt","dc-fp-ge"};
  const uint64_t integers[]={0,(uint64_t)-7,7,16777219,UINT64_C(0x100000001),(uint64_t)INT32_MIN};
  const uint32_t converted[]={0,0xc0e00000u,0x40e00000u,0x4b800002u,0x3f800000u,0xcf000000u};
  const uint32_t roots[]={0x40800000u,0x41100000u,0,0x80000000u,0x7f800000u};
  const uint32_t answers[]={0x40000000u,0x40400000u,0,0x80000000u,0x7f800000u};
  uint32_t result;
#define CHECK(actual,want,label) check((uint64_t)(actual),(uint64_t)(want),++*id,label,context)
#define ONE(name,x) ((one)(uintptr_t)entry(rt,name))(x)
#define TWO(name,x,y) ((two)(uintptr_t)entry(rt,name))(x,y)
#define THREE(name,x,y,z) ((three)(uintptr_t)entry(rt,name))(x,y,z)
#define FIVE(name,a,b,c,d,e) ((five)(uintptr_t)entry(rt,name))(a,b,c,d,e)
  if (!dc_goal_load_module(rt,goal_float_fixture_exports,GOAL_FLOAT_FIXTURE_EXPORT_COUNT,sync,NULL,&fixture) ||
      !dc_goal_load_module(rt,goal_original_math_exports,GOAL_ORIGINAL_MATH_EXPORT_COUNT,sync,NULL,&original)) {
    CHECK(rt->error,0,"floating-point modules link"); return 0;
  }
  CHECK(__builtin_sh_get_fpscr()&3u,0,"native arithmetic round-to-nearest profile");
  for (i=0;i<sizeof(dc_float_vectors)/sizeof(dc_float_vectors[0]);++i) for (j=0;j<6;++j) {
    CHECK(((comparison)(uintptr_t)entry(rt,names[j]))(from_bits(dc_float_vectors[i].left),
             from_bits(dc_float_vectors[i].right)),(dc_float_vectors[i].mask>>j)&1u,"float condition matches pinned SSE oracle");
  }
  for (i=0;i<sizeof(dc_float_int_inputs)/sizeof(dc_float_int_inputs[0]);++i) {
    CHECK(((float_int)(uintptr_t)entry(rt,"dc-fp-to-int"))(from_bits(dc_float_int_inputs[i])),
          (uint64_t)(int64_t)dc_float_int_outputs[i],"float-to-int truncation/invalid result");
  }
  for (i=0;i<6;++i) CHECK(bits(((int_float)(uintptr_t)entry(rt,"dc-fp-to-float"))(integers[i])),
                          converted[i],"signed low-32 integer to float");
  CHECK(bits(TWO("dc-fp-min",2.0f,-3.0f)),bits(-3.0f),"float minimum ordering");
  CHECK(bits(TWO("dc-fp-max",2.0f,-3.0f)),bits(2.0f),"float maximum ordering");
  CHECK(bits(TWO("dc-fp-min",0.0f,from_bits(0x80000000u))),0x80000000u,"minimum signed-zero tie selects second");
  CHECK(bits(TWO("dc-fp-max",from_bits(0x80000000u),0.0f)),0,"maximum signed-zero tie selects second");
  CHECK(bits(TWO("dc-fp-min",from_bits(0x7fc01234u),2.0f)),bits(2.0f),"minimum first NaN selects second");
  CHECK(bits(TWO("dc-fp-max",from_bits(0x7fc01234u),2.0f)),bits(2.0f),"maximum first NaN selects second");
  CHECK(bits(TWO("dc-fp-min",2.0f,from_bits(0x7fc01234u))),0x7fc01234u,"minimum second NaN payload preserved");
  CHECK(bits(TWO("dc-fp-max",2.0f,from_bits(0x7fc01234u))),0x7fc01234u,"maximum second NaN payload preserved");
  for (i=0;i<5;++i) CHECK(bits(ONE("dc-fp-sqrt",from_bits(roots[i]))),answers[i],"native scalar square root");
  result=bits(ONE("dc-fp-sqrt",-4.0f));
  CHECK((result&0x7f800000u)==0x7f800000u && (result&0x007fffffu)!=0,1,"negative square root produces NaN");
  CHECK(bits(ONE("truncate",3.9f)),bits(3.0f),"original truncate positive");
  CHECK(bits(ONE("truncate",-3.9f)),bits(-3.0f),"original truncate negative");
  CHECK(bits(ONE("fractional-part",3.25f)),bits(0.25f),"original fractional part positive");
  CHECK(bits(ONE("fractional-part",-3.25f)),bits(-0.25f),"original fractional part negative");
  CHECK(((float_int)(uintptr_t)entry(rt,"integral?"))(3.0f),rt->s7+8u,"original integral predicate true");
  CHECK(((float_int)(uintptr_t)entry(rt,"integral?"))(3.25f),rt->s7,"original integral predicate false");
  CHECK(bits(THREE("seek",0.0f,10.0f,3.0f)),bits(3.0f),"original seek increasing");
  CHECK(bits(THREE("seek",10.0f,0.0f,3.0f)),bits(7.0f),"original seek decreasing");
  CHECK(bits(THREE("seek",9.0f,10.0f,3.0f)),bits(10.0f),"original seek reaches target");
  CHECK(bits(THREE("lerp-clamp",10.0f,20.0f,-0.25f)),bits(10.0f),"original lerp lower clamp");
  CHECK(bits(THREE("lerp-clamp",10.0f,20.0f,1.25f)),bits(20.0f),"original lerp upper clamp");
  CHECK(bits(THREE("lerp-clamp",10.0f,20.0f,0.25f)),bits(12.5f),"original lerp interior");
  CHECK(bits(FIVE("lerp-scale",10.0f,20.0f,5.0f,0.0f,10.0f)),bits(15.0f),"original lerp scale interior");
  CHECK(bits(FIVE("lerp-scale",10.0f,20.0f,-5.0f,0.0f,10.0f)),bits(10.0f),"original lerp scale lower clamp");
  CHECK(bits(FIVE("lerp-scale",10.0f,20.0f,15.0f,0.0f,10.0f)),bits(20.0f),"original lerp scale upper clamp");
  CHECK(bits(FIVE("lerp-scale",10.0f,20.0f,5.0f,0.0f,0.0f)),bits(20.0f),"original division-zero saturation path");
#undef FIVE
#undef THREE
#undef TWO
#undef ONE
#undef CHECK
  return fixture.code_bytes+original.code_bytes;
}
