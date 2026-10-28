/* Native loader transaction/bounds acceptance cases (AI-assisted), ISC. */
#include "goal_module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned tests,passed,syncs;
#define CHECK(expression) do { ++tests; if (expression) ++passed; \
  else fprintf(stderr,"FAIL %u at line %u\n",tests,(unsigned)__LINE__); } while (0)
static void sync_code(uint32_t address,uint32_t bytes,void *context) {
  dc_goal_runtime *rt=context;
  CHECK(dc_goal_resolve(rt,address,bytes)!=NULL);
  ++syncs;
}
static uint32_t word_at(dc_goal_runtime *rt,uint32_t address) {
  uint32_t value; memcpy(&value,dc_goal_resolve(rt,address,4),4); return value;
}
int main(void) {
  const uint32_t capacity=1024u*1024u;
  void *allocation=malloc(capacity+16u);
  unsigned char *memory,*snapshot=malloc(DC_GOAL_SYMBOL_BYTES);
  dc_goal_runtime rt;
  dc_goal_module_result result;
  const unsigned char code[16]={0x0b,0,9,0,0,0,0,0,0,0,0,0,0,0,0,0};
  const dc_goal_code_relocation relocations[]={{4,"cell:module-counter"},{8,"pointer:#f"},
                                               {12,"function:module-other"}};
  dc_goal_code_export functions[2]={{"module-reader",code,16,relocations,3},
                                     {"module-other",code,4,NULL,0}};
  dc_goal_code_export bad;
  dc_goal_code_relocation bad_reloc;
  uint32_t function,symbol,used,count;
  uint64_t value=UINT64_C(0xfeedfeedfeedfeed);
  if (!allocation || !snapshot) return 2;
  memory=(unsigned char*)(((uintptr_t)allocation+15u)&~(uintptr_t)15u);
  CHECK(dc_goal_init(&rt,memory,capacity,0x8c100000u));
  CHECK(dc_goal_load_module(&rt,functions,2,sync_code,&rt,&result));
  CHECK(result.functions==2 && result.code_bytes==20);
  CHECK(result.temporary_bytes==262144 && result.arena_bytes>20);
  CHECK(syncs==2);
  symbol=dc_goal_find(&rt,"module-reader"); function=dc_goal_symbol_value(&rt,symbol);
  CHECK(dc_goal_type_of(&rt,function)==rt.function_type);
  CHECK((function&15u)==4u);
  CHECK(word_at(&rt,function+4)==dc_goal_find(&rt,"module-counter"));
  CHECK(word_at(&rt,function+8)==rt.s7);
  CHECK(word_at(&rt,function+12)==dc_goal_symbol_value(&rt,dc_goal_find(&rt,"module-other")));
  CHECK(!memcmp(dc_goal_resolve(&rt,function,4),code,4));
  used=rt.used; count=rt.symbol_count;
  memcpy(snapshot,dc_goal_resolve(&rt,rt.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES);
  /* Second-pass failure must roll back new names AND a replaced function cell. */
  bad_reloc.offset=4; bad_reloc.symbol="cell:_empty_";
  bad=functions[0]; bad.relocations=&bad_reloc; bad.relocation_count=1;
  functions[1].name="module-new-before-failure";
  functions[0]=bad;
  CHECK(!dc_goal_load_module(&rt,functions,2,sync_code,&rt,&result));
  CHECK(rt.error==DC_GOAL_BAD_MODULE && syncs==2);
  CHECK(rt.used==used && rt.symbol_count==count);
  CHECK(!memcmp(snapshot,dc_goal_resolve(&rt,rt.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES));
  CHECK(dc_goal_symbol_value(&rt,symbol)==function);
  bad=functions[0]; bad.relocations=&bad_reloc; bad.relocation_count=1;
  bad_reloc.offset=4; bad_reloc.symbol="function:#f";
  CHECK(!dc_goal_load_module(&rt,&bad,1,NULL,NULL,&result));
  CHECK(rt.error==DC_GOAL_BAD_MODULE && rt.used==used);
  bad_reloc.symbol="function:module-missing";
  CHECK(!dc_goal_load_module(&rt,&bad,1,NULL,NULL,&result));
  CHECK(rt.error==DC_GOAL_BAD_MODULE && rt.used==used && rt.symbol_count==count);
  CHECK(!dc_goal_find(&rt,"module-missing"));
  CHECK(!dc_goal_run_initializer(&rt,"module-reader",&value));
  CHECK(rt.error==DC_GOAL_BAD_ARGUMENT && value==UINT64_C(0xfeedfeedfeedfeed));
  CHECK(!dc_goal_run_initializer(&rt,"#f",&value));
  CHECK(rt.error==DC_GOAL_BAD_MODULE);
  CHECK(!dc_goal_run_initializer(&rt,"module-missing",&value));
  CHECK(!dc_goal_run_initializer(&rt,"module-reader",NULL));
  CHECK(!dc_goal_run_initializer_void(&rt,"module-reader") && rt.error==DC_GOAL_BAD_ARGUMENT);
  CHECK(!dc_goal_run_initializer_void(&rt,"#f") && rt.error==DC_GOAL_BAD_MODULE);
  CHECK(!dc_goal_run_initializer_void(&rt,"module-missing"));
  CHECK(!dc_goal_run_initializer_void(NULL,"module-reader"));
  CHECK(!dc_goal_run_initializer_void(&rt,NULL));
  CHECK(!dc_goal_find(&rt,"module-new-before-failure"));
  CHECK(result.functions==0 && result.code_bytes==0 && result.arena_bytes==0);
  /* OOM after replacing the first cell must also restore the whole transaction. */
  functions[0].relocations=NULL; functions[0].relocation_count=0;
  functions[1].bytes=capacity;
  CHECK(!dc_goal_load_module(&rt,functions,2,NULL,NULL,&result));
  CHECK(rt.error==DC_GOAL_OUT_OF_MEMORY);
  CHECK(rt.used==used && rt.symbol_count==count);
  CHECK(!memcmp(snapshot,dc_goal_resolve(&rt,rt.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES));
  CHECK(dc_goal_symbol_value(&rt,symbol)==function);
  /* Reject collisions with non-function builtin bindings. */
  bad=functions[0]; bad.name="#f";
  CHECK(!dc_goal_load_module(&rt,&bad,1,NULL,NULL,&result));
  CHECK(rt.error==DC_GOAL_BAD_MODULE && rt.used==used);
  CHECK(dc_goal_symbol_value(&rt,rt.s7)==rt.s7);
  /* Malformed trusted compiler descriptors fail before changing the arena. */
  bad.name="bad"; bad.bytes=3;
  CHECK(!dc_goal_load_module(&rt,&bad,1,NULL,NULL,&result));
  bad.bytes=12; bad.relocations=&bad_reloc; bad.relocation_count=1;
  bad_reloc.offset=2; bad_reloc.symbol="pointer:#f";
  CHECK(!dc_goal_load_module(&rt,&bad,1,NULL,NULL,&result));
  bad_reloc.offset=12;
  CHECK(!dc_goal_load_module(&rt,&bad,1,NULL,NULL,&result));
  bad_reloc.offset=4; bad_reloc.symbol="pointer:";
  CHECK(!dc_goal_load_module(&rt,&bad,1,NULL,NULL,&result));
  bad_reloc.symbol="untyped:#f";
  CHECK(!dc_goal_load_module(&rt,&bad,1,NULL,NULL,&result));
  bad.relocations=NULL;
  CHECK(!dc_goal_load_module(&rt,&bad,1,NULL,NULL,&result));
  functions[1]=functions[0];
  CHECK(!dc_goal_load_module(&rt,functions,2,NULL,NULL,&result));
  CHECK(!dc_goal_load_module(&rt,functions,0,NULL,NULL,&result));
  CHECK(!dc_goal_load_module(&rt,NULL,1,NULL,NULL,&result));
  CHECK(rt.used==used && rt.symbol_count==count);
  CHECK(!memcmp(snapshot,dc_goal_resolve(&rt,rt.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES));
  {
    const unsigned char text_bytes[]={0,0,0,0,3,0,0,0,'A',0,'B',0};
    const unsigned char pointer_bytes[]={0,0,0,0};
    dc_goal_code_relocation text_type={0,"type:string"},local_pointer={0,"data:text"};
    dc_goal_code_relocation import={4,"data:forward"};
    dc_goal_data_export objects[2]={{"text",text_bytes,12,4,&text_type,1},
                                    {"forward",pointer_bytes,4,0,&local_pointer,1}};
    dc_goal_code_export reader={"data-reader",code,8,&import,1};
    uint32_t object,forward;
    CHECK(dc_goal_load_module_data(&rt,&reader,1,objects,2,sync_code,&rt,&result));
    CHECK(result.data_objects==2 && result.data_bytes==16 && result.temporary_bytes==262152);
    forward=word_at(&rt,dc_goal_symbol_value(&rt,dc_goal_find(&rt,"data-reader"))+4);
    object=word_at(&rt,forward);
    CHECK(dc_goal_type_of(&rt,object)==rt.string_type);
    CHECK(word_at(&rt,object)==3);
    CHECK(!memcmp(dc_goal_resolve(&rt,object+4,4),text_bytes+8,4));
    CHECK(!dc_goal_find(&rt,"text") && !dc_goal_find(&rt,"forward"));
    used=rt.used; count=rt.symbol_count;
    memcpy(snapshot,dc_goal_resolve(&rt,rt.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES);
    local_pointer.symbol="data:missing";
    CHECK(!dc_goal_load_module_data(&rt,&reader,1,objects,2,NULL,NULL,&result));
    CHECK(rt.error==DC_GOAL_BAD_MODULE && rt.used==used && rt.symbol_count==count);
    CHECK(!memcmp(snapshot,dc_goal_resolve(&rt,rt.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES));
    CHECK(result.functions==0 && result.data_objects==0 && result.data_bytes==0);
    local_pointer.symbol="data:text"; text_type.symbol="type:module-reader";
    CHECK(!dc_goal_load_module_data(&rt,&reader,1,objects,2,NULL,NULL,&result));
    CHECK(rt.error==DC_GOAL_BAD_MODULE && rt.used==used);
    text_type.symbol="type:string"; objects[0].address_offset=12;
    CHECK(!dc_goal_load_module_data(&rt,&reader,1,objects,2,NULL,NULL,&result));
    objects[0].address_offset=4; objects[1].name="text";
    CHECK(!dc_goal_load_module_data(&rt,&reader,1,objects,2,NULL,NULL,&result));
    objects[1].name="forward";
    CHECK(!dc_goal_load_module_data(&rt,&reader,1,NULL,2,NULL,NULL,&result));
    objects[0].bytes=capacity; objects[0].relocation_count=0;
    CHECK(!dc_goal_load_module_data(&rt,&reader,1,objects,2,NULL,NULL,&result));
    CHECK(rt.error==DC_GOAL_OUT_OF_MEMORY && rt.used==used);
    CHECK(!memcmp(snapshot,dc_goal_resolve(&rt,rt.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES));
    CHECK(word_at(&rt,forward)==object);
  }
  { /* A temporary startup: top of the arena, outside the registry, freed once. */
    dc_goal_code_export startup[2]={{"module-startup",code,8,NULL,0},{"module-kept",code,4,NULL,0}};
    dc_goal_code_entry_info info;
    uint32_t top=rt.base+rt.capacity,entry,kept,low;
    used=rt.used; count=rt.code_registry_count;
    CHECK(dc_goal_load_module_temporary(&rt,startup,2,NULL,0,"module-startup",NULL,NULL,&result));
    entry=dc_goal_symbol_value(&rt,dc_goal_find(&rt,"module-startup"));
    kept=dc_goal_symbol_value(&rt,dc_goal_find(&rt,"module-kept"));
    CHECK(rt.temporary==16 && entry==top-12u && rt.temporary_entry==entry && rt.temporary_bytes==8);
    CHECK(kept<rt.base+rt.used && rt.code_registry_count==count+1);
    CHECK(dc_goal_type_of(&rt,entry)==rt.function_type && dc_goal_resolve(&rt,entry,8));
    CHECK(dc_goal_code_entry(&rt,entry,&info) && info.entry==entry && info.bytes==8);
    CHECK(dc_goal_code_entry(&rt,kept,&info) && info.entry==kept);
    /* One resident at a time: a second temporary load rolls back whole. */
    used=rt.used;
    CHECK(!dc_goal_load_module_temporary(&rt,startup+1,1,NULL,0,"module-kept",NULL,NULL,&result));
    CHECK(rt.used==used && rt.temporary==16 && rt.temporary_entry==entry);
    /* Ordinary allocations stop below the region. */
    low=rt.capacity-rt.temporary-rt.used;
    CHECK(!dc_goal_alloc(&rt,low+16u) && rt.error==DC_GOAL_OUT_OF_MEMORY && rt.used==used);
    dc_goal_release_temporary(&rt);
    CHECK(rt.temporary==0 && rt.temporary_entry==0 && rt.temporary_symbol==0);
    CHECK(dc_goal_symbol_value(&rt,dc_goal_find(&rt,"module-startup"))==rt.s7);
    CHECK(!dc_goal_code_entry(&rt,entry,&info) && !dc_goal_resolve(&rt,entry,4));
    CHECK(dc_goal_code_entry(&rt,kept,&info) && info.entry==kept);
    CHECK(dc_goal_alloc_temporary(&rt,1)==top-16u && rt.temporary==16);
    dc_goal_release_temporary(&rt);
    CHECK(rt.temporary==0 && !dc_goal_alloc_temporary(&rt,0));
  }
  CHECK(dc_goal_bind_fixed(&rt,DC_FIX_SYM_GLOBAL_HEAP,"global",0));
  CHECK(dc_goal_find(&rt,"global")==rt.s7+DC_FIX_SYM_GLOBAL_HEAP);
  CHECK(dc_goal_bind_fixed(&rt,DC_FIX_SYM_GLOBAL_HEAP,"global",123));
  CHECK(dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_GLOBAL_HEAP)==123);
  CHECK(!dc_goal_bind_fixed(&rt,1,"misaligned",0));
  CHECK(!dc_goal_bind_fixed(&rt,DC_FIX_FIXED_SYM_END_OFFSET,"outside",0));
  CHECK(!dc_goal_bind_fixed(&rt,DC_FIX_SYM_GLOBAL_HEAP,"#t",0));
  CHECK(!dc_goal_bind_fixed(&rt,DC_FIX_SYM_TRUE,"global",0));
  CHECK(!dc_goal_bind_fixed(&rt,DC_FIX_SYM_GLOBAL_HEAP,"other",0));
  CHECK(!dc_goal_bind_fixed(&rt,DC_FIX_SYM_GLOBAL_HEAP,"",0));
  free(snapshot); free(allocation);
  printf("{\"tests\":%u,\"passed\":%u}\n",tests,passed);
  return tests==passed?0:1;
}
