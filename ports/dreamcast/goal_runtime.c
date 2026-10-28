/* Jak 1 layout-preserving native runtime foundation (AI-assisted), ISC.
 * Layout/hash semantics: game/kernel/{common,jak1}/kscheme.cpp.
 * Global bump allocation only. Process heaps, top allocation, methods and
 * original object linking still need integration; no dummy methods are bound.
 */
#include "goal_runtime.h"
#include <limits.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error The native Dreamcast runtime requires little-endian memory
#endif

static uint32_t fail(dc_goal_runtime *rt,enum dc_goal_error error) {
  if (rt) rt->error=error;
  return 0;
}
void *dc_goal_resolve(dc_goal_runtime *rt,uint32_t address,uint32_t size) {
  uint32_t offset,low;
  if (!rt || !rt->memory || rt->temporary>rt->capacity || rt->used>rt->capacity-rt->temporary ||
      address<rt->base) { fail(rt,DC_GOAL_BOUNDS); return NULL; }
  offset=address-rt->base;
  if (offset<=rt->used && size<=rt->used-offset) return rt->memory+offset;
  low=rt->capacity-rt->temporary;  /* the temporary region, [low, capacity) */
  if (rt->temporary && offset>=low && offset<=rt->capacity && size<=rt->capacity-offset)
    return rt->memory+offset;
  fail(rt,DC_GOAL_BOUNDS); return NULL;
}
static int read32(dc_goal_runtime *rt,uint32_t at,uint32_t *value) {
  void *p=dc_goal_resolve(rt,at,4);
  if (!p) return 0;
  memcpy(value,p,4); return 1;
}
static int write32(dc_goal_runtime *rt,uint32_t at,uint32_t value) {
  void *p=dc_goal_resolve(rt,at,4);
  if (!p) return 0;
  memcpy(p,&value,4); return 1;
}
enum dc_goal_error dc_goal_sticky_error=DC_GOAL_OK;
uint32_t dc_goal_error_site;
uint32_t dc_goal_last_failed_alloc;
uint32_t dc_goal_alloc(dc_goal_runtime *rt,uint32_t bytes) {
  uint32_t size,address;
  if (!rt || !rt->memory || !bytes || bytes>UINT32_MAX-15u)
    return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  size=(bytes+15u)&~15u;
  if (rt->temporary>rt->capacity || rt->used>rt->capacity-rt->temporary ||
      size>rt->capacity-rt->temporary-rt->used) {
    dc_goal_last_failed_alloc=size;
    return fail(rt,DC_GOAL_OUT_OF_MEMORY);
  }
  address=rt->base+rt->used;
  memset(rt->memory+rt->used,0,size); rt->used+=size;
  return address;
}
uint32_t dc_goal_alloc_temporary(dc_goal_runtime *rt,uint32_t bytes) {
  uint32_t size;
  if (!rt || !rt->memory || !bytes || bytes>UINT32_MAX-15u || (rt->capacity&15u))
    return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  size=(bytes+15u)&~15u;
  if (rt->temporary>rt->capacity || rt->used>rt->capacity-rt->temporary ||
      size>rt->capacity-rt->temporary-rt->used) {
    dc_goal_last_failed_alloc=size;
    return fail(rt,DC_GOAL_OUT_OF_MEMORY);
  }
  rt->temporary+=size;
  memset(rt->memory+(rt->capacity-rt->temporary),0,size);
  return rt->base+(rt->capacity-rt->temporary);
}
void dc_goal_release_temporary(dc_goal_runtime *rt) {
  if (!rt) return;
  if (rt->temporary_symbol) dc_goal_set_symbol(rt,rt->temporary_symbol,rt->s7);
  rt->temporary=rt->temporary_entry=rt->temporary_bytes=rt->temporary_symbol=0;
}
uint32_t dc_goal_hash(const char *name) {
  uint32_t crc=0;
  if (!name) return 0;
  while (*name) {
    uint32_t n=crc&0xff000000u;
    unsigned i;
    for (i=0;i<8;++i) n=(n<<1)^((n&0x80000000u)?0x04c11db7u:0);
    crc=n^((crc<<8)|(unsigned char)*name++);
  }
  return ~crc;
}
static dc_goal_type_fields *type_fields(dc_goal_runtime *rt,uint32_t type) {
  uint32_t tag;
  dc_goal_type_fields *fields;
  if ((type&15u)!=DC_GOAL_BASIC_OFFSET || !read32(rt,type-4,&tag) || tag!=rt->type_type) {
    fail(rt,DC_GOAL_BAD_TYPE); return NULL;
  }
  fields=dc_goal_resolve(rt,type,sizeof(*fields));
  if (!fields || !fields->num_methods || fields->num_methods>=127 ||
      !dc_goal_resolve(rt,type,16u+4u*fields->num_methods)) {
    fail(rt,DC_GOAL_BAD_TYPE); return NULL;
  }
  return fields;
}
static int symbol_address(dc_goal_runtime *rt,uint32_t symbol) {
  uint32_t offset,header,hash;
  if (!rt || symbol<rt->table+4u || (symbol-rt->table)%8u!=4u) return 0;
  offset=symbol-rt->table;
  if (offset>=(DC_GOAL_MAX_SYMBOLS-32u)*8u ||
      (symbol>=rt->s7-16u && symbol<rt->s7)) return 0;
  return read32(rt,symbol-4,&header) && header==rt->symbol_type &&
         read32(rt,symbol+DC_GOAL_SYM_INFO_OFFSET,&hash) && hash!=0;
}
uint32_t dc_goal_symbol_value(dc_goal_runtime *rt,uint32_t symbol) {
  uint32_t value=0;
  if (!symbol_address(rt,symbol)) return fail(rt,DC_GOAL_BAD_OBJECT);
  if (!read32(rt,symbol,&value)) return 0;
  return value;
}
int dc_goal_set_symbol(dc_goal_runtime *rt,uint32_t symbol,uint32_t value) {
  if (!symbol_address(rt,symbol)) { fail(rt,DC_GOAL_BAD_OBJECT); return 0; }
  return write32(rt,symbol,value);
}
static const char *string_data(dc_goal_runtime *rt,uint32_t string) {
  uint32_t header,length;
  const char *text;
  if ((string&15u)!=4 || !read32(rt,string-4,&header) || header!=rt->string_type ||
      !read32(rt,string,&length) || length==UINT32_MAX) {
    fail(rt,DC_GOAL_BAD_OBJECT); return NULL;
  }
  text=dc_goal_resolve(rt,string+4,length+1);
  if (!text || text[length]!=0) { fail(rt,DC_GOAL_BAD_OBJECT); return NULL; }
  return text;
}
const char *dc_goal_symbol_name(dc_goal_runtime *rt,uint32_t symbol) {
  uint32_t string;
  if (!symbol_address(rt,symbol)) { fail(rt,DC_GOAL_BAD_OBJECT); return NULL; }
  if (!read32(rt,symbol+DC_GOAL_SYM_TO_STRING_OFFSET,&string)) return NULL;
  return string_data(rt,string);
}
uint32_t dc_goal_new_basic(dc_goal_runtime *rt,uint32_t type,uint32_t bytes) {
  uint32_t memory;
  dc_goal_type_fields *fields=type_fields(rt,type);
  if (!fields || bytes<4 || bytes<fields->allocated_size) return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  memory=dc_goal_alloc(rt,bytes);
  if (!memory) return 0;
  write32(rt,memory,type); return memory+4;
}
uint32_t dc_goal_string(dc_goal_runtime *rt,const char *text) {
  size_t length;
  uint32_t bytes,string;
  if (!rt || !rt->memory || !text) return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  length=strlen(text);
  if (length>UINT32_MAX-9u) return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  bytes=(uint32_t)length+1;
  if (bytes<8) bytes=8;
  string=dc_goal_new_basic(rt,rt->string_type,bytes+8);
  if (!string) return 0;
  write32(rt,string,(uint32_t)length);
  memcpy(dc_goal_resolve(rt,string+4,(uint32_t)length+1),text,length+1);
  return string;
}
static int set_slot(dc_goal_runtime *rt,uint32_t symbol,const char *name,uint32_t value) {
  uint32_t hash=dc_goal_hash(name),string;
  if (!hash) { fail(rt,DC_GOAL_BAD_HASH); return 0; }
  string=dc_goal_string(rt,name);
  if (!string) return 0;
  write32(rt,symbol-4,rt->symbol_type); write32(rt,symbol,value);
  write32(rt,symbol+DC_GOAL_SYM_INFO_OFFSET,hash);
  write32(rt,symbol+DC_GOAL_SYM_TO_STRING_OFFSET,string);
  ++rt->symbol_count; return 1;
}
static uint32_t search_area(dc_goal_runtime *rt,uint32_t hash,const char *name,
                             uint32_t start,uint32_t end,uint32_t *free_slot,int *valid) {
  uint32_t at;
  for (at=start;at<end;at+=8) {
    uint32_t existing;
    if (!read32(rt,at+DC_GOAL_SYM_INFO_OFFSET,&existing)) { *valid=0; return 0; }
    if (!existing) { *free_slot=at; return 0; }
    if (existing==hash) {
      const char *old=dc_goal_symbol_name(rt,at);
      if (!old) { *valid=0; return 0; }
      if (!strcmp(old,name)) return at;
    }
  }
  return 0;
}
static uint32_t find_slot(dc_goal_runtime *rt,const char *name,uint32_t *free_slot) {
  uint32_t hash,at,result,last;
  int32_t bucket;
  int valid=1;
  *free_slot=0;
  if (!rt || !rt->table || !name) return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  hash=dc_goal_hash(name);
  if (!hash) return fail(rt,DC_GOAL_BAD_HASH);
  if (hash==0x8454b6e6u && !strcmp(name,"_empty_")) return rt->s7-10u;
  /* Reserve all fixed slots, including fixed primitives not installed yet. */
  for (at=rt->s7;at<rt->s7+DC_FIX_FIXED_SYM_END_OFFSET;at+=8) {
    uint32_t existing;
    if (!read32(rt,at+DC_GOAL_SYM_INFO_OFFSET,&existing)) return 0;
    if (existing==hash) {
      const char *old=dc_goal_symbol_name(rt,at);
      if (!old) return 0;
      if (!strcmp(old,name)) return at;
    }
  }
  bucket=(int32_t)(hash&(DC_GOAL_MAX_SYMBOLS-1u));
  if (bucket>=(int32_t)(DC_GOAL_MAX_SYMBOLS/2u)) bucket-=(int32_t)DC_GOAL_MAX_SYMBOLS;
  last=rt->table+(DC_GOAL_MAX_SYMBOLS-32u)*8u;
  if (bucket>0) {
    uint32_t first=rt->s7+(uint32_t)bucket*8u;
    if (first<rt->s7+DC_FIX_FIXED_SYM_END_OFFSET) first=rt->s7+DC_FIX_FIXED_SYM_END_OFFSET;
    result=search_area(rt,hash,name,first,last,free_slot,&valid);
    if (result || *free_slot || !valid) return result;
    return search_area(rt,hash,name,rt->table+4,rt->s7-16u,free_slot,&valid);
  }
  result=search_area(rt,hash,name,(uint32_t)((int64_t)rt->s7+bucket*8),rt->s7-16u,free_slot,&valid);
  if (result || *free_slot || !valid) return result;
  return search_area(rt,hash,name,rt->s7+DC_FIX_FIXED_SYM_END_OFFSET,last,free_slot,&valid);
}
int dc_goal_bind_fixed(dc_goal_runtime *rt,uint32_t offset,const char *name,uint32_t value) {
  uint32_t symbol,existing,hash;
  if (!rt || !name || !*name || (offset&7u) || offset>=DC_FIX_FIXED_SYM_END_OFFSET) {
    (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT)); return 0;
  }
  symbol=rt->s7+offset;
  existing=dc_goal_find(rt,name);
  if (rt->error!=DC_GOAL_OK || (existing && existing!=symbol) ||
      !read32(rt,symbol+DC_GOAL_SYM_INFO_OFFSET,&hash)) {
    (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT)); return 0;
  }
  if (hash) {
    const char *old=dc_goal_symbol_name(rt,symbol);
    if (!old || strcmp(old,name)) { (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT)); return 0; }
    return dc_goal_set_symbol(rt,symbol,value);
  }
  return set_slot(rt,symbol,name,value);
}
uint32_t dc_goal_find(dc_goal_runtime *rt,const char *name) {
  uint32_t slot;
  if (rt) rt->error=DC_GOAL_OK;
  return find_slot(rt,name,&slot);
}
uint32_t dc_goal_intern(dc_goal_runtime *rt,const char *name) {
  uint32_t slot,result;
  if (rt) rt->error=DC_GOAL_OK;
  result=find_slot(rt,name,&slot);
  if (result) return result;
  if (rt && rt->error!=DC_GOAL_OK) return 0;
  if (!slot) return fail(rt,DC_GOAL_SYMBOL_TABLE_FULL);
  if (!set_slot(rt,slot,name,0)) return 0;
  return slot;
}
uint32_t dc_goal_new_pair(dc_goal_runtime *rt,uint32_t car,uint32_t cdr) {
  uint32_t memory=dc_goal_alloc(rt,8);
  if (!memory) return 0;
  write32(rt,memory,car); write32(rt,memory+4,cdr);
  return memory+2;
}
uint32_t dc_goal_type_of(dc_goal_runtime *rt,uint32_t value) {
  uint32_t tag;
  if (!rt || !rt->memory) return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  switch(value&7u) {
    case 0: return rt->binteger_type;
    case 2:
      if (!dc_goal_resolve(rt,value-2,8)) return fail(rt,DC_GOAL_BAD_OBJECT);
      return rt->pair_type;
    case 4:
      if (!read32(rt,value-4,&tag) || !type_fields(rt,tag)) return fail(rt,DC_GOAL_BAD_OBJECT);
      return tag;
    default: return fail(rt,DC_GOAL_BAD_OBJECT);
  }
}
int dc_goal_set_method(dc_goal_runtime *rt,uint32_t type,uint16_t index,uint32_t function) {
  dc_goal_type_fields *fields=type_fields(rt,type);
  if (!fields || index>=fields->num_methods) { fail(rt,DC_GOAL_METHOD_CAPACITY); return 0; }
  return write32(rt,type+16u+index*4u,function);
}
uint32_t dc_goal_get_method(dc_goal_runtime *rt,uint32_t type,uint16_t index) {
  dc_goal_type_fields *fields=type_fields(rt,type);
  uint32_t value=0;
  if (!fields || index>=fields->num_methods) return fail(rt,DC_GOAL_METHOD_CAPACITY);
  read32(rt,type+16u+index*4u,&value); return value;
}
int dc_goal_is_subtype(dc_goal_runtime *rt,uint32_t type,uint32_t ancestor) {
  uint32_t depth;
  if (!type_fields(rt,ancestor)) return 0;
  for (depth=0;depth<DC_GOAL_MAX_SYMBOLS;++depth) {
    dc_goal_type_fields *fields=type_fields(rt,type);
    if (!fields) return 0;
    if (type==ancestor) return 1;
    if (type==rt->object_type || !fields->parent) return 0;
    type=fields->parent;
  }
  fail(rt,DC_GOAL_BAD_TYPE); return 0;
}
uint32_t dc_goal_define_type(dc_goal_runtime *rt,const char *name,uint32_t parent,
                             uint16_t bytes,uint16_t heap_base,uint16_t methods) {
  dc_goal_type_fields *source=type_fields(rt,parent),*fields;
  uint32_t symbol,type,memory;
  uint16_t i;
  if (!source || !name || !methods || methods>=127 || bytes>65520u)
    return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  symbol=dc_goal_intern(rt,name);
  if (!symbol) return 0;
  type=dc_goal_symbol_value(rt,symbol);
  if (type) {
    fields=type_fields(rt,type);
    if (!fields) return 0;
    if (methods>fields->num_methods) return fail(rt,DC_GOAL_METHOD_CAPACITY);
    if (fields->parent!=parent || fields->allocated_size!=bytes || fields->heap_base!=heap_base)
      return fail(rt,DC_GOAL_BAD_TYPE);
    return type;
  }
  memory=dc_goal_alloc(rt,(4u*methods+0x23u)&~15u);
  if (!memory) return 0;
  write32(rt,memory,rt->type_type); type=memory+4;
  fields=dc_goal_resolve(rt,type,sizeof(*fields));
  fields->symbol=symbol; fields->parent=parent; fields->allocated_size=bytes;
  fields->padded_size=(uint16_t)((bytes+15u)&~15u); fields->heap_base=heap_base;
  fields->num_methods=methods;
  /* Preserve defined inherited slots; never read beyond the parent table. */
  for (i=0;i<methods && i<source->num_methods;++i)
    write32(rt,type+16u+i*4u,dc_goal_get_method(rt,parent,i));
  dc_goal_set_symbol(rt,symbol,type); return type;
}
uint32_t dc_goal_intern_type(dc_goal_runtime *rt,const char *name,uint16_t methods) {
  uint32_t symbol,type,memory;
  uint16_t count=methods==0?12u:(methods==1?44u:methods);
  dc_goal_type_fields *fields;
  if (!rt || !name || !*name || methods>=127) return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  symbol=dc_goal_intern(rt,name);
  if (!symbol) return 0;
  type=dc_goal_symbol_value(rt,symbol);
  if (type) {
    fields=type_fields(rt,type);
    if (!fields) return 0;
    if (methods>1 && methods>fields->num_methods) return fail(rt,DC_GOAL_METHOD_CAPACITY);
    return type;
  }
  memory=dc_goal_alloc(rt,(4u*count+0x23u)&~15u);
  if (!memory) return 0;
  write32(rt,memory,rt->type_type); type=memory+4;
  fields=dc_goal_resolve(rt,type,sizeof(*fields));
  fields->symbol=symbol; fields->num_methods=count;
  dc_goal_set_symbol(rt,symbol,type); return type;
}

uint32_t dc_goal_new_type(dc_goal_runtime *rt,uint32_t symbol,uint32_t parent,uint64_t flags) {
  dc_goal_type_fields *source,*fields;
  const char *name;
  uint32_t type;
  uint16_t i,methods=(uint16_t)(flags>>32),bytes=(uint16_t)flags;
  if (!rt) return 0;
  rt->error=DC_GOAL_OK;
  source=type_fields(rt,parent);
  name=dc_goal_symbol_name(rt,symbol);
  if (!source || !name) return 0;
  if (!methods) methods=12;
  if (methods>=127 || bytes>65520u) return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  type=dc_goal_symbol_value(rt,symbol);
  if (type) {
    fields=type_fields(rt,type);
    if (!fields) return 0;
    if (methods>fields->num_methods) return fail(rt,DC_GOAL_METHOD_CAPACITY);
    if (dc_goal_is_subtype(rt,parent,type)) return fail(rt,DC_GOAL_BAD_TYPE);
    if (rt->error!=DC_GOAL_OK) return 0;
  } else {
    type=dc_goal_define_type(rt,name,parent,bytes,(uint16_t)(flags>>16),methods);
    if (!type) return 0;
    fields=type_fields(rt,type);
  }
  fields->parent=parent; fields->allocated_size=bytes;
  fields->padded_size=(uint16_t)((bytes+15u)&~15u); fields->heap_base=(uint16_t)(flags>>16);
  /* Unlike the original out-of-bounds child-count copy, bound parent reads. */
  for (i=0;i<methods;++i)
    write32(rt,type+16u+i*4u,i<source->num_methods?dc_goal_get_method(rt,parent,i):0);
  return type;
}

uint32_t dc_goal_method_set(dc_goal_runtime *rt,uint32_t type,uint32_t index,
                            uint32_t method,int propagate) {
  dc_goal_type_fields *fields;
  uint32_t previous,pass,i;
  if (!rt) return 0;
  rt->error=DC_GOAL_OK;
  fields=type_fields(rt,type);
  if (!fields) return 0;
  if (index>=fields->num_methods) return fail(rt,DC_GOAL_METHOD_CAPACITY);
  previous=dc_goal_get_method(rt,type,(uint16_t)index);
  if (!method) return 0;
  if (method==1) method=0;
  else if (method==2) {
    method=dc_goal_get_method(rt,fields->parent,(uint16_t)index);
    if (rt->error!=DC_GOAL_OK) return 0;
  } else if (dc_goal_type_of(rt,method)!=rt->function_type) {
    /* A virtual state (defstate on a process type) installs a state object in
     * the method table; desktop method_set stores any value. Accept exactly an
     * instance of the GOAL state type, once gstate has defined it. */
    uint32_t state_symbol=dc_goal_find(rt,"state"),state_type,type;
    state_type=state_symbol?dc_goal_symbol_value(rt,state_symbol):0;
    type=dc_goal_type_of(rt,method);
    rt->error=DC_GOAL_OK;
    if (!state_type || !type || dc_goal_type_of(rt,state_type)!=rt->type_type ||
        !dc_goal_is_subtype(rt,type,state_type)) return (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT));
  }
  if (propagate) for (pass=0;pass<2;++pass) {
    /* Validate the full update before mutating. No allocations or scheduling
     * occur between passes. Only installed symbol-bound types participate. */
    for (i=0;i<DC_GOAL_MAX_SYMBOLS-32u;++i) {
      uint32_t symbol=rt->table+i*8u+4u,candidate,tag;
      dc_goal_type_fields *child;
      if (!symbol_address(rt,symbol)) continue;
      if (!read32(rt,symbol,&candidate)) return 0;
      if (candidate==type || (candidate&15u)!=4u || candidate<rt->base+4u ||
          candidate-rt->base>rt->used) continue;
      if (!read32(rt,candidate-4u,&tag) || tag!=rt->type_type) continue;
      child=type_fields(rt,candidate);
      if (!child) return 0;
      if (index>=child->num_methods || dc_goal_get_method(rt,candidate,(uint16_t)index)!=previous) continue;
      if (!dc_goal_is_subtype(rt,candidate,type)) {
        if (rt->error!=DC_GOAL_OK) return 0;
        continue;
      }
      if (pass) write32(rt,candidate+16u+index*4u,method);
    }
  }
  write32(rt,type+16u+index*4u,method);
  return method;
}

int dc_goal_init(dc_goal_runtime *rt,void *memory,uint32_t capacity,uint32_t virtual_base) {
  unsigned i;
  if (!rt) return 0;
  struct boot_type { uint32_t *value; int offset; const char *name; uint32_t *parent; uint16_t bytes; };
  struct boot_type types[]={
    {&rt->object_type,DC_FIX_SYM_OBJECT_TYPE,"object",&rt->object_type,4},
    {&rt->structure_type,DC_FIX_SYM_STRUCTURE_TYPE,"structure",&rt->object_type,4},
    {&rt->basic_type,DC_FIX_SYM_BASIC_TYPE,"basic",&rt->structure_type,4},
    {&rt->type_type,DC_FIX_SYM_TYPE_TYPE,"type",&rt->basic_type,0x38},
    {&rt->symbol_type,DC_FIX_SYM_SYMBOL_TYPE,"symbol",&rt->basic_type,8},
    {&rt->string_type,DC_FIX_SYM_STRING_TYPE,"string",&rt->basic_type,8},
    {&rt->function_type,DC_FIX_SYM_FUNCTION_TYPE,"function",&rt->basic_type,4},
    {&rt->pair_type,DC_FIX_SYM_PAIR_TYPE,"pair",&rt->object_type,8},
    {&rt->number_type,DC_FIX_SYM_NUMBER_TYPE,"number",&rt->object_type,8},
    {&rt->integer_type,DC_FIX_SYM_INTEGER_TYPE,"integer",&rt->number_type,8},
    {&rt->binteger_type,DC_FIX_SYM_BINTEGER_TYPE,"binteger",&rt->integer_type,8}
  };
  memset(rt,0,sizeof(*rt));
  if (!memory || ((uintptr_t)memory&15u) || !virtual_base || (virtual_base&15u) ||
      capacity>UINT32_MAX-virtual_base) { (DC_GOAL_NOTE_SITE(2),fail(rt,DC_GOAL_BAD_ARGUMENT)); return 0; }
  rt->memory=memory; rt->base=virtual_base; rt->capacity=capacity;
  rt->table=dc_goal_alloc(rt,DC_GOAL_SYMBOL_BYTES);
  if (!rt->table) return 0;
  rt->s7=rt->table+(DC_GOAL_MAX_SYMBOLS/2u)*8u+4u;
  write32(rt,rt->s7-12u,rt->s7-10u); write32(rt,rt->s7-8u,rt->s7-10u);
  for (i=0;i<sizeof(types)/sizeof(types[0]);++i) {
    uint32_t allocation=dc_goal_alloc(rt,64);
    if (!allocation) return 0;
    *types[i].value=allocation+4;
  }
  for (i=0;i<sizeof(types)/sizeof(types[0]);++i) {
    dc_goal_type_fields *fields=dc_goal_resolve(rt,*types[i].value,sizeof(*fields));
    write32(rt,*types[i].value-4,rt->type_type);
    fields->symbol=rt->s7+(uint32_t)types[i].offset; fields->parent=*types[i].parent;
    fields->allocated_size=types[i].bytes; fields->padded_size=(uint16_t)((types[i].bytes+15u)&~15u);
    fields->num_methods=9;
  }
  for (i=0;i<sizeof(types)/sizeof(types[0]);++i)
    if (!set_slot(rt,rt->s7+(uint32_t)types[i].offset,types[i].name,*types[i].value)) return 0;
  if (!set_slot(rt,rt->s7,"#f",rt->s7) || !set_slot(rt,rt->s7+8,"#t",rt->s7+8)) return 0;
  return 1;
}
