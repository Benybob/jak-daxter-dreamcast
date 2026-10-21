/* Original retail data format checks on SH-4 (AI-assisted), ISC. */
#include "goal_object.h"
#include "object_cases.h"
#include "build/object_fixture.h"
#include <string.h>
typedef struct { uint32_t visits,retail; } visitor_state;
static int visit(const dc_goal_object_relocation *r,void *context) {
  visitor_state *state=context; ++state->visits;
  if (r->kind==DC_OBJECT_TYPE && r->name_bytes==15 && !memcmp(r->name,"game-count-info",15)) ++state->retail;
  return 1;
}
static int reject(const dc_goal_object_relocation *r,void *context) {
  (void)r; (void)context; return 0;
}
void dc_object_cases(dc_runtime_check check,void *context,unsigned *id) {
  dc_goal_object_layout layout;
  visitor_state state={0,0};
  unsigned char bad[sizeof(object_v4)],before[sizeof(object_v4)];
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  memcpy(before,object_v4,sizeof(before));
  CHECK(dc_goal_object_scan(object_v4,sizeof(object_v4),visit,&state,&layout)==NULL,1,"original V4 format accepted");
  CHECK(layout.version,4,"original V4 version");
  CHECK(layout.data_offset,16,"V4 data begins after sixteen-byte header");
  CHECK(layout.data_bytes,64,"bounded V4 payload size");
  CHECK(layout.link_bytes,64,"original link padding alignment");
  CHECK(layout.bitmap_bytes,2,"two-bitset-byte duplicate tracking");
  CHECK(layout.internal_relocations,1,"internal pointer relocation count");
  CHECK(layout.symbol_relocations,1,"symbol relocation count");
  CHECK(layout.type_relocations,1,"type relocation count");
  CHECK(layout.empty_relocations,1,"empty list special relocation count");
  CHECK(layout.imports,3,"original import groups");
  CHECK(state.visits,4,"native relocation visitor");
  CHECK(memcmp(before,object_v4,sizeof(before)),0,"object scan leaves input immutable");
  CHECK(dc_goal_object_scan(object_v2,sizeof(object_v2),NULL,NULL,&layout)==NULL,1,"original V2 format accepted");
  CHECK(layout.version,2,"original V2 version");
  CHECK(layout.data_offset,64,"V2 data follows link table");
  CHECK(dc_goal_object_scan(object_large,sizeof(object_large),NULL,NULL,&layout)==NULL,1,"original 255-byte run continuations");
  CHECK(layout.internal_relocations,10,"continued pointer run count");
  CHECK(layout.bitmap_bytes,65,"bounded large fixture bitmap");
  CHECK(dc_goal_object_scan(object_v4,0,NULL,NULL,&layout)!=NULL,1,"empty object rejected");
  CHECK(dc_goal_object_scan(object_v4,sizeof(object_v4),NULL,NULL,NULL)!=NULL,1,"null scan layout rejected");
  CHECK(dc_goal_object_scan(NULL,sizeof(object_v4),NULL,NULL,&layout)!=NULL,1,"null object source rejected");
  CHECK(dc_goal_object_scan(object_v4,sizeof(object_v4),reject,NULL,&layout)!=NULL,1,"native visitor rejection honored");
  memcpy(bad,object_v4,sizeof(bad)); bad[28]=64;
  CHECK(dc_goal_object_scan(bad,sizeof(bad),NULL,NULL,&layout)==NULL,1,"one-past pointer for zero-length data accepted");
  bad[28]=65;
  CHECK(dc_goal_object_scan(bad,sizeof(bad),NULL,NULL,&layout)!=NULL,1,"outside-segment pointer target rejected");
  memcpy(bad,object_v4,sizeof(bad)); bad[111]=0;
  CHECK(dc_goal_object_scan(bad,sizeof(bad),NULL,NULL,&layout)!=NULL,1,"duplicate relocation rejected");
  memcpy(bad,object_v4,sizeof(bad)); bad[16]=0;
  CHECK(dc_goal_object_scan(bad,sizeof(bad),NULL,NULL,&layout)!=NULL,1,"incorrect import placeholder rejected");
  memcpy(bad,object_v4,sizeof(bad)); bad[sizeof(bad)-1]=1;
  CHECK(dc_goal_object_scan(bad,sizeof(bad),NULL,NULL,&layout)!=NULL,1,"nonzero link padding rejected");
  memcpy(bad,object_v4,sizeof(bad)); bad[8]=3;
  CHECK(dc_goal_object_scan(bad,sizeof(bad),NULL,NULL,&layout)!=NULL,1,"retail MIPS V3 code object rejected");
  CHECK(dc_goal_object_scan(object_v4,sizeof(object_v4)-1,NULL,NULL,&layout)!=NULL,1,"truncated original object rejected");
#if DC_OBJECT_HAS_RETAIL
  state.visits=0; state.retail=0;
  CHECK(dc_goal_object_scan(object_retail,sizeof(object_retail),visit,&state,&layout)==NULL,1,"unmodified retail game-cnt object accepted");
  CHECK(layout.version,4,"retail data version");
  CHECK(layout.data_bytes,144,"retail game-count payload size");
  CHECK(layout.type_relocations,1,"retail game-count type reference");
  CHECK(layout.imports,1,"retail game-count import count");
  CHECK(layout.internal_relocations,0,"retail game-count has no internal pointer fixups");
  CHECK(layout.symbol_relocations+layout.empty_relocations,0,"retail game-count has no symbol fixups");
  CHECK(state.retail,1,"retail type-name visitor reads original bytes");
#endif
#undef CHECK
}
