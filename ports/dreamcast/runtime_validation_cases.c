/* Unchanged original valid? with an explicit Dreamcast context (AI-assisted), ISC. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "goal_module.h"
#include "runtime_cases.h"
#include "build/original_valid.h"
#include "build/scalar_validation_fixture.h"
typedef uint64_t (*zero)(void);
typedef uint64_t (*validator)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
uint32_t dc_runtime_validation_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                     unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result original,profile;
  validator valid;
  uint32_t pair,string,structure,child,record,bad,low=0,high=0,i;
  const uint64_t integers[]={0,8,(uint64_t)-8,UINT64_C(0x7ffffffffffffff8),UINT64_C(0x8000000000000000)};
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
#define VALID(value,type) valid((value),(type),rt->s7,rt->s7,0)
  CHECK(dc_goal_load_module_data(rt,goal_original_valid_exports,GOAL_ORIGINAL_VALID_EXPORT_COUNT,
        GOAL_ORIGINAL_VALID_DATA,GOAL_ORIGINAL_VALID_DATA_COUNT,sync,NULL,&original),1,"original validator module links");
  CHECK(dc_goal_load_module_data(rt,goal_scalar_validation_fixture_exports,GOAL_SCALAR_VALIDATION_FIXTURE_EXPORT_COUNT,
        GOAL_SCALAR_VALIDATION_FIXTURE_DATA,GOAL_SCALAR_VALIDATION_FIXTURE_DATA_COUNT,sync,NULL,&profile),1,"explicit scalar validation context links");
  if (!original.functions || !profile.functions) return 0;
  CHECK(original.functions,1,"only selected unchanged original validator exported");
  CHECK(profile.functions,3,"three explicit context inspection functions");
  CHECK(((zero)(uintptr_t)entry(rt,"dc-validation-start"))(),rt->table,"native symbol table lower bound");
  CHECK(((zero)(uintptr_t)entry(rt,"dc-validation-end"))(),rt->table+DC_GOAL_SYMBOL_BYTES,"native reserved symbol table upper bound");
  CHECK(((zero)(uintptr_t)entry(rt,"dc-validation-ram-end"))(),0x0d000000u,"stock 16 MiB cached P0 RAM upper bound");
  CHECK(rt->s7-rt->table,65540,"context fixed role matches native s7 layout");
  CHECK(rt->base>=0x0c000000u && rt->base+rt->capacity<=0x0d000000u,1,"diagnostic arena within configured native RAM");
  valid=(validator)(uintptr_t)entry(rt,"valid?");
  pair=dc_goal_new_pair(rt,rt->s7+8,rt->s7-10);
  string=dc_goal_string(rt,"original validator"); structure=dc_goal_alloc(rt,32);
  child=dc_goal_define_type(rt,"dc-validation-child",rt->basic_type,8,0,12);
  record=dc_goal_new_basic(rt,child,8); bad=dc_goal_new_basic(rt,rt->symbol_type,8);
  CHECK(pair && string && structure && child && record && bad,1,"validator native fixtures allocated");
  if (!pair || !string || !structure || !child || !record || !bad) return original.code_bytes+profile.code_bytes;
  CHECK(VALID(structure,rt->s7),rt->s7+8,"original raw address accepts native arena");
  CHECK(VALID(0x0cfffffcu,rt->s7),rt->s7+8,"original raw address accepts RAM last aligned word without dereference");
  CHECK(VALID(0x0d000000u,rt->s7),rt->s7,"original raw address rejects exclusive RAM end");
  CHECK(VALID(0x8c000000u,rt->s7),rt->s7,"original raw address rejects cached P1 alias");
  CHECK(VALID(rt->table-4,rt->s7),rt->s7,"original raw address rejects below symbol table");
  CHECK(VALID(structure+1,rt->s7),rt->s7,"original raw address rejects misalignment");
  CHECK(VALID(structure,rt->structure_type),rt->s7+8,"original structure accepts aligned native allocation");
  CHECK(VALID(structure+4,rt->structure_type),rt->s7,"original structure rejects four-byte-only alignment");
  CHECK(VALID(rt->table,rt->structure_type),rt->s7,"original structure rejects reserved symbol memory");
  CHECK(VALID(pair,rt->pair_type),rt->s7+8,"original pair validation");
  CHECK(VALID(rt->s7-10,rt->pair_type),rt->s7+8,"original empty pair in symbol table");
  CHECK(VALID(structure,rt->pair_type),rt->s7,"original pair rejects wrong tag");
  CHECK(VALID(0x0d000002u,rt->pair_type),rt->s7,"original pair rejects address outside RAM before dereference");
  for(i=0;i<sizeof(integers)/sizeof(integers[0]);++i)
    CHECK(VALID(integers[i],rt->binteger_type),rt->s7+8,"original binteger validation preserves 64-bit payload");
  CHECK(VALID(1,rt->binteger_type),rt->s7,"original binteger rejects wrong tag");
  CHECK(valid(rt->s7,rt->string_type,rt->s7,rt->s7+8,0),rt->s7+8,"original allow-false bypasses type check");
  CHECK(VALID(rt->s7,rt->string_type),rt->s7,"original disallowed false fails string type");
  CHECK(VALID(rt->s7,rt->symbol_type),rt->s7+8,"original false remains valid symbol");
  CHECK(VALID(rt->s7+8,rt->symbol_type),rt->s7+8,"original true symbol validation");
  CHECK(VALID(string,rt->string_type),rt->s7+8,"original string recursive runtime type validation");
  CHECK(VALID(string,rt->basic_type),rt->s7+8,"original basic ancestor type validation");
  CHECK(VALID(record,child),rt->s7+8,"original exact child type validation");
  CHECK(VALID(record,rt->basic_type),rt->s7+8,"original inherited child type validation");
  CHECK(VALID(record,rt->string_type),rt->s7,"original mismatched child type rejection");
  CHECK(VALID(child,rt->type_type),rt->s7+8,"original type object validation");
  CHECK(VALID(string,rt->type_type),rt->s7,"original non-type runtime tag rejection");
  CHECK(VALID(bad,rt->symbol_type),rt->s7,"original heap object cannot masquerade as table symbol");
  CHECK(VALID(rt->s7,rt->basic_type),rt->s7,"original non-symbol expectation rejects table storage");
  { uint32_t invalid_type=0; memcpy(dc_goal_resolve(rt,bad-4,4),&invalid_type,4); }
  CHECK(VALID(bad,rt->basic_type),rt->s7,"original recursive invalid runtime type rejects safely");
  for(i=0;i<32;++i) {
    char name[40]; uint32_t sym;
    snprintf(name,sizeof(name),"dc-validation-bucket-%08lx",(unsigned long)(i*2654435761u));
    sym=dc_goal_intern(rt,name);
    if(sym<rt->s7-32768) ++low;
    if(sym>=rt->s7+32768) ++high;
    CHECK(VALID(sym,rt->symbol_type),rt->s7+8,"original validator covers native hash buckets outside old approximation");
  }
  CHECK(low>0 && high>0,1,"symbol probes cover both previously excluded table ranges");
  CHECK(rt->error,DC_GOAL_OK,"native original validation leaves runtime error clear");
#undef VALID
#undef CHECK
  return original.code_bytes+profile.code_bytes;
}
