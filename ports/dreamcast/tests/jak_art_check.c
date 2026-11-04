/* Host execution of native data linker; GOAL login is a double (AI-assisted), ISC. */
#include "goal_art.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build/jak-art-validation/art_names.h"

enum {CAPACITY=11010048,PAYLOAD=1979744};
static unsigned char memory[CAPACITY];
static unsigned callbacks;
static int preintern=1;
static uint32_t word(dc_goal_runtime *rt,uint32_t p){
  uint32_t v;memcpy(&v,dc_goal_resolve(rt,p,4),4);return v;
}
static int logged(dc_goal_runtime *rt,uint32_t art,void *context){
  const char *name=callbacks?"sidekick":"eichar";
  uint32_t str=word(rt,art+4),type=dc_goal_symbol_value(rt,dc_goal_find(rt,"art-group"));
  (void)context;
  assert(callbacks<2 && word(rt,art-4)==type);
  assert(word(rt,str)==strlen(name));
  assert(!strcmp(dc_goal_resolve(rt,str+4,(uint32_t)strlen(name)+1),name));
  ++callbacks;return 1;
}
static void init(dc_goal_runtime *rt){
  unsigned i;
  assert(dc_goal_init(rt,memory,sizeof memory,0x10000000));
  for(i=0;i<sizeof art_types/sizeof art_types[0];++i){
    uint32_t s=dc_goal_find(rt,art_types[i]),t=s?dc_goal_symbol_value(rt,s):0;
    if(!t || t==rt->s7)assert(dc_goal_define_type(rt,art_types[i],rt->basic_type,16,0,44));
  }
  if(preintern)for(i=0;i<sizeof art_symbols/sizeof art_symbols[0];++i)assert(dc_goal_intern(rt,art_symbols[i]));
  assert(rt->error==DC_GOAL_OK);callbacks=0;
}
int main(int argc,char **argv){
  dc_goal_runtime rt;dc_goal_art_result r;FILE *f,*out;uint32_t before,symbols;unsigned i;
  assert(argc==3 || argc==4);init(&rt);before=rt.used;symbols=rt.symbol_count;
  f=fopen(argv[1],"rb");assert(f);
  if(argc==4){
    assert(!dc_goal_load_art_dgo(&rt,f,PAYLOAD,logged,NULL,&r));
    assert(!callbacks && rt.used==before && rt.symbol_count==symbols);
    assert(rt.error==DC_GOAL_BAD_MODULE);fclose(f);puts("malformed data refused before allocation/login");return 0;
  }
  assert(!dc_goal_load_art_dgo(&rt,f,PAYLOAD-16,logged,NULL,&r));
  assert(rt.error==DC_GOAL_BAD_MODULE && rt.used==before && !callbacks && rt.symbol_count==symbols);
  rt.error=DC_GOAL_OK;
  /* Run the real allocator at the exact RECORDED arena baseline, not a new
   * SH-4 observation. Neither an art byte nor a symbol is written on refusal. */
  rt.used=10673360;memory[rt.used]=0xa5;
  assert(!dc_goal_load_art_dgo(&rt,f,PAYLOAD,logged,NULL,&r));
  assert(rt.error==DC_GOAL_OUT_OF_MEMORY && r.available==336688 && r.shortfall==1643056);
  assert(rt.used==10673360 && memory[rt.used]==0xa5 && !callbacks && rt.symbol_count==symbols);
  rt.error=DC_GOAL_OK;
  assert(!dc_goal_art_admit(&rt,1318288,&r) && r.shortfall==981600);
  rt.error=DC_GOAL_OK;rt.temporary=16;
  assert(!dc_goal_art_admit(&rt,PAYLOAD,&r) && r.shortfall==1643072);
  rt.error=DC_GOAL_BAD_FUNCTION;
  assert(!dc_goal_art_admit(&rt,PAYLOAD,&r) && rt.error==DC_GOAL_BAD_FUNCTION);
  /* Retail resources contain names that code startup may never have interned.
   * Account for the real symbol-string allocations separately from payload. */
  preintern=0;init(&rt);before=rt.used;
  {uint32_t missing=0,string_bytes=0,count=rt.symbol_count;
   for(i=0;i<sizeof art_symbols/sizeof art_symbols[0];++i){
     if(!dc_goal_find(&rt,art_symbols[i])){
       uint32_t bytes=(uint32_t)strlen(art_symbols[i])+1;
       if(bytes<8)bytes=8;
       string_bytes+=(bytes+8+15)&~15u;++missing;
     }
   }
   assert(dc_goal_load_art_dgo(&rt,f,PAYLOAD,logged,NULL,&r));
   assert(rt.used==before+PAYLOAD+string_bytes && rt.symbol_count==count+missing);
   assert(callbacks==2 && missing>0);
   printf("new resource symbols %u; extra string bytes %u\n",missing,string_bytes);
  }
  preintern=1;
  init(&rt);before=rt.used;rt.capacity=before+PAYLOAD-16;
  assert(!dc_goal_load_art_dgo(&rt,f,PAYLOAD,logged,NULL,&r) && r.shortfall==16 && rt.used==before && !callbacks);
  rt.error=DC_GOAL_OK;rt.capacity+=16;
  assert(dc_goal_load_art_dgo(&rt,f,PAYLOAD,logged,NULL,&r));fclose(f);
  assert(rt.error==DC_GOAL_OK && r.payload==PAYLOAD && rt.used==rt.capacity);
  assert(r.linked==2 && r.relocated==2 && callbacks==2 && r.pointers==28301);
  assert(rt.symbol_count==symbols);
  out=fopen(argv[2],"wb");assert(out);
  assert(fwrite(memory+before,1,PAYLOAD,out)==PAYLOAD);fclose(out);
  printf("base %u\n",rt.base+before);
  for(i=0;i<sizeof art_types/sizeof art_types[0];++i)
    printf("type %s %u\n",art_types[i],dc_goal_symbol_value(&rt,dc_goal_find(&rt,art_types[i])));
  for(i=0;i<sizeof art_symbols/sizeof art_symbols[0];++i)
    printf("symbol %s %u\n",art_symbols[i],dc_goal_find(&rt,art_symbols[i]));
  printf("linked %u pointers %u bindings %u; exact-fit and OOM checks passed\n",r.payload,r.pointers,r.bindings);
  return 0;
}
