/* Native static object and exact float-bit checks (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/static_fixture.h"
typedef uint64_t (*zero)(void);
typedef uint64_t (*one)(uint64_t);
typedef float (*float_zero)(void);
typedef float (*float_one)(float);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t bits(float value) { uint32_t result; memcpy(&result,&value,4); return result; }
uint32_t dc_runtime_static_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                  unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result module,failure;
  dc_goal_data_export invalid[GOAL_STATIC_FIXTURE_DATA_COUNT];
  dc_goal_code_relocation bad_type={0,"type:missing-type"};
  uint64_t result;
  uint32_t text,literal,length,saved_used,old_function;
  unsigned char *characters;
#define CHECK(actual,want,label) check((uint64_t)(actual),(uint64_t)(want),++*id,label,context)
#define ZERO(name) ((zero)(uintptr_t)entry(rt,name))()
#define ONE(name,arg) ((one)(uintptr_t)entry(rt,name))((uint64_t)(arg))
  if (!dc_goal_load_module_data(rt,goal_static_fixture_exports,GOAL_STATIC_FIXTURE_EXPORT_COUNT,
                               GOAL_STATIC_FIXTURE_DATA,GOAL_STATIC_FIXTURE_DATA_COUNT,sync,NULL,&module)) {
    CHECK(rt->error,0,"static module link"); return 0;
  }
  CHECK(module.data_objects,2,"selected static string objects");
  CHECK(module.data_bytes,54,"original string headers and payload bytes");
  CHECK(module.temporary_bytes,262152,"bounded static relocation address table");
  CHECK(entry(rt,"dc-static-text"),0,"static global not initialized by link");
  CHECK(dc_goal_run_initializer(rt,GOAL_STATIC_FIXTURE_INITIALIZER,&result),1,"static-data startup execution");
  CHECK(result,rt->s7,"static-data startup return");
  text=entry(rt,"dc-static-text");
  CHECK(dc_goal_type_of(rt,text),rt->string_type,"static string runtime type header");
  memcpy(&length,dc_goal_resolve(rt,text,4),4);
  CHECK(length,21,"static string original length field");
  characters=dc_goal_resolve(rt,text+4,length+1);
  CHECK(memcmp(characters,"Dreamcast static GOAL",22),0,"static string payload and terminator");
  CHECK(ZERO("dc-static-read-text"),text,"compiled static global reader");
  literal=(uint32_t)ZERO("dc-static-literal");
  CHECK(ZERO("dc-static-literal"),literal,"literal retains object identity across calls");
  CHECK(dc_goal_type_of(rt,literal),rt->string_type,"literal static type header");
  CHECK(ONE("dc-static-string-length",literal),15,"compiled literal length field");
  CHECK(ONE("dc-static-first-char",literal),'l',"compiled literal byte field");
  CHECK(ONE("dc-static-first-char",text),'D',"compiled static byte field");
  characters[0]='d';
  CHECK(ONE("dc-static-first-char",text),'d',"native static mutation visible");
  CHECK(bits(((float_zero)(uintptr_t)entry(rt,"dc-static-number"))()),0x40500000u,"exact original float literal bits");
  CHECK(bits(((float_zero)(uintptr_t)entry(rt,"dc-static-read-float"))()),0x40500000u,"initialized float global bits");
  CHECK(bits(((float_one)(uintptr_t)entry(rt,"dc-static-float-add"))(-2.0f)),0xbf000000u,"float literal arithmetic");
  CHECK(entry(rt,"dc-static-float"),0x40500000u,"native float global cell representation");
  saved_used=rt->used; old_function=entry(rt,"dc-static-literal");
  memcpy(invalid,GOAL_STATIC_FIXTURE_DATA,sizeof(invalid));
  invalid[0].relocations=&bad_type; invalid[0].relocation_count=1;
  CHECK(dc_goal_load_module_data(rt,goal_static_fixture_exports,GOAL_STATIC_FIXTURE_EXPORT_COUNT,
                                invalid,GOAL_STATIC_FIXTURE_DATA_COUNT,sync,NULL,&failure),0,"native late data relocation rejection");
  CHECK(rt->used,saved_used,"native data failure rolls arena back");
  CHECK(entry(rt,"dc-static-literal"),old_function,"native data failure restores old function cell");
  CHECK(ZERO("dc-static-literal"),literal,"old code and static object survive failed load");
  CHECK(dc_goal_find(rt,"missing-type"),0,"missing type import does not intern a substitute");
#undef ONE
#undef ZERO
#undef CHECK
  return module.code_bytes;
}
