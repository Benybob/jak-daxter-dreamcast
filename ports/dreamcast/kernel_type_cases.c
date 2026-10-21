/* Original fixed metadata and atomic namespace setup (AI-assisted), ISC. */
#include "kernel_type_cases.h"
#include "kernel_types_data.h"
#include <stdlib.h>
#include <string.h>
static dc_goal_type_fields *fields(dc_goal_runtime *rt,uint32_t type) {
  return dc_goal_resolve(rt,type,sizeof(dc_goal_type_fields));
}
void dc_kernel_type_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id) {
  dc_kernel_types_result result;
  uint32_t used,symbols,old_method,method,old_art,old_global,i;
  void *allocation;
  unsigned char *memory,*snapshot;
  dc_goal_runtime failure;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_install_kernel_types(rt,&result),1,"original fixed type metadata installation");
  if (!result.types) return;
  CHECK(result.types,35,"all original C fixed type definitions installed");
  CHECK(result.new_types,24,"twenty-four types added beyond minimal eleven");
  CHECK(result.placeholders,6,"six original fixed asset placeholders");
  CHECK(result.temporary_bytes,262144,"fixed type installation snapshot cost");
  for (i=0;i<DC_KERNEL_TYPE_COUNT;++i) {
    const dc_kernel_type_schema *schema=&dc_kernel_types[i];
    uint32_t symbol=dc_goal_find(rt,schema->name),type=dc_goal_symbol_value(rt,symbol);
    dc_goal_type_fields *f=fields(rt,type);
    CHECK(symbol,rt->s7+schema->offset,"original fixed type symbol offset");
    CHECK(dc_goal_type_of(rt,type),rt->type_type,"fixed type runtime tag");
    CHECK(f->parent,dc_goal_symbol_value(rt,rt->s7+schema->parent),"original fixed type parent");
    CHECK(f->allocated_size,schema->size,"original fixed type allocation size");
    CHECK(f->padded_size,(schema->size+15u)&~15u,"original fixed type padded size");
    CHECK(f->heap_base,schema->heap_base,"original fixed type heap base");
    CHECK(f->num_methods>=schema->methods,1,"fixed type method capacity");
  }
  for (i=0;i<DC_KERNEL_PLACEHOLDER_COUNT;++i)
    CHECK(dc_goal_find(rt,dc_kernel_placeholders[i].name),rt->s7+dc_kernel_placeholders[i].offset,"original fixed asset name offset");
  CHECK(dc_goal_find(rt,"global"),rt->s7+DC_FIX_SYM_GLOBAL_HEAP,"canonical global selector installed");
  CHECK(fields(rt,dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_PROCESS_TYPE))->allocated_size,112,"original process metadata size");
  CHECK(fields(rt,dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_THREAD_TYPE))->num_methods,12,"original thread method capacity");
  CHECK(dc_goal_get_method(rt,dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_THREAD_TYPE),0),
        dc_goal_get_method(rt,rt->basic_type,0),"new fixed types inherit installed basic constructor");
  CHECK(dc_goal_find(rt,"uintger"),rt->s7+DC_FIX_SYM_UINTEGER_TYPE,"original stored unsigned-type spelling retained");
  method=dc_goal_new_basic(rt,rt->function_type,4); old_method=dc_goal_get_method(rt,rt->basic_type,5);
  dc_goal_set_method(rt,rt->basic_type,5,method);
  old_art=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_ART_GROUP);
  old_global=dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_GLOBAL_HEAP);
  dc_goal_set_symbol(rt,rt->s7+DC_FIX_SYM_ART_GROUP,0x12345678u);
  dc_goal_set_symbol(rt,rt->s7+DC_FIX_SYM_GLOBAL_HEAP,0x23456789u);
  used=rt->used; symbols=rt->symbol_count;
  CHECK(dc_goal_install_kernel_types(rt,&result),1,"fixed metadata installation repeats safely");
  CHECK(result.new_types,0,"repeated installation creates no types");
  CHECK(result.arena_bytes,0,"repeated installation consumes no arena bytes");
  CHECK(rt->used,used,"repeat preserves arena cursor");
  CHECK(rt->symbol_count,symbols,"repeat preserves symbol count");
  CHECK(dc_goal_get_method(rt,rt->basic_type,5),method,"repeat preserves installed method overrides");
  CHECK(dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_ART_GROUP),0x12345678u,"repeat preserves initialized asset placeholder value");
  CHECK(dc_goal_symbol_value(rt,rt->s7+DC_FIX_SYM_GLOBAL_HEAP),0x23456789u,"repeat preserves global heap binding");
  dc_goal_set_method(rt,rt->basic_type,5,old_method);
  dc_goal_set_symbol(rt,rt->s7+DC_FIX_SYM_ART_GROUP,old_art);
  dc_goal_set_symbol(rt,rt->s7+DC_FIX_SYM_GLOBAL_HEAP,old_global);
  allocation=malloc(1048608); snapshot=malloc(DC_GOAL_SYMBOL_BYTES);
  CHECK(allocation!=NULL && snapshot!=NULL,1,"fixed namespace failure-test storage");
  if (!allocation || !snapshot) { free(allocation); free(snapshot); return; }
  memory=(unsigned char*)(((uintptr_t)allocation+15u)&~(uintptr_t)15u);
#ifdef __DREAMCAST__
  CHECK(dc_goal_init_native(&failure,memory,1048576),1,"failure-test native runtime");
#else
  CHECK(dc_goal_init(&failure,memory,1048576,0x8c500000u),1,"failure-test host runtime");
#endif
  used=failure.used; symbols=failure.symbol_count;
  memcpy(snapshot,dc_goal_resolve(&failure,failure.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES);
  failure.capacity=used+64u;
  CHECK(dc_goal_install_kernel_types(&failure,&result),0,"fixed type setup OOM after name allocation");
  CHECK(failure.error,DC_GOAL_OUT_OF_MEMORY,"fixed type setup preserves allocation error");
  CHECK(failure.used,used,"fixed type setup OOM rewinds arena");
  CHECK(failure.symbol_count,symbols,"fixed type setup OOM restores symbol count");
  CHECK(memcmp(snapshot,dc_goal_resolve(&failure,failure.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES),0,"fixed type setup OOM restores entire table");
  CHECK(result.types==0 && result.arena_bytes==0,1,"failed fixed metadata result remains empty");
  failure.capacity=1048576;
  method=dc_goal_intern(&failure,"uint128");
  used=failure.used; symbols=failure.symbol_count;
  memcpy(snapshot,dc_goal_resolve(&failure,failure.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES);
  CHECK(dc_goal_install_kernel_types(&failure,&result),0,"late fixed-name collision rejected");
  CHECK(failure.error,DC_GOAL_BAD_ARGUMENT,"fixed name collision explicit error");
  CHECK(failure.used,used,"late name collision rewinds all new type allocations");
  CHECK(failure.symbol_count,symbols,"late name collision restores count");
  CHECK(memcmp(snapshot,dc_goal_resolve(&failure,failure.table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES),0,"late name collision restores entire table");
  CHECK(dc_goal_find(&failure,"uint128"),method,"preexisting dynamic binding retained on collision");
  CHECK(dc_goal_find(&failure,"vu-function"),0,"late failure removes earlier new fixed names");
  fields(&failure,failure.basic_type)->allocated_size=8;
  CHECK(dc_goal_install_kernel_types(&failure,&result),0,"mismatched existing type metadata rejected");
  CHECK(failure.error,DC_GOAL_BAD_TYPE,"existing metadata mismatch explicit error");
  CHECK(fields(&failure,failure.basic_type)->allocated_size,8,"failed setup does not rewrite old type metadata");
  fields(&failure,failure.basic_type)->allocated_size=4;
  free(snapshot); free(allocation);
  CHECK(dc_goal_install_kernel_types(NULL,&result),0,"null fixed metadata runtime rejected");
  CHECK(dc_goal_install_kernel_types(rt,NULL),0,"null fixed metadata result rejected");
#undef CHECK
}
