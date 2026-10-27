/* Schema-driven fixed namespace setup, transactional (AI-assisted), ISC. */
#include "goal_kernel_types.h"
#include "kernel_types_data.h"
#include <stdlib.h>
#include <string.h>
static int bind_preserving_value(dc_goal_runtime *rt,uint32_t offset,const char *name) {
  uint32_t symbol=dc_goal_find(rt,name),value=0;
  if (rt->error!=DC_GOAL_OK) return 0;
  if (symbol) value=dc_goal_symbol_value(rt,symbol);
  return dc_goal_bind_fixed(rt,offset,name,value);
}
int dc_goal_install_kernel_types(dc_goal_runtime *rt,dc_kernel_types_result *result) {
  dc_goal_runtime saved;
  unsigned char created[DC_KERNEL_TYPE_COUNT]={0},*snapshot;
  const void *table;
  uint32_t i,new_types=0;
  if (result) memset(result,0,sizeof(*result));
  if (!rt || !result) { if (rt) rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  rt->error=DC_GOAL_OK;
  table=dc_goal_resolve(rt,rt->table,DC_GOAL_SYMBOL_BYTES);
  if (!table) return 0;
  snapshot=malloc(DC_GOAL_SYMBOL_BYTES);
  if (!snapshot) { rt->error=DC_GOAL_OUT_OF_MEMORY; return 0; }
  memcpy(snapshot,table,DC_GOAL_SYMBOL_BYTES); saved=*rt;
  for (i=0;i<DC_KERNEL_TYPE_COUNT;++i) {
    const dc_kernel_type_schema *schema=&dc_kernel_types[i];
    uint32_t symbol,type;
    if (!bind_preserving_value(rt,schema->offset,schema->name)) goto rollback;
    symbol=rt->s7+schema->offset; type=dc_goal_symbol_value(rt,symbol);
    if (type) {
      dc_goal_type_fields *fields;
      if (dc_goal_type_of(rt,type)!=rt->type_type) { rt->error=DC_GOAL_BAD_TYPE; goto rollback; }
      fields=dc_goal_resolve(rt,type,sizeof(*fields));
      if (!fields || fields->symbol!=symbol || fields->num_methods<schema->methods || fields->num_methods>=127 ||
          fields->allocated_size!=schema->size || fields->padded_size!=((schema->size+15u)&~15u) ||
          fields->heap_base!=schema->heap_base ||
          fields->parent!=dc_goal_symbol_value(rt,rt->s7+schema->parent) ||
          !dc_goal_resolve(rt,type,16u+4u*fields->num_methods)) {
        rt->error=DC_GOAL_BAD_TYPE; goto rollback;
      }
    } else {
      if (!dc_goal_intern_type(rt,schema->name,schema->methods)) goto rollback;
      created[i]=1; ++new_types;
    }
  }
  /* New type blocks only are mutated below; existing objects are validated and
   * left untouched, so the table snapshot plus arena rewind is sufficient. */
  for (i=0;i<DC_KERNEL_TYPE_COUNT;++i) if (created[i]) {
    const dc_kernel_type_schema *schema=&dc_kernel_types[i];
    uint32_t parent=dc_goal_symbol_value(rt,rt->s7+schema->parent);
    uint64_t flags=(uint64_t)schema->size|((uint64_t)schema->heap_base<<16)|((uint64_t)schema->methods<<32);
    if (!dc_goal_new_type(rt,rt->s7+schema->offset,parent,flags)) goto rollback;
  }
  for (i=0;i<DC_KERNEL_PLACEHOLDER_COUNT;++i)
    if (!bind_preserving_value(rt,dc_kernel_placeholders[i].offset,dc_kernel_placeholders[i].name)) goto rollback;
  if (!bind_preserving_value(rt,DC_FIX_SYM_GLOBAL_HEAP,"global")) goto rollback;
  /* kscheme set_fixed_symbol(FIX_SYM_DEBUG_HEAP,"debug",kdebugheap): no debug
   * segment on Dreamcast, so its kheap stays null (malloc falls back to global). */
  if (!bind_preserving_value(rt,DC_FIX_SYM_DEBUG_HEAP,"debug")) goto rollback;
  result->types=DC_KERNEL_TYPE_COUNT; result->new_types=new_types;
  result->placeholders=DC_KERNEL_PLACEHOLDER_COUNT; result->arena_bytes=rt->used-saved.used;
  result->temporary_bytes=DC_GOAL_SYMBOL_BYTES;
  free(snapshot); rt->error=DC_GOAL_OK; return 1;
rollback:
  {
    enum dc_goal_error error=rt->error==DC_GOAL_OK?DC_GOAL_BAD_TYPE:rt->error;
    memcpy(saved.memory+(saved.table-saved.base),snapshot,DC_GOAL_SYMBOL_BYTES);
    *rt=saved; rt->error=error;
  }
  free(snapshot); return 0;
}
