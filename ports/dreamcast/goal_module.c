/* Box native code as GOAL function objects and bind runtime cells (AI-assisted).
 * Snapshot rollback costs 256 KiB temporarily; resident code lives in the arena.
 * Native scalar initializers run explicitly after linking code/private data.
 * Retail V4 data uses goal_object_load.c; MIPS code/process linking remains open.
 */
#include "goal_module.h"
#include <stdlib.h>
#include <string.h>

enum import_kind { IMPORT_CELL,IMPORT_POINTER,IMPORT_FUNCTION,IMPORT_TYPE,IMPORT_DATA };
static const char *import_name(const char *symbol,enum import_kind *kind) {
  static const char *prefixes[]={"cell:","pointer:","function:","type:","data:"};
  unsigned i;
  if (!symbol) return NULL;
  for (i=0;i<5;++i) {
    size_t length=strlen(prefixes[i]);
    if (!strncmp(symbol,prefixes[i],length) && symbol[length]) {
      *kind=(enum import_kind)i; return symbol+length;
    }
  }
  return NULL;
}
static int valid_relocations(const dc_goal_code_relocation *relocations,uint32_t count,uint32_t bytes) {
  uint32_t j,k;
  if (count>bytes/4u || (count && !relocations)) return 0;
  for (j=0;j<count;++j) {
    const dc_goal_code_relocation *r=&relocations[j];
    enum import_kind kind;
    if ((r->offset&3u) || r->offset>bytes-4u || !import_name(r->symbol,&kind)) return 0;
    for (k=0;k<j;++k) if (relocations[k].offset==r->offset) return 0;
  }
  return 1;
}
static int valid_exports(const dc_goal_code_export *functions,uint32_t count) {
  uint32_t i,j;
  if (!functions || !count || count>DC_GOAL_MAX_SYMBOLS) return 0;
  for (i=0;i<count;++i) {
    const dc_goal_code_export *f=&functions[i];
    if (!f->name || !*f->name || !f->code || f->bytes<2 || (f->bytes&1u) ||
        f->bytes>UINT32_MAX-4u || !valid_relocations(f->relocations,f->relocation_count,f->bytes)) return 0;
    for (j=0;j<i;++j) if (!strcmp(f->name,functions[j].name)) return 0;
  }
  return 1;
}
static int valid_data(const dc_goal_data_export *data,uint32_t count) {
  uint32_t i,j;
  if (count>DC_GOAL_MAX_SYMBOLS || (count && !data)) return 0;
  for (i=0;i<count;++i) {
    if (!data[i].name || !*data[i].name || !data[i].data || !data[i].bytes ||
        data[i].address_offset>=data[i].bytes ||
        !valid_relocations(data[i].relocations,data[i].relocation_count,data[i].bytes)) return 0;
    for (j=0;j<i;++j) if (!strcmp(data[i].name,data[j].name)) return 0;
  }
  return 1;
}
static uint32_t resolve_import(dc_goal_runtime *rt,const char *reference,
                               const dc_goal_data_export *data,const uint32_t *addresses,uint32_t count) {
  enum import_kind kind;
  const char *name=import_name(reference,&kind);
  uint32_t address,i;
  if (!name) goto invalid;
  if (kind==IMPORT_DATA) {
    for (i=0;i<count;++i) if (!strcmp(name,data[i].name)) return addresses[i];
    goto invalid;
  }
  address=(kind==IMPORT_CELL || kind==IMPORT_POINTER)?dc_goal_intern(rt,name):dc_goal_find(rt,name);
  if (!address) {
    if (rt->error!=DC_GOAL_OK) return 0;
    goto invalid;
  }
  if (kind==IMPORT_FUNCTION || kind==IMPORT_TYPE) {
    address=dc_goal_symbol_value(rt,address);
    if (!address || dc_goal_type_of(rt,address)!=(kind==IMPORT_FUNCTION?rt->function_type:rt->type_type) ||
        !dc_goal_resolve(rt,address,2)) goto invalid;
  }
  if (kind==IMPORT_CELL && ((address&3u) || dc_goal_type_of(rt,address)!=rt->symbol_type)) goto invalid;
  return address;
invalid:
  rt->error=DC_GOAL_BAD_MODULE; return 0;
}
int dc_goal_load_module_data(dc_goal_runtime *rt,const dc_goal_code_export *functions,uint32_t count,
                              const dc_goal_data_export *data,uint32_t data_count,
                              dc_goal_cache_sync sync,void *context,dc_goal_module_result *result) {
  dc_goal_runtime saved;
  unsigned char *snapshot;
  uint32_t i,j,total_code=0,total_data=0,*data_addresses;
  if (result) memset(result,0,sizeof(*result));
  if (!rt || !rt->memory || !result || !valid_exports(functions,count) || !valid_data(data,data_count)) {
    if (rt) rt->error=DC_GOAL_BAD_MODULE;
    return 0;
  }
#ifdef __DREAMCAST__
  if (!sync) { rt->error=DC_GOAL_BAD_MODULE; return 0; }
#endif
  if (!dc_goal_resolve(rt,rt->table,DC_GOAL_SYMBOL_BYTES) ||
      dc_goal_type_of(rt,rt->function_type)!=rt->type_type) {
    rt->error=DC_GOAL_BAD_TYPE; return 0;
  }
  saved=*rt;
  snapshot=malloc(DC_GOAL_SYMBOL_BYTES+data_count*4u);
  if (!snapshot) { rt->error=DC_GOAL_OUT_OF_MEMORY; return 0; }
  data_addresses=(uint32_t*)(snapshot+DC_GOAL_SYMBOL_BYTES);
  memcpy(snapshot,dc_goal_resolve(rt,rt->table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES);
  /* Publish every code address before resolving mutual function references. */
  for (i=0;i<count;++i) {
    uint32_t symbol=dc_goal_intern(rt,functions[i].name),function,old;
    if (!symbol) goto rollback;
    old=dc_goal_symbol_value(rt,symbol);
    if (old && dc_goal_type_of(rt,old)!=rt->function_type) {
      rt->error=DC_GOAL_BAD_MODULE; goto rollback;
    }
    function=dc_goal_new_basic(rt,rt->function_type,functions[i].bytes+4u);
    if (!function || !dc_goal_set_symbol(rt,symbol,function)) goto rollback;
    memcpy(dc_goal_resolve(rt,function,functions[i].bytes),functions[i].code,functions[i].bytes);
    total_code+=functions[i].bytes;
  }
  for (i=0;i<data_count;++i) {
    uint32_t allocation=dc_goal_alloc(rt,data[i].bytes);
    if (!allocation) goto rollback;
    data_addresses[i]=allocation+data[i].address_offset;
    memcpy(dc_goal_resolve(rt,allocation,data[i].bytes),data[i].data,data[i].bytes);
    total_data+=data[i].bytes;
  }
  for (i=0;i<count;++i) {
    uint32_t symbol=dc_goal_find(rt,functions[i].name),function;
    if (!symbol) goto rollback;
    function=dc_goal_symbol_value(rt,symbol);
    for (j=0;j<functions[i].relocation_count;++j) {
      const dc_goal_code_relocation *r=&functions[i].relocations[j];
      uint32_t address=resolve_import(rt,r->symbol,data,data_addresses,data_count);
      void *destination;
      if (!address) goto rollback;
      destination=dc_goal_resolve(rt,function+r->offset,4);
      if (!destination) goto rollback;
      memcpy(destination,&address,4);
    }
  }
  for (i=0;i<data_count;++i) for (j=0;j<data[i].relocation_count;++j) {
    const dc_goal_code_relocation *r=&data[i].relocations[j];
    uint32_t address=resolve_import(rt,r->symbol,data,data_addresses,data_count);
    void *destination;
    if (!address) goto rollback;
    destination=dc_goal_resolve(rt,data_addresses[i]-data[i].address_offset+r->offset,4);
    if (!destination) goto rollback;
    memcpy(destination,&address,4);
  }
  /* Cache synchronization follows all relocations; no code executes mid-load. */
  if (sync) for (i=0;i<count;++i) {
    uint32_t function=dc_goal_symbol_value(rt,dc_goal_find(rt,functions[i].name));
    sync(function-4u,functions[i].bytes+4u,context);
  }
  result->functions=count; result->code_bytes=total_code;
  result->arena_bytes=rt->used-saved.used; result->temporary_bytes=DC_GOAL_SYMBOL_BYTES+data_count*4u;
  result->data_objects=data_count; result->data_bytes=total_data;
  free(snapshot); rt->error=DC_GOAL_OK; return 1;
rollback:
  {
    enum dc_goal_error error=rt->error==DC_GOAL_OK?DC_GOAL_BAD_MODULE:rt->error;
    memcpy(saved.memory+(saved.table-saved.base),snapshot,DC_GOAL_SYMBOL_BYTES);
    *rt=saved; rt->error=error;
  }
  free(snapshot); return 0;
}

int dc_goal_load_module(dc_goal_runtime *rt,const dc_goal_code_export *functions,uint32_t count,
                         dc_goal_cache_sync sync,void *context,dc_goal_module_result *result) {
  return dc_goal_load_module_data(rt,functions,count,NULL,0,sync,context,result);
}

static uint32_t initializer_entry(dc_goal_runtime *rt,const char *name) {
  uint32_t symbol,function;
  if (!rt || !name) {
    if (rt) rt->error=DC_GOAL_BAD_ARGUMENT;
    return 0;
  }
  symbol=dc_goal_find(rt,name);
  function=symbol?dc_goal_symbol_value(rt,symbol):0;
  if (!function || dc_goal_type_of(rt,function)!=rt->function_type ||
      !dc_goal_resolve(rt,function,2)) {
    rt->error=DC_GOAL_BAD_MODULE; return 0;
  }
  return function;
}

int dc_goal_run_initializer(dc_goal_runtime *rt,const char *name,uint64_t *value) {
  uint32_t function;
  if (!value) { if (rt) rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  function=initializer_entry(rt,name);
  if (!function) return 0;
#ifdef __DREAMCAST__
  *value=((uint64_t (*)(void))(uintptr_t)function)();
  return 1;
#else
  rt->error=DC_GOAL_BAD_ARGUMENT;
  return 0;
#endif
}

int dc_goal_run_initializer_void(dc_goal_runtime *rt,const char *name) {
  uint32_t function=initializer_entry(rt,name);
  if (!function) return 0;
#ifdef __DREAMCAST__
  ((void (*)(void))(uintptr_t)function)();
  return 1;
#else
  rt->error=DC_GOAL_BAD_ARGUMENT;
  return 0;
#endif
}
