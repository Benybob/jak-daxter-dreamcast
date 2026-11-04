/* No original player is spawned unless complete data fits (AI-assisted), ISC. */
#if defined(DC_ARENA_JAK_ART) || defined(DC_ARENA_JAK_PLAYER)
#include "arena_jak_art.h"
#include "goal_module.h"
#include "build/jak-art-disc/jak_art_sizes.h"
#include <string.h>
#include <stdio.h>
#ifdef DC_ARENA_JAK_PLAYER
#include "arena_jak_memory.h"
#include "arena_jak_player.h"
#endif

volatile dc_jak_art_evidence dc_arena_jak_art_evidence={.version=1};
typedef char art_evidence_bytes[sizeof(dc_jak_art_evidence)==100?1:-1];
char dc_arena_jak_art_reason[96];
extern volatile uint32_t dc_arena_slab_live,dc_arena_slab_high_water;
static uint32_t word(dc_goal_runtime *rt,uint32_t p){
  uint32_t v=0;const void *b=dc_goal_resolve(rt,p,4);if(b)memcpy(&v,b,4);return v;
}
static uint32_t named(dc_goal_runtime *rt,const char *name){
  uint32_t s=dc_goal_find(rt,name);return s?dc_goal_symbol_value(rt,s):0;
}
static int relocate(dc_goal_runtime *rt,uint32_t art,void *context){
  typedef void (*relocate_fn)(uint64_t,uint64_t,uint64_t);
  uint32_t levels=named(rt,"*level*"),level=*(const uint32_t *)context;
  uint32_t type=named(rt,"art-group"),fn=named(rt,"(method relocate art-group)");
  uint32_t old,heap=named(rt,"global"),name=word(rt,art+4);
  dc_goal_code_entry_info info;
  /* level-group.loading-level is tagged +8; default level remains the real
   * original inline object. No directory entries or art fields are set by C. */
  if(rt->error!=DC_GOAL_OK || !dc_goal_code_entry(rt,fn,&info) ||
     info.symbol!=dc_goal_find(rt,"(method relocate art-group)") ||
     word(rt,art-4)!=type || dc_goal_get_method(rt,type,7)!=fn ||
     !dc_goal_resolve(rt,levels+8,4))return 0;
  dc_arena_jak_art_evidence.relocate_entry=fn;
  old=word(rt,levels+8);memcpy(dc_goal_resolve(rt,levels+8,4),&level,4);
  ((relocate_fn)(uintptr_t)fn)(art,heap,name+4);
  {enum dc_goal_error error=rt->error;
   memcpy(dc_goal_resolve(rt,levels+8,4),&old,4);rt->error=error;}
  ++dc_arena_jak_art_evidence.relocate_calls;
  return rt->error==DC_GOAL_OK;
}
int dc_arena_jak_art_load(dc_goal_runtime *rt,uint32_t level){
  dc_goal_art_result result;FILE *f;int ok;
#ifdef DC_ARENA_JAK_PLAYER
  uint32_t free_bytes,payload;
  const char *path;
  if(!rt || rt->error!=DC_GOAL_OK || rt->temporary>rt->capacity || rt->used>rt->capacity-rt->temporary)return 0;
  free_bytes=rt->capacity-rt->temporary-rt->used;
  /* Keep the exact pair deficit without raising/clearing an OOM just to
   * choose the permitted one-object path. Login/symbol costs come afterwards. */
  dc_arena_jak_player_evidence.pair_available=free_bytes;
  dc_arena_jak_player_evidence.pair_shortfall=DC_JAK_ART_BYTES>free_bytes?DC_JAK_ART_BYTES-free_bytes:0;
  payload=free_bytes>=DC_JAK_ART_BYTES+65536u?DC_JAK_ART_BYTES:DC_JAK_EICHAR_BYTES;
  path=payload==DC_JAK_ART_BYTES?"/cd/JAKART.DGO":"/cd/EICHAR.DGO";
  dc_arena_jak_player_evidence.sidekick_loaded=0;
#endif
  dc_arena_jak_art_evidence.eichar_bytes=DC_JAK_EICHAR_BYTES;
  dc_arena_jak_art_evidence.sidekick_bytes=DC_JAK_SIDEKICK_BYTES;
#ifdef DC_ARENA_JAK_PLAYER
  dc_arena_jak_art_evidence.slab_bytes=DC_JAK_RENDER_SLAB_BYTES;
#else
  dc_arena_jak_art_evidence.slab_bytes=2621440;
#endif
  dc_arena_jak_art_evidence.slab_live=dc_arena_slab_live;
  dc_arena_jak_art_evidence.slab_high_water=dc_arena_slab_high_water;
#ifdef DC_ARENA_JAK_PLAYER
  ok=dc_goal_art_admit(rt,payload,&result);
#else
  ok=dc_goal_art_admit(rt,DC_JAK_ART_BYTES,&result);
#endif
  dc_arena_jak_art_evidence.load=result;
  dc_arena_jak_art_evidence.single_shortfall=DC_JAK_EICHAR_BYTES>result.available?
    DC_JAK_EICHAR_BYTES-result.available:0;
  if(!ok){
    snprintf(dc_arena_jak_art_reason,sizeof dc_arena_jak_art_reason,
      "jak art RAM: need %lu free %lu short %lu (payload only)",
      (unsigned long)result.payload,(unsigned long)result.available,(unsigned long)result.shortfall);
  }else{
    /* Deliberately outside both VRAM romdisks. The unchanged boot image and
     * live render image cannot accommodate this 2 MB archive. */
#ifdef DC_ARENA_JAK_PLAYER
    f=fopen(path,"rb");
    if(!f){ok=0;snprintf(dc_arena_jak_art_reason,sizeof dc_arena_jak_art_reason,"jak art: staged %s is absent",path);}
    else {
      ok=dc_goal_load_art_dgo(rt,f,payload,relocate,&level,&result);fclose(f);
#else
    f=fopen("/cd/JAKART.DGO","rb");
    if(!f){ok=0;snprintf(dc_arena_jak_art_reason,sizeof dc_arena_jak_art_reason,"jak art: staged /cd/JAKART.DGO is absent");}
    else {
      ok=dc_goal_load_art_dgo(rt,f,DC_JAK_ART_BYTES,relocate,&level,&result);fclose(f);
#endif
      dc_arena_jak_art_evidence.load=result;
      /* Bind the admitted metadata to the loaded archive, never silently load
       * a larger/different pair after a successful admission. */
      snprintf(dc_arena_jak_art_reason,sizeof dc_arena_jak_art_reason,"jak art: retail relocation/login failed");
    }
  }
  dc_arena_jak_art_evidence.after=rt->used;
  dc_arena_jak_art_evidence.error=rt->error;
  dc_arena_jak_art_evidence.last_failed_alloc=dc_goal_last_failed_alloc;
#ifdef DC_ARENA_JAK_PLAYER
  if(ok){
    uint32_t available=rt->capacity-rt->temporary-rt->used;
    dc_arena_jak_player_evidence.sidekick_loaded=payload==DC_JAK_ART_BYTES;
    dc_arena_jak_player_evidence.sidekick_available=available;
    dc_arena_jak_player_evidence.sidekick_shortfall=payload==DC_JAK_ART_BYTES?0:
      (DC_JAK_SIDEKICK_BYTES>available?DC_JAK_SIDEKICK_BYTES-available:0);
  }
#endif
  return ok;
}
#endif
