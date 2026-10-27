/* Shared outer exits and original type/method traversal on SH-4 (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/early_fixture.h"
#include "build/original_early.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef uint64_t (*eight)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef float (*floats)(float,float);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static float as_float(uint32_t bits) { float value; memcpy(&value,&bits,4); return value; }
static uint32_t bits(float value) { uint32_t result; memcpy(&result,&value,4); return result; }
uint32_t dc_runtime_early_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                unsigned *id,dc_goal_cache_sync sync) {
  const uint64_t ints[]={0,1,42,UINT64_MAX,(uint64_t)-42,UINT64_C(0x100000001),
                          UINT64_C(0x8000000000000000),INT64_MAX};
  const uint32_t fp[]={0,0x80000000u,0x3fa00000u,0xc0200000u,0x7f800000u,0xff800000u,0x7fc12345u,0xffc12345u};
  const uint64_t loops[]={0,1,4,5,6,100,(uint64_t)-1};
  dc_goal_module_result fixture,original;
  uint64_t objects[6]; uint32_t parent,child,grand,record,method_a,method_b,nothing;
  one integer,loop,effect,nested; two object,basic_type,type_type,find; eight stack; floats floating;
  unsigned i;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module_data(rt,goal_early_fixture_exports,GOAL_EARLY_FIXTURE_EXPORT_COUNT,
        GOAL_EARLY_FIXTURE_DATA,GOAL_EARLY_FIXTURE_DATA_COUNT,sync,NULL,&fixture),1,"early-return fixture links");
  CHECK(dc_goal_load_module_data(rt,goal_original_early_exports,GOAL_ORIGINAL_EARLY_EXPORT_COUNT,
        GOAL_ORIGINAL_EARLY_DATA,GOAL_ORIGINAL_EARLY_DATA_COUNT,sync,NULL,&original),1,"original early-return kernel functions link");
  if (!fixture.functions || !original.functions) return 0;
  CHECK(fixture.functions,7,"seven original-frontend return fixtures");
  CHECK(original.functions,3,"three unchanged original kernel functions");
  integer=(one)(uintptr_t)entry(rt,"dc-early-int"); loop=(one)(uintptr_t)entry(rt,"dc-early-loop");
  effect=(one)(uintptr_t)entry(rt,"dc-early-effect"); nested=(one)(uintptr_t)entry(rt,"dc-early-nested");
  object=(two)(uintptr_t)entry(rt,"dc-early-object"); stack=(eight)(uintptr_t)entry(rt,"dc-early-eight");
  floating=(floats)(uintptr_t)entry(rt,"dc-early-float");
  for (i=0;i<sizeof(ints)/sizeof(ints[0]);++i)
    CHECK(integer(ints[i]),(int64_t)ints[i]<0?ints[i]-7:ints[i]==42?UINT64_C(12345678901):ints[i]+9,
          "shared return preserves both 32-bit halves and signed branching");
  for (i=0;i<sizeof(loops)/sizeof(loops[0]);++i)
    CHECK(loop(loops[i]),(int64_t)loops[i]<=0?0:loops[i]>5?105:loops[i],"loop exit and early function return");
  for (i=0;i<sizeof(fp)/sizeof(fp[0]);++i) {
    CHECK(bits(floating(-1.0f,as_float(fp[i]))),fp[i],"float early return preserves raw payload");
    /* The original frontend's pinned SSE unordered LT profile is true for NaN. */
    CHECK(bits(floating(as_float(fp[i]),17.0f)),
          as_float(fp[i])<0.0f || (fp[i]&0x7fffffffu)>0x7f800000u?bits(17.0f):fp[i],
          "float normal return and original SSE unordered comparison");
  }
  CHECK(effect(0),7,"early return result before later side effect");
  CHECK(entry(rt,"dc-early-marker"),11,"early return skips later side effect");
  CHECK(effect(1),8,"normal return result after side effect");
  CHECK(entry(rt,"dc-early-marker"),22,"normal path performs later side effect");
  objects[0]=rt->s7; objects[1]=rt->s7+8; objects[2]=rt->s7-10;
  objects[3]=dc_goal_string(rt,"early return"); objects[4]=dc_goal_new_pair(rt,rt->s7,rt->s7-10);
  objects[5]=UINT64_C(0x12345678abcdef01);
  for (i=0;i<6;++i) {
    CHECK(object(rt->s7,objects[i]),objects[i],"tagged early object return");
    CHECK(object(objects[i],rt->s7+8),i==0?rt->s7+8:objects[i],"tagged normal object return");
  }
  CHECK(nested(3),17,"nested block exit continues outer computation");
  CHECK(nested(4),99,"outer exit bypasses nested continuation");
  CHECK(nested(5),15,"nested normal path");
  CHECK(stack(1,2,3,4,5,6,7,8),36,"normal return with eight incoming arguments");
  CHECK(stack(UINT64_C(0x100000001),2,3,4,5,6,7,(uint64_t)-9),UINT64_C(0xfffffff8),"early return reads GCC stack argument");
  CHECK(stack(UINT64_C(0x100000001),2,3,4,5,6,7,UINT64_C(0x200000008)),UINT64_C(0x300000024),"normal stack path keeps high halves");
  parent=dc_goal_define_type(rt,"dc-early-parent",rt->basic_type,8,0,12);
  child=dc_goal_define_type(rt,"dc-early-child",parent,8,0,12);
  grand=dc_goal_define_type(rt,"dc-early-grand",child,8,0,12);
  CHECK(parent && child && grand,1,"test type chain created");
  if (!parent || !child || !grand) return fixture.code_bytes+original.code_bytes;
  record=dc_goal_new_basic(rt,grand,8);
  CHECK(record!=0,1,"original basic-type test object created");
  if (!record) return fixture.code_bytes+original.code_bytes;
  basic_type=(two)(uintptr_t)entry(rt,"basic-type?"); type_type=(two)(uintptr_t)entry(rt,"type-type?");
  find=(two)(uintptr_t)entry(rt,"find-parent-method");
  CHECK(basic_type(record,grand),rt->s7+8,"original basic-type exact match");
  CHECK(basic_type(record,parent),rt->s7+8,"original basic-type ancestor match");
  CHECK(basic_type(record,rt->string_type),rt->s7,"original basic-type absent match");
  CHECK(basic_type(record,rt->object_type),rt->s7,"original basic-type intentionally excludes object root");
  CHECK(type_type(grand,grand),rt->s7+8,"original type-type exact match");
  CHECK(type_type(grand,parent),rt->s7+8,"original type-type ancestor match");
  CHECK(type_type(grand,rt->string_type),rt->s7,"original type-type absent match");
  CHECK(type_type(grand,rt->object_type),rt->s7,"original type-type stops before object root");
  CHECK(type_type(rt->object_type,rt->object_type),rt->s7+8,"original body-first root exact match");
  method_a=entry(rt,"dc-early-int"); method_b=entry(rt,"dc-early-loop"); nothing=entry(rt,"nothing");
  CHECK(dc_goal_set_method(rt,parent,5,method_a) && dc_goal_set_method(rt,child,5,method_b) &&
        dc_goal_set_method(rt,grand,5,method_b),1,"method chain has inherited override");
  CHECK(find(grand,5),method_a,"original find-parent skips equal inherited methods");
  CHECK(find(child,5),method_a,"original find-parent returns different immediate method");
  CHECK(find(rt->object_type,5),nothing,"original find-parent exits at object root");
  /* Slot 7 (relocate) has no kscheme default; 6 (copy) does, as on desktop. */
  CHECK(find(grand,7),nothing,"original find-parent exits at zero parent slot");
  CHECK(find(grand,6),dc_goal_get_method(rt,rt->structure_type,6),"original find-parent reaches structure copy below basic copy");
  CHECK(rt->error,DC_GOAL_OK,"early return and original traversal leave runtime error clear");
#undef CHECK
  return fixture.code_bytes+original.code_bytes;
}
