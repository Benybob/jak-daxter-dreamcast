/* Execute isolated admission wrapper on a recorded HOST budget (AI-assisted), ISC. */
#include "arena_jak_art.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[11010048];
volatile uint32_t dc_arena_slab_live=1255392,dc_arena_slab_high_water=1255392;
int main(int argc,char **argv){
  dc_goal_runtime rt;FILE *f;uint32_t symbols;
  assert(argc==2 && dc_goal_init(&rt,memory,sizeof memory,0x10000000));
  rt.used=10673360;symbols=rt.symbol_count;memory[rt.used]=0xa5;
  assert(!dc_arena_jak_art_load(&rt,0));
  assert(rt.error==DC_GOAL_OUT_OF_MEMORY && rt.used==10673360 && rt.symbol_count==symbols);
  assert(memory[rt.used]==0xa5 && dc_arena_jak_art_evidence.version==1);
  assert(dc_arena_jak_art_evidence.load.shortfall==1643056);
  assert(dc_arena_jak_art_evidence.single_shortfall==981600);
  assert(!dc_arena_jak_art_evidence.relocate_calls && !dc_arena_jak_art_evidence.load.linked);
  assert(!strcmp(dc_arena_jak_art_reason,
    "jak art RAM: need 1979744 free 336688 short 1643056 (payload only)"));
  f=fopen(argv[1],"wb");assert(f);
  assert(fwrite((const void *)&dc_arena_jak_art_evidence,1,100,f)==100);fclose(f);
  puts("isolated art wrapper: exact recorded-budget refusal, no file/login/target call");
  return 0;
}
