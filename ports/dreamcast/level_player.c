/* Jak in the level ELF (AI-assisted), ISC.
 * The player path of the Jak ELF, without its arena renderer: load the
 * texture-page directory and eichar through their original relocate methods
 * (/cd/DIR-TPAGES.DGO, /cd/EICHAR.DGO from stage_jak_art.py), take the
 * original continue point, call original start 'play with the listener as the
 * caller, and import the target's armed main thread on *dram-stack* as a
 * native fiber so the display frames run it. --dump dc_level_player_evidence 64 */
#include "level_player.h"
#include "goal_art.h"
#include "goal_bridge.h"
#include "goal_fiber.h"
#include "goal_module.h"
#include "goal_main_start.h"
#include "build/jak-art-disc/jak_art_sizes.h"
#include <stdio.h>
#include <string.h>

volatile struct {
  uint32_t version,stage,error,site,dir,art_linked,art_relocated,continue_point;
  uint32_t target,thread,fiber,used_before,used_after,free_after,pad[2];
} dc_level_player_evidence={.version=1};
typedef char dc_level_player_evidence_bytes[sizeof dc_level_player_evidence==64?1:-1];

static dc_goal_fiber target_fiber;
static uint32_t named(dc_goal_runtime *rt,const char *name){
  uint32_t s=dc_goal_find(rt,name);return s?dc_goal_symbol_value(rt,s):0;
}
static uint32_t word(dc_goal_runtime *rt,uint32_t at){
  uint32_t v=0;const void *p=dc_goal_resolve(rt,at,4);if(p)memcpy(&v,p,4);return v;
}
static int stop(dc_goal_runtime *rt,uint32_t stage){
  dc_level_player_evidence.stage=stage;dc_level_player_evidence.error=rt->error;
  dc_level_player_evidence.site=dc_goal_error_site;
  return 0;
}
/* Original texture-page-dir.relocate sets *texture-page-dir*. */
static int relocate_dir(dc_goal_runtime *rt,uint32_t dir,void *context){
  typedef void (*relocate_fn)(uint64_t,uint64_t,uint64_t);
  uint32_t type=named(rt,"texture-page-dir"),fn=type?dc_goal_get_method(rt,type,7):0;
  (void)context;
  if(rt->error!=DC_GOAL_OK || !fn || word(rt,dir-4)!=type)return 0;
  ((relocate_fn)(uintptr_t)fn)(dir,named(rt,"global"),0);
  return rt->error==DC_GOAL_OK;
}
/* Original art-group.relocate (login, set-loaded-art) with the level-group's
 * loading-level (+8) selecting the default level, as the Jak ELF does. */
static int relocate_art(dc_goal_runtime *rt,uint32_t art,void *context){
  typedef void (*relocate_fn)(uint64_t,uint64_t,uint64_t);
  uint32_t levels=named(rt,"*level*"),level=*(const uint32_t *)context;
  uint32_t type=named(rt,"art-group"),fn=type?dc_goal_get_method(rt,type,7):0,old;
  unsigned char *slot;
  if(rt->error!=DC_GOAL_OK || !fn || word(rt,art-4)!=type || !(slot=dc_goal_resolve(rt,levels+8,4)))return 0;
  memcpy(&old,slot,4);memcpy(slot,&level,4);
  ((relocate_fn)(uintptr_t)fn)(art,named(rt,"global"),word(rt,art+4)+4);
  {enum dc_goal_error error=rt->error;memcpy(dc_goal_resolve(rt,levels+8,4),&old,4);rt->error=error;}
  return rt->error==DC_GOAL_OK;
}
int dc_level_player(dc_goal_runtime *rt){
  typedef uint64_t (*one_fn)(uint64_t);typedef uint64_t (*two_fn)(uint64_t,uint64_t);
  dc_goal_art_result art;FILE *f;int ok;
  uint32_t levels,level,cp,listener,bytes,start,target,thread,dram,base,top;
  if(!rt || rt->error!=DC_GOAL_OK || dc_goal_fiber_current())return stop(rt,1);
  dc_level_player_evidence.used_before=rt->used;
  /* The texture-page directory, then eichar into the default level. */
  if(!(f=fopen("/cd/DIR-TPAGES.DGO","rb")))return stop(rt,2);
  ok=dc_goal_load_tpage_dir(rt,f,DC_JAK_TPAGE_DIR_BYTES,relocate_dir,NULL);fclose(f);
  dc_level_player_evidence.dir=named(rt,"*texture-page-dir*");
  if(!ok || rt->error!=DC_GOAL_OK)return stop(rt,3);
  levels=named(rt,"*level*");level=levels+5312u;  /* level-group level-default, original IR */
  if(!(f=fopen("/cd/EICHAR.DGO","rb")))return stop(rt,4);
  ok=dc_goal_load_art_dgo(rt,f,DC_JAK_EICHAR_BYTES,relocate_art,&level,&art);fclose(f);
  dc_level_player_evidence.art_linked=art.linked;dc_level_player_evidence.art_relocated=art.relocated;
  if(!ok || rt->error!=DC_GOAL_OK)return stop(rt,5);
  /* The original continue point, then original start with the listener caller. */
  cp=(uint32_t)((one_fn)(uintptr_t)named(rt,"(method get-or-create-continue! game-info)"))(named(rt,"*game-info*"));
  dc_level_player_evidence.continue_point=cp;
  if(rt->error!=DC_GOAL_OK || !cp || cp==rt->s7)return stop(rt,6);
  listener=named(rt,"*listener-process*");bytes=word(rt,listener+80)-(listener-4u);
  start=named(rt,"start");
  if(!start || !dc_goal_note_process_span(rt,listener,bytes,NULL) || !dc_goal_bind_process_context(rt,listener,bytes))
    return stop(rt,7);
  target=(uint32_t)((two_fn)(uintptr_t)start)(dc_goal_find(rt,"play"),cp);
  {enum dc_goal_error e=rt->error;dc_goal_clear_process_context(rt);rt->error=e;}
  dc_level_player_evidence.target=target;
  if(rt->error!=DC_GOAL_OK || !target || target!=named(rt,"*target*"))return stop(rt,8);
  /* Its armed main thread runs on *dram-stack* (start's :stack), imported as
   * the Jak ELF does; the dispatcher resumes it through thread-resume. */
  thread=word(rt,target+40);bytes=word(rt,target+68)+112u;
  dram=named(rt,"*dram-stack*");base=(dram+12u+15u)&~15u;top=(dram+0x8000u)&~15u;
  dc_level_player_evidence.thread=thread;
  if(!dc_goal_fiber_init_main(&target_fiber,rt,target,bytes,thread,base,top-base) ||
     !dc_goal_register_thread_fiber(rt,&target_fiber))return stop(rt,9);
  dc_level_player_evidence.fiber=1;
  dc_level_player_evidence.used_after=rt->used;
  dc_level_player_evidence.free_after=rt->capacity-rt->temporary-rt->used;
  dc_level_player_evidence.stage=10;
  return 1;
}
