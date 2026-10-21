/* Bounded V4 data loader, no code execution (AI-assisted), ISC. */
#include "goal_object_load.h"
#include <stdlib.h>
#include <string.h>
typedef struct { dc_goal_runtime *rt; uint32_t data; } load_context;
static int apply(const dc_goal_object_relocation *r,void *context) {
  load_context *load=context;
  dc_goal_runtime *rt=load->rt;
  uint32_t address=0;
  void *destination;
  switch (r->kind) {
    case DC_OBJECT_INTERNAL: address=load->data+r->target; break;
    case DC_OBJECT_SYMBOL: address=dc_goal_intern(rt,r->name); break;
    case DC_OBJECT_TYPE: address=dc_goal_intern_type(rt,r->name,r->method_hint?r->method_hint:1); break;
    case DC_OBJECT_EMPTY: address=rt->s7-10u; break;
    default: rt->error=DC_GOAL_BAD_MODULE; return 0;
  }
  if (!address) return 0;
  destination=dc_goal_resolve(rt,load->data+r->offset,4);
  if (!destination) return 0;
  memcpy(destination,&address,4); return 1;
}
int dc_goal_load_data_object(dc_goal_runtime *rt,const void *source,uint32_t bytes,dc_goal_data_result *result) {
  dc_goal_object_layout layout,second;
  dc_goal_runtime saved;
  load_context load;
  unsigned char *snapshot;
  const void *table;
  const char *error;
  if (result) memset(result,0,sizeof(*result));
  if (!rt || !result || !source) { if (rt) rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  rt->error=DC_GOAL_OK;
  error=dc_goal_object_scan(source,bytes,NULL,NULL,&layout);
  if (error || layout.version!=4 || layout.data_bytes>DC_GOAL_DATA_MAX_BYTES) {
    rt->error=error && !strcmp(error,"object scan allocation failed")?DC_GOAL_OUT_OF_MEMORY:DC_GOAL_BAD_MODULE;
    return 0;
  }
  table=dc_goal_resolve(rt,rt->table,DC_GOAL_SYMBOL_BYTES);
  if (!table) return 0;
  snapshot=malloc(DC_GOAL_SYMBOL_BYTES);
  if (!snapshot) { rt->error=DC_GOAL_OUT_OF_MEMORY; return 0; }
  memcpy(snapshot,table,DC_GOAL_SYMBOL_BYTES); saved=*rt;
  load.rt=rt; load.data=dc_goal_alloc(rt,layout.data_bytes);
  if (load.data) {
    memcpy(dc_goal_resolve(rt,load.data,layout.data_bytes),(const unsigned char*)source+layout.data_offset,layout.data_bytes);
    error=dc_goal_object_scan(source,bytes,apply,&load,&second);
    if (!error) {
      result->data=load.data; result->bytes=layout.data_bytes;
      result->arena_bytes=rt->used-saved.used;
      result->temporary_bytes=DC_GOAL_SYMBOL_BYTES+layout.bitmap_bytes;
      result->imports=layout.imports;
      result->relocations=layout.internal_relocations+layout.symbol_relocations+
                          layout.type_relocations+layout.empty_relocations;
      free(snapshot); rt->error=DC_GOAL_OK; return 1;
    }
    if (rt->error==DC_GOAL_OK) rt->error=!strcmp(error,"object scan allocation failed")?
                                        DC_GOAL_OUT_OF_MEMORY:DC_GOAL_BAD_MODULE;
  }
  {
    enum dc_goal_error failure=rt->error;
    memcpy(saved.memory+(saved.table-saved.base),snapshot,DC_GOAL_SYMBOL_BYTES);
    *rt=saved; rt->error=failure;
  }
  free(snapshot); return 0;
}
