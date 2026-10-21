/* Native typed memory and original list routines (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/memory_fixture.h"
#include "build/original_lists.h"

typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef float (*one_float)(uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t float_bits(float value) {
  uint32_t result; memcpy(&result,&value,4); return result;
}
uint32_t dc_runtime_memory_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                  unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result memory_module,lists;
  uint32_t address,head,middle,tail,text,pair,other,alist,empty=rt->s7-10u;
  unsigned char *data;
  uint64_t raw,value=UINT64_C(0xfedcba9889abcdef);
  uint32_t word;
  unsigned i;
  const char *loads[]={"i8","u8","i16","u16","i32","u32","i64","u64"};
  const uint64_t expected[]={UINT64_C(0xffffffffffffffef),239,UINT64_C(0xffffffffffffcdef),52719,
                             UINT64_C(0xffffffff89abcdef),UINT64_C(0x89abcdef),
                             UINT64_C(0xfedcba9889abcdef),UINT64_C(0xfedcba9889abcdef)};
  const char *stores[]={"u8","u16","u32","u64"};
  const uint64_t stored[]={UINT64_C(0xa5a5a5a5a5a5a5ef),UINT64_C(0xa5a5a5a5a5a5cdef),
                          UINT64_C(0xa5a5a5a589abcdef),UINT64_C(0xfedcba9889abcdef)};
#define CHECK(actual,want,label) check((uint64_t)(actual),(uint64_t)(want),++*id,label,context)
#define ONE(name,arg) ((one)(uintptr_t)entry(rt,name))((uint64_t)(arg))
#define TWO(name,a,b) ((two)(uintptr_t)entry(rt,name))((uint64_t)(a),(uint64_t)(b))
  if (!dc_goal_load_module(rt,goal_memory_fixture_exports,GOAL_MEMORY_FIXTURE_EXPORT_COUNT,sync,NULL,&memory_module) ||
      !dc_goal_load_module(rt,goal_original_lists_exports,GOAL_ORIGINAL_LISTS_EXPORT_COUNT,sync,NULL,&lists)) {
    CHECK(rt->error,0,"memory/list modules link"); return 0;
  }
  address=dc_goal_alloc(rt,352); data=dc_goal_resolve(rt,address,352);
  if (!data) { CHECK(0,1,"typed memory buffer allocation"); return 0; }
  memcpy(data+8,&value,8);
  for (i=0;i<8;++i) {
    char name[32]="dc-mem-load-";
    strcat(name,loads[i]);
    CHECK(ONE(name,address+8u),expected[i],"typed signed/unsigned load");
  }
  for (i=0;i<4;++i) {
    char name[32]="dc-mem-store-";
    strcat(name,stores[i]); memset(data,0xa5,32);
    CHECK(TWO(name,address+8u,value),value,"typed store preserves expression value");
    memcpy(&raw,data+8,8); CHECK(raw,stored[i],"typed store width and byte order");
    memcpy(&raw,data,8); CHECK(raw,UINT64_C(0xa5a5a5a5a5a5a5a5),"typed store leading canary");
    memcpy(&raw,data+16,8); CHECK(raw,UINT64_C(0xa5a5a5a5a5a5a5a5),"typed store trailing canary");
  }
  word=0xdeadbeefu; memcpy(data+320,&word,4);
  CHECK(ONE("dc-mem-offset",address),0xdeadbeefu,"large constant field offset");
  word=0xc0f00000u; memcpy(data+8,&word,4);
  CHECK(float_bits(((one_float)(uintptr_t)entry(rt,"dc-mem-load-float"))(address+8u)),word,"float memory load");
  word=0x80000000u; memcpy(data+8,&word,4);
  CHECK(float_bits(((one_float)(uintptr_t)entry(rt,"dc-mem-load-float"))(address+8u)),word,"float signed zero preserved");
  word=0x40500000u; memcpy(data+12,&word,4);
  CHECK(float_bits(((one_float)(uintptr_t)entry(rt,"dc-mem-store-float"))(address+8u)),0x40500000u,"float store expression result");
  memcpy(&word,data+8,4); CHECK(word,0x40500000u,"float native field mutation");
  text=dc_goal_string(rt,"original native list"); pair=dc_goal_new_pair(rt,text,empty);
  CHECK(ONE("dc-mem-type",text),rt->string_type,"basic type header at negative offset");
  CHECK(ONE("dc-mem-car",pair),text,"pair car tag-adjusted load");
  CHECK(ONE("dc-mem-cdr",pair),empty,"pair cdr tag-adjusted load");
  CHECK(TWO("dc-mem-set-car",pair,rt->s7+8u),rt->s7+8u,"pair car store expression");
  CHECK(ONE("dc-mem-car",pair),rt->s7+8u,"pair car mutation visible");
  CHECK(TWO("dc-mem-set-car",pair,(uint64_t)-8),(uint64_t)-8,"negative binteger stored in object cell");
  CHECK(ONE("dc-mem-car",pair),(uint64_t)-8,"object cell preserves signed binteger semantics");
  other=dc_goal_new_pair(rt,text,empty);
  CHECK(TWO("dc-mem-set-cdr",pair,other),other,"pair cdr store expression");
  CHECK(ONE("dc-mem-cdr",pair),other,"pair cdr mutation visible");
  tail=dc_goal_new_pair(rt,text,empty); middle=dc_goal_new_pair(rt,rt->s7+8u,tail);
  head=dc_goal_new_pair(rt,rt->s7,middle);
  CHECK(TWO("ref",head,0),rt->s7,"original ref first false value");
  CHECK(TWO("ref",head,1),rt->s7+8u,"original ref middle");
  CHECK(TWO("ref",head,2),text,"original ref last");
  CHECK(ONE("last",head),tail,"original last traverses list");
  CHECK(ONE("last",tail),tail,"original last singleton");
  CHECK(TWO("member",rt->s7,head),head,"original member first");
  CHECK(TWO("member",rt->s7+8u,head),middle,"original member middle");
  CHECK(TWO("member",text,head),tail,"original member tail");
  CHECK(TWO("member",pair,head),rt->s7,"original member absent");
  CHECK(TWO("member",text,empty),rt->s7,"original member empty");
  pair=dc_goal_new_pair(rt,text,rt->s7+8u); other=dc_goal_new_pair(rt,rt->s7,0x12345678u);
  tail=dc_goal_new_pair(rt,pair,empty); alist=dc_goal_new_pair(rt,other,tail);
  CHECK(TWO("assoc",text,alist),pair,"original assoc tail match");
  CHECK(TWO("assoc",rt->s7,alist),other,"original assoc first match");
  CHECK(TWO("assoc",rt->s7+8u,alist),rt->s7,"original assoc absent");
  CHECK(TWO("assoc",text,empty),rt->s7,"original assoc empty");
#undef TWO
#undef ONE
#undef CHECK
  return memory_module.code_bytes+lists.code_bytes;
}
