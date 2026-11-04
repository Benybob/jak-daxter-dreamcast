/* Real one-object relocation/admission; login double (AI-assisted), ISC. */
#include "goal_art.h"
#include "build/arena-jak-budget/budget.h"
#include "build/jak-art-validation/art_names.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[DC_JAK_GOAL_BYTES] __attribute__((aligned(32)));
static unsigned calls;
static int login(dc_goal_runtime *rt,uint32_t art,void *context){
 uint32_t type;memcpy(&type,dc_goal_resolve(rt,art-4,4),4);
 assert(type==dc_goal_symbol_value(rt,dc_goal_find(rt,"art-group")));
 (void)context;++calls;return 1;
}
int main(int argc,char **argv){
 dc_goal_runtime rt;dc_goal_art_result r;uint32_t i,base;FILE *f,*out;
 assert(argc==3);assert(dc_goal_init(&rt,memory,sizeof memory,0x10000000));
 for(i=0;i<sizeof art_types/sizeof art_types[0];++i){
  uint32_t s=dc_goal_find(&rt,art_types[i]);
  if(!s || !dc_goal_symbol_value(&rt,s))assert(dc_goal_define_type(&rt,art_types[i],rt.basic_type,16,0,44));
 }
 for(i=0;i<sizeof art_symbols/sizeof art_symbols[0];++i)assert(dc_goal_intern(&rt,art_symbols[i]));
 rt.used=10673360;
 assert(!dc_goal_art_admit(&rt,1979744,&r));
 assert(r.shortfall==1979744u-(DC_JAK_GOAL_BYTES-10673360u) && rt.error==DC_GOAL_OUT_OF_MEMORY && rt.used==10673360);
 rt.error=DC_GOAL_OK;assert(dc_goal_art_admit(&rt,1318288,&r));assert(!r.shortfall);
 f=fopen(argv[1],"rb");assert(f);
 assert(dc_goal_load_art_dgo(&rt,f,1318288,login,NULL,&r));assert(!fclose(f));
 assert(calls==1 && r.objects==1 && r.linked==1 && r.relocated==1 && r.art[1]==0);
 assert(r.pointers==18523 && rt.capacity-rt.used==DC_JAK_GOAL_BYTES-10673360u-1318288u);
 base=r.art[0]-4;printf("base %lu\n",(unsigned long)base);
 for(i=0;i<sizeof art_types/sizeof art_types[0];++i)
  printf("type %s %lu\n",art_types[i],(unsigned long)dc_goal_symbol_value(&rt,dc_goal_find(&rt,art_types[i])));
 for(i=0;i<sizeof art_symbols/sizeof art_symbols[0];++i)
  printf("symbol %s %lu\n",art_symbols[i],(unsigned long)dc_goal_find(&rt,art_symbols[i]));
 out=fopen(argv[2],"wb");assert(out);assert(fwrite(dc_goal_resolve(&rt,base,1318288),1,1318288,out)==1318288);
 assert(!fclose(out));return 0;
}
