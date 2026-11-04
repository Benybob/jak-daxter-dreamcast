/* Dependency refusal is part of this milestone, never a simulated player.
 * No target/control/camera position is produced here (AI-assisted), ISC. */
#ifdef DC_ARENA_JAK
#include "arena_jak.h"
#include "arena_live.h"
#include "goal_fiber.h"
#if defined(DC_ARENA_JAK_ART) || defined(DC_ARENA_JAK_PLAYER)
#include "arena_jak_art.h"
#endif
#ifdef DC_ARENA_JAK_PLAYER
#include "arena_jak_player.h"
#include "arena_jak_exports.h"
#include "goal_art.h"
#endif
#include <stdio.h>
#include <string.h>

volatile dc_jak_evidence dc_arena_jak_evidence={.version=1};
typedef char jak_evidence_size[sizeof(dc_jak_evidence)==212?1:-1];
static const unsigned char *span(dc_goal_runtime *rt,uint32_t p,uint32_t n){
  uint32_t off;
  if(!rt || !rt->memory || rt->used>rt->capacity || rt->base>UINT32_MAX-rt->capacity || p<rt->base)return NULL;
  off=p-rt->base;return off<=rt->used && n<=rt->used-off?rt->memory+off:NULL;
}
static uint32_t word(dc_goal_runtime *rt,uint32_t p){
  uint32_t v=0;const void *b=span(rt,p,4);if(b)memcpy(&v,b,4);return v;
}
static uint32_t named(dc_goal_runtime *rt,const char *name){
  /* Find resets rt->error; lookups must never hide a fault from original code. */
  enum dc_goal_error error=rt->error;uint32_t s=dc_goal_find(rt,name),v=s?word(rt,s):0;
  if(error!=DC_GOAL_OK)rt->error=error;
  return v;
}
static int object(dc_goal_runtime *rt,uint32_t p,const char *type,uint32_t bytes){
  uint32_t t=named(rt,type);
  return p>=4 && (p&15u)==4 && t && t!=rt->s7 && span(rt,p-4,bytes) && word(rt,p-4)==t;
}
static uint32_t entry(dc_goal_runtime *rt,const char *name){
  dc_goal_code_entry_info info;uint32_t p=named(rt,name),s;
  if(rt->error!=DC_GOAL_OK || !dc_goal_code_entry(rt,p,&info))return 0;
  s=dc_goal_find(rt,name);return info.symbol==s?p:0;
}
static int require(dc_goal_runtime *rt,int ok,unsigned blocker,const char *why){
  if(!ok){
    dc_arena_jak_evidence.blocker=blocker;
    dc_arena_jak_evidence.runtime_error=rt?rt->error:DC_GOAL_BAD_ARGUMENT;
    dc_arena_jak_evidence.error_site=dc_goal_error_site;
    snprintf((char *)dc_arena_jak_evidence.missing,sizeof dc_arena_jak_evidence.missing,"%s",why);
  }
  return dc_arena_live_check(ok,1,why);
}
#define REQUIRE(ok,id,why) do{if(!require(rt,(ok),(id),(why)))return 0;}while(0)
static int string_is(dc_goal_runtime *rt,uint32_t p,const char *text){
  uint32_t n=(uint32_t)strlen(text);
  return object(rt,p,"string",n+9) && word(rt,p)==n && span(rt,p+4,n+1) &&
         !memcmp(span(rt,p+4,n+1),text,n+1);
}
int dc_arena_jak_finish(dc_goal_runtime *rt){
  typedef uint64_t (*one_fn)(uint64_t);
  typedef uint64_t (*five_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
  uint32_t game,load,cp,levels,level,sg,dir,names,data,n,art,lookup,version;
  uint32_t before,error;
  REQUIRE(rt && rt->error==DC_GOAL_OK && !dc_goal_fiber_current() &&
          dc_arena_jak_evidence.debug_calls==1 && dc_arena_jak_evidence.debug_restored &&
          dc_arena_live_evidence.restored && dc_arena_live_evidence.rendered==DC_LIVE_UPDATES &&
          dc_arena_live_evidence.turning_pairs==2,1,
          "jak dependency probe follows completed original DIGITAL camera proof");
  dc_arena_jak_evidence.start_entry=entry(rt,"start");
  dc_arena_jak_evidence.init_entry=entry(rt,"init-target");
  dc_arena_jak_evidence.continue_entry=entry(rt,"(method get-or-create-continue! game-info)");
  dc_arena_jak_evidence.lookup_entry=entry(rt,"(method load-to-heap-by-name load-dir-art-group)");
  REQUIRE(dc_arena_jak_evidence.start_entry && dc_arena_jak_evidence.init_entry &&
          dc_arena_jak_evidence.continue_entry && dc_arena_jak_evidence.lookup_entry,2,
          "jak start, init-target, continue factory and art lookup are exact original exports");
  dc_arena_jak_evidence.target_before=named(rt,"*target*");
  dc_arena_jak_evidence.target_after=dc_arena_jak_evidence.target_before;
  REQUIRE(dc_arena_jak_evidence.target_before==rt->s7 &&
          span(rt,named(rt,"*target-dead-pool*"),20) && span(rt,named(rt,"*target-pool*"),20),3,
          "jak target is absent; original target pools exist before any spawn");
  game=named(rt,"*game-info*");load=named(rt,"*load-state*");
  REQUIRE(object(rt,game,"game-info",112) && span(rt,load,36) &&
          object(rt,named(rt,"*default-continue*"),"continue-point",128),4,
          "jak original continue factory has game-info, load-state and default continue");
  cp=(uint32_t)((one_fn)(uintptr_t)dc_arena_jak_evidence.continue_entry)(game);
  error=rt->error;++dc_arena_jak_evidence.continue_calls;dc_arena_jak_evidence.continue_point=cp;
  dc_arena_jak_evidence.target_after=named(rt,"*target*");
  REQUIRE(error==DC_GOAL_OK && object(rt,cp,"continue-point",128) &&
          dc_arena_jak_evidence.target_after==dc_arena_jak_evidence.target_before,5,
          "jak ORIGINAL get-or-create-continue! returns a typed continue point");
  /* initialize-skeleton's exact texture-level=2 route: default inline level,
   * not the beach renderer's private DCI package. Offsets from original IR:
   * level-group+5312, level.art-group+48, skeleton-group.name+0/version+61. */
  levels=named(rt,"*level*");sg=named(rt,"*jchar-sg*");level=levels+5312;
  REQUIRE(object(rt,levels,"level-group",0x1ef4) && object(rt,level,"level",2608) &&
          object(rt,sg,"skeleton-group",80) && *span(rt,sg+60,1)==2 &&
          string_is(rt,word(rt,sg),"eichar"),6,
          "jak original skeleton descriptor requests eichar in default level");
  dir=word(rt,level+48);dc_arena_jak_evidence.art_directory=dir;
  REQUIRE(object(rt,dir,"load-dir-art-group",16) &&
          word(rt,word(rt,dir-4)+52)==dc_arena_jak_evidence.lookup_entry,7,
          "jak spawn blocked: default art directory or original lookup method missing");
  names=word(rt,dir+4);data=word(rt,dir+8);n=word(rt,names);
  REQUIRE(object(rt,names,"array",16) && object(rt,data,"array",16) &&
          n<=word(rt,names+4) && n==word(rt,data) && n<=word(rt,data+4) &&
          n<=50 && span(rt,names+12,n*4) && span(rt,data+12,n*4) &&
          named(rt,"*debug-segment*")==rt->s7,8,
          "jak art lookup has bounded original arrays and debug disk loading disabled");
  lookup=dc_arena_jak_evidence.lookup_entry;version=*span(rt,sg+61,1);
  dc_arena_jak_evidence.art_name=word(rt,sg);before=rt->used;
  art=(uint32_t)((five_fn)(uintptr_t)lookup)(dir,word(rt,sg),rt->s7,named(rt,"global"),version);
  error=rt->error;++dc_arena_jak_evidence.lookup_calls;dc_arena_jak_evidence.art_result=art;
  dc_arena_jak_evidence.target_after=named(rt,"*target*");
  REQUIRE(error==DC_GOAL_OK && rt->used==before && word(rt,names)==n && word(rt,data)==n &&
          dc_arena_jak_evidence.target_after==dc_arena_jak_evidence.target_before,9,
          "jak ORIGINAL art lookup returns without runtime fault or fabricated data");
#if defined(DC_ARENA_JAK_ART) || defined(DC_ARENA_JAK_PLAYER)
#ifdef DC_ARENA_JAK_PLAYER
  /* Snapshot provenance before original login. This is not a new check: login
   * still runs, and the player check remains the only refusal. The query must
   * not clear or create the runtime fault or its site. */
  {
    enum dc_goal_error saved_error=rt->error;
    uint32_t saved_site=dc_goal_error_site;
    dc_arena_jak_exports(rt,0);
    rt->error=saved_error;
    dc_goal_error_site=saved_site;
  }
#endif
  REQUIRE(dc_arena_jak_art_load(rt,level),10,dc_arena_jak_art_reason);
#ifdef DC_ARENA_JAK_PLAYER
  dc_goal_art_probe(rt,6);
#endif
  art=(uint32_t)((five_fn)(uintptr_t)lookup)(dir,word(rt,sg),rt->s7,named(rt,"global"),version);
#ifdef DC_ARENA_JAK_PLAYER
  dc_goal_art_probe(rt,7);
#endif
  ++dc_arena_jak_evidence.lookup_calls;dc_arena_jak_evidence.art_result=art;
  REQUIRE(rt->error==DC_GOAL_OK && object(rt,art,"art-group",32),10,
          "jak ORIGINAL lookup finds real relocated eichar after original login");
#endif
  /* Do not call start with an absent art group: initialize-skeleton goes to
   * process-drawable-art-error before a runnable target can be initialized.
   * This is a source-derived prerequisite refusal, not an observed init fault. */
  REQUIRE(object(rt,art,"art-group",32),10,
          "jak spawn blocked: missing eichar art-group in default level (init-target:1230)");
#ifdef DC_ARENA_JAK_PLAYER
  return dc_arena_jak_player_run(rt,cp);
#else
  /* Art alone is insufficient. Keep this mode failing until the remaining
   * original spawn/follow path is implemented and measured, even with new data. */
  REQUIRE(0,11,"jak spawn blocked: level/entity/target execution route still requires integration");
  return 0;
#endif
}
#undef REQUIRE
#endif
