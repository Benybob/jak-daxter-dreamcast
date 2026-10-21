/* Shared transactional original data loading tests (AI-assisted), ISC. */
#include "goal_object_load.h"
#include "object_load_cases.h"
#ifdef __DREAMCAST__
#include "build/object_fixture.h"
#else
#include "build/object_fixture_host.h"
#endif
#include <stdlib.h>
#include <string.h>
static uint32_t word(dc_goal_runtime *rt,uint32_t at) {
  uint32_t value=0; void *p=dc_goal_resolve(rt,at,4);
  if (p) memcpy(&value,p,4);
  return value;
}
static dc_goal_type_fields *fields(dc_goal_runtime *rt,uint32_t type) {
  return dc_goal_resolve(rt,type,sizeof(dc_goal_type_fields));
}
static void put32(unsigned char *p,uint32_t value) {
  unsigned i; for (i=0;i<4;++i) p[i]=(unsigned char)(value>>(i*8u));
}
#if DC_OBJECT_HAS_ART
typedef struct {
  dc_goal_runtime *rt; dc_runtime_check check; void *context;
  unsigned *id; uint32_t data,visits;
  unsigned char patched[284];
} art_check_context;
static int check_art_relocation(const dc_goal_object_relocation *r,void *context) {
  art_check_context *c=context;
  uint32_t expected=0,symbol;
  if (r->kind==DC_OBJECT_INTERNAL) expected=c->data+r->target;
  else if (r->kind==DC_OBJECT_EMPTY) expected=c->rt->s7-10u;
  else {
    symbol=dc_goal_find(c->rt,r->name);
    expected=r->kind==DC_OBJECT_TYPE?dc_goal_symbol_value(c->rt,symbol):symbol;
  }
  c->check(word(c->rt,c->data+r->offset),expected,++*c->id,"retail art relocation uses correct native address",c->context);
  c->patched[r->offset/4u]=1; ++c->visits;
  return 1;
}
#endif
void dc_object_load_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id) {
  dc_goal_data_result r,second;
  uint32_t forward,used,symbols,capacity,taken;
  unsigned char *table=malloc(DC_GOAL_SYMBOL_BYTES),bad[sizeof(object_v4)];
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  if (!table) { CHECK(0,1,"data test symbol snapshot allocation"); return; }
  CHECK(dc_goal_bind_fixed(rt,DC_FIX_SYM_GLOBAL_HEAP,"global",0),1,"data imports use canonical global symbol slot");
  CHECK(dc_goal_load_data_object(rt,object_v4,sizeof(object_v4),&r),1,"original V4 data load");
  if (!r.data) { free(table); return; }
  CHECK(r.bytes,64,"loaded data bytes");
  CHECK(r.imports,3,"loaded original import groups");
  CHECK(r.relocations,4,"all original pointer/import patches applied");
  CHECK(r.temporary_bytes,262146,"data snapshot plus validation bitmap");
  CHECK(word(rt,r.data),rt->basic_type,"type import resolves existing native type");
  CHECK(word(rt,r.data+4),rt->s7+DC_FIX_SYM_GLOBAL_HEAP,"symbol import resolves canonical native address");
  CHECK(word(rt,r.data+8),rt->s7-10u,"original empty-list import");
  CHECK(word(rt,r.data+12),r.data+20u,"internal pointer rebased to allocated native data");
  CHECK(dc_goal_type_of(rt,r.data+4),rt->basic_type,"loaded data retains original basic tag layout");
  CHECK(object_v4[16]==255 && object_v4[17]==255,1,"source placeholders remain unchanged");
  CHECK(dc_goal_load_data_object(rt,object_v4,sizeof(object_v4),&second),1,"same original object loads at another address");
  CHECK(second.data!=r.data,1,"independent data allocations");
  CHECK(word(rt,second.data+4),word(rt,r.data+4),"repeated imports retain intern identity");
  memcpy(bad,object_v4,sizeof(bad)); bad[28]=64;
  CHECK(dc_goal_load_data_object(rt,bad,sizeof(bad),&second),1,"original one-past pointer loads without dereferencing it");
  CHECK(word(rt,second.data+12),second.data+second.bytes,"one-past native pointer retains exact segment boundary");
  forward=dc_goal_intern_type(rt,"dc-load-forward",1);
  CHECK(forward!=0,1,"forward type allocated for original unknown-count hint");
  CHECK(dc_goal_type_of(rt,forward),rt->type_type,"forward type object tag");
  CHECK(fields(rt,forward)->num_methods,44,"original unknown-count hint allocates forty-four slots");
  CHECK(fields(rt,forward)->parent,0,"forward type has no invented parent");
  CHECK(fields(rt,forward)->allocated_size,0,"forward type has no invented object size");
  CHECK(dc_goal_is_subtype(rt,forward,rt->basic_type),0,"forward declaration is not a completed basic subtype");
  used=rt->used;
  CHECK(dc_goal_intern_type(rt,"dc-load-forward",1),forward,"forward type intern identity");
  CHECK(rt->used,used,"repeated forward intern does not allocate");
  CHECK(dc_goal_intern_type(rt,"basic",0),rt->basic_type,"existing type ignores default-count hint");
  CHECK(dc_goal_intern_type(rt,"basic",12),0,"explicit growth beyond existing method capacity rejected");
  CHECK(rt->error,DC_GOAL_METHOD_CAPACITY,"forward intern capacity error");
  CHECK(dc_goal_new_type(rt,dc_goal_find(rt,"dc-load-forward"),rt->basic_type,
                        UINT64_C(16)|(UINT64_C(3)<<16)|(UINT64_C(9)<<32)),forward,"deftype refines original forward object in place");
  CHECK(fields(rt,forward)->parent,rt->basic_type,"forward type receives original parent");
  CHECK(fields(rt,forward)->allocated_size,16,"forward type receives original size");
  CHECK(fields(rt,forward)->num_methods,44,"forward table capacity retained after definition");
  CHECK(rt->used,used,"forward refinement allocates no replacement type");
  taken=dc_goal_intern_type(rt,"dc-load-default",0);
  CHECK(taken!=0 && fields(rt,taken)->num_methods==12,1,"zero intern hint uses twelve slots");
  CHECK(dc_goal_intern_type(rt,"#f",1),0,"occupied non-type binding cannot become a type");
  CHECK(dc_goal_intern_type(rt,"dc-load-invalid",127),0,"unsupported forward method count rejected");
  CHECK(dc_goal_find(rt,"dc-load-invalid"),0,"invalid hint does not create a symbol");
  taken=dc_goal_intern(rt,"taken"); dc_goal_set_symbol(rt,taken,rt->s7);
  used=rt->used; symbols=rt->symbol_count;
  memcpy(table,dc_goal_resolve(rt,rt->table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES);
  CHECK(dc_goal_load_data_object(rt,object_failure,sizeof(object_failure),&r),0,"late type-import failure after forward allocation");
  CHECK(rt->error,DC_GOAL_BAD_TYPE,"late type failure preserved");
  CHECK(rt->used,used,"late failure rolls back arena");
  CHECK(rt->symbol_count,symbols,"late failure rolls back symbol count");
  CHECK(memcmp(table,dc_goal_resolve(rt,rt->table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES),0,"late failure restores exact symbol table");
  CHECK(dc_goal_find(rt,"fresh"),0,"late failure removes newly interned forward type");
  CHECK(r.data==0 && r.bytes==0 && r.relocations==0,1,"failed data result remains empty");
  capacity=rt->capacity; rt->capacity=rt->used+96u;
  CHECK(dc_goal_load_data_object(rt,object_failure,sizeof(object_failure),&r),0,"data OOM after allocating a fresh symbol name");
  CHECK(rt->error,DC_GOAL_OUT_OF_MEMORY,"data OOM preserved");
  CHECK(rt->used,used,"data OOM rolls back arena");
  CHECK(memcmp(table,dc_goal_resolve(rt,rt->table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES),0,"data OOM restores symbol table");
  CHECK(dc_goal_find(rt,"fresh"),0,"data OOM removes fresh name");
  rt->capacity=capacity;
  memcpy(bad,object_v4,sizeof(bad)); bad[28]=65;
  CHECK(dc_goal_load_data_object(rt,bad,sizeof(bad),&r),0,"malformed pointer rejected before data allocation");
  CHECK(rt->used,used,"malformed source consumes no arena");
  CHECK(memcmp(table,dc_goal_resolve(rt,rt->table,DC_GOAL_SYMBOL_BYTES),DC_GOAL_SYMBOL_BYTES),0,"malformed source leaves table unchanged");
  CHECK(dc_goal_load_data_object(rt,object_v2,sizeof(object_v2),&r),0,"native asset load requires unambiguous V4 data format");
  CHECK(rt->used,used,"V2 rejection consumes no arena");
  memcpy(bad,object_v4,sizeof(bad)); bad[8]=3;
  CHECK(dc_goal_load_data_object(rt,bad,sizeof(bad),&r),0,"MIPS V3 code cannot enter native data loader");
  CHECK(rt->used,used,"V3 rejection consumes no arena");
  CHECK(dc_goal_load_data_object(rt,object_v4,sizeof(object_v4),NULL),0,"null data result rejected");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"null data result error");
  CHECK(dc_goal_load_data_object(NULL,object_v4,sizeof(object_v4),&r),0,"null data runtime rejected");
  CHECK(dc_goal_load_data_object(rt,NULL,sizeof(object_v4),&r),0,"null data source rejected");
  {
    uint32_t data_bytes=DC_GOAL_DATA_MAX_BYTES+4u,link=16u+data_bytes,total=link+64u;
    unsigned char *oversize=calloc(total,1);
    CHECK(oversize!=NULL,1,"over-budget data fixture allocation");
    if (oversize) {
      put32(oversize,UINT32_MAX); put32(oversize+4,64); put32(oversize+8,4); put32(oversize+12,data_bytes);
      put32(oversize+link,UINT32_MAX); put32(oversize+link+4,64); put32(oversize+link+8,2);
      CHECK(dc_goal_load_data_object(rt,oversize,total,&r),0,"structurally valid data beyond two-MiB profile rejected");
      CHECK(rt->error,DC_GOAL_BAD_MODULE,"data profile budget fails before arena allocation");
      CHECK(rt->used,used,"over-budget original data leaves arena unchanged");
      free(oversize);
    }
  }
#if DC_OBJECT_HAS_RETAIL
  CHECK(dc_goal_load_data_object(rt,object_retail,sizeof(object_retail),&r),1,"unmodified original game-cnt data links natively");
  CHECK(r.bytes,144,"retail native data size");
  CHECK(r.relocations,1,"retail native type patch count");
  forward=dc_goal_intern_type(rt,"game-count-info",1);
  CHECK(dc_goal_type_of(rt,r.data+4),forward,"retail root uses native forward type tag");
  CHECK(fields(rt,forward)->num_methods,44,"retail unknown-count import uses original default capacity");
  CHECK(fields(rt,forward)->parent,0,"retail type remains explicitly forward-declared");
  CHECK(object_retail[16]==255 && object_retail[17]==255,1,"retail source remains original and immutable");
  CHECK(memcmp(dc_goal_resolve(rt,r.data+4,140),object_retail+20,140),0,"all retail payload fields preserved byte-for-byte");
  CHECK(r.imports,1,"retail data imports one original type");
#endif
#if DC_OBJECT_HAS_ART
  {
    dc_goal_object_layout layout;
    art_check_context art={0};
    const char *scan_error;
    uint32_t name,geometry,i,unmodified=0,mismatch=0;
    CHECK(dc_goal_bind_fixed(rt,DC_FIX_SYM_ART_GROUP,"art-group",0),1,"original art-group fixed symbol preinstalled");
    CHECK(dc_goal_load_data_object(rt,object_retail_art,sizeof(object_retail_art),&r),1,"original warpgate art data links natively");
    if (!r.data) { free(table); return; }
    CHECK(r.bytes,1136,"retail art data bytes");
    CHECK(r.imports,7,"retail art import groups");
    CHECK(r.relocations,48,"retail art total pointer/import patches");
    forward=dc_goal_intern_type(rt,"art-group",1);
    CHECK(dc_goal_type_of(rt,r.data+4),forward,"retail art root uses native type");
    CHECK(dc_goal_find(rt,"art-group"),rt->s7+DC_FIX_SYM_ART_GROUP,"retail art keeps original fixed namespace slot");
    name=word(rt,r.data+8);
    CHECK(name,r.data+1108u,"retail art name pointer rebased");
    CHECK(dc_goal_type_of(rt,name),rt->string_type,"retail nested string retains native type");
    CHECK(word(rt,name),8,"retail art name length");
    CHECK(memcmp(dc_goal_resolve(rt,name+4,9),"warpgate",9),0,"retail art name bytes preserved");
    CHECK(word(rt,r.data+12),3,"retail art element count preserved");
    CHECK(word(rt,r.data+16),rt->s7,"retail art false symbol canonical address");
    geometry=word(rt,r.data+32);
    CHECK(geometry,r.data+244u,"retail art geometry pointer rebased");
    CHECK(dc_goal_type_of(rt,geometry),dc_goal_intern_type(rt,"art-joint-geo",1),"retail nested geometry tag");
    art.rt=rt; art.check=check; art.context=context; art.id=id; art.data=r.data;
    scan_error=dc_goal_object_scan(object_retail_art,sizeof(object_retail_art),check_art_relocation,&art,&layout);
    CHECK(scan_error==NULL,1,"retail art native fixup inventory validates");
    CHECK(art.visits,48,"all retail art fixups checked");
    for (i=0;i<284;++i) if (!art.patched[i]) {
      uint32_t source; memcpy(&source,object_retail_art+16u+i*4u,4);
      ++unmodified; if (word(rt,r.data+i*4u)!=source) ++mismatch;
    }
    CHECK(unmodified,236,"retail art unpatched word count");
    CHECK(mismatch,0,"retail art unpatched matrix/payload words preserved");
  }
#endif
  free(table);
#undef CHECK
}
