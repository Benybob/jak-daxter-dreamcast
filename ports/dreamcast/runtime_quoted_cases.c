/* Original reader's quoted pair graph on SH-4 (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/quoted_fixture.h"
typedef uint64_t (*zero)(void);
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t word_at(dc_goal_runtime *rt,uint32_t address) {
  uint32_t value; memcpy(&value,dc_goal_resolve(rt,address,4),4); return value;
}
uint32_t dc_runtime_quoted_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                  unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result module;
  uint64_t result;
  uint32_t root,text,nested,literal,last;
#define CHECK(actual,want,label) check((uint64_t)(actual),(uint64_t)(want),++*id,label,context)
#define ZERO(name) ((zero)(uintptr_t)entry(rt,name))()
#define ONE(name,x) ((one)(uintptr_t)entry(rt,name))((uint64_t)(x))
#define TWO(name,x,y) ((two)(uintptr_t)entry(rt,name))((uint64_t)(x),(uint64_t)(y))
  if (!dc_goal_load_module_data(rt,goal_quoted_fixture_exports,GOAL_QUOTED_FIXTURE_EXPORT_COUNT,
                               GOAL_QUOTED_FIXTURE_DATA,GOAL_QUOTED_FIXTURE_DATA_COUNT,sync,NULL,&module)) {
    CHECK(rt->error,0,"quoted graph link"); return 0;
  }
  CHECK(module.data_objects,9,"quoted graph transitive object count");
  CHECK(module.data_bytes,77,"quoted graph raw pair/string bytes");
  CHECK(dc_goal_run_initializer(rt,GOAL_QUOTED_FIXTURE_INITIALIZER,&result),1,"quoted graph initialization");
  CHECK(result,rt->s7,"quoted graph top-level return");
  root=entry(rt,"dc-quoted-list");
  CHECK(ZERO("dc-quoted-root"),root,"compiled quoted global reader");
  CHECK(dc_goal_type_of(rt,root),rt->pair_type,"quoted root pair tag");
  CHECK(TWO("ref",root,0),dc_goal_find(rt,"alpha"),"quoted symbol is interned");
  CHECK(TWO("ref",root,1),(uint64_t)-16,"quoted negative integer is boxed binteger");
  text=(uint32_t)TWO("ref",root,2);
  CHECK(dc_goal_type_of(rt,text),rt->string_type,"quoted nested string type");
  CHECK(word_at(rt,text),4,"quoted string length");
  CHECK(memcmp(dc_goal_resolve(rt,text+4,5),"text",5),0,"quoted string bytes");
  nested=(uint32_t)TWO("ref",root,3);
  CHECK(dc_goal_type_of(rt,nested),rt->pair_type,"quoted nested pair tag");
  CHECK(TWO("ref",nested,0),dc_goal_find(rt,"nested"),"quoted nested symbol");
  CHECK(TWO("ref",nested,1),rt->s7+8u,"quoted true symbol identity");
  last=(uint32_t)ONE("last",root);
  CHECK(word_at(rt,last+2),rt->s7-10u,"quoted list uses canonical empty tail");
  literal=(uint32_t)ZERO("dc-quoted-literal");
  CHECK(TWO("ref",literal,0),dc_goal_find(rt,"repeat"),"quoted repeated symbol first");
  CHECK(TWO("ref",literal,1),dc_goal_find(rt,"repeat"),"quoted repeated symbol shares identity");
  CHECK(ZERO("dc-quoted-literal"),literal,"quoted literal retains pair identity");
  TWO("dc-mem-set-car",root,rt->s7);
  CHECK(ZERO("dc-quoted-head"),rt->s7,"quoted pair mutation visible to compiled code");
#undef TWO
#undef ONE
#undef ZERO
#undef CHECK
  return module.code_bytes;
}
