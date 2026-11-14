/* Jak in the level ELF (AI-assisted), ISC.
 * The player path of the Jak ELF, without its arena renderer: load the
 * texture-page directory and eichar through their original relocate methods
 * (/cd/DIR-TPAGES.DGO, /cd/EICHAR.DGO from stage_jak_art.py), take the
  * Geyser Rock's original continue point (training-start), call original start 'play with the listener as the
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
#include <math.h>

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
/* GAME.CGO art groups the level's actors look up in the default level
 * (stage_common_art.py): loaded and relocated like eichar. A file the disc
 * lacks is skipped; loaded counts the ones in. */
volatile struct {uint32_t loaded,missing,failed,bytes;} dc_level_common_art;
static void load_common_art(dc_goal_runtime *rt,uint32_t level){
  static const char *const files[][2]={{"/cd/CRATE.DGO","crate"},{"/cd/MONEY.DGO","money"},
                                       {"/cd/FUELCELL.DGO","fuel-cell"},{"/cd/BUZZER.DGO","buzzer"}};
  unsigned i;
  for(i=0;i<sizeof files/sizeof files[0];++i){
    dc_goal_art_result r;uint32_t root=0,before=rt->used;FILE *f=fopen(files[i][0],"rb");
    if(!f){++dc_level_common_art.missing;continue;}
    if(dc_goal_load_v4_single(rt,f,files[i][0]+4,files[i][1],"art-group",&root,&r) && relocate_art(rt,root,&level)){
      ++dc_level_common_art.loaded;dc_level_common_art.bytes+=rt->used-before;}
    else{++dc_level_common_art.failed;rt->error=DC_GOAL_OK;}
    fclose(f);
  }
}
int dc_level_player(dc_goal_runtime *rt){
  typedef uint64_t (*two_fn)(uint64_t,uint64_t);
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
  load_common_art(rt,level);
  /* No actor heap compaction. Original compaction (actors-update -> compact)
   * copies a live process lower; its suspended code lives on a native fiber
   * stack that cannot move with it, so a moved process never resumes (a
   * training camera's hint stalled that way and held Jak). Geyser Rock's actor
   * heap is about a fifth used. */
  {uint32_t c=dc_goal_find(rt,"*compact-actors*");
   if(!c || !dc_goal_set_symbol(rt,c,rt->s7))return stop(rt,12);}
  /* The original continue point, then original start with the listener caller. */
  /* Geyser Rock's new-game continue point, by the original lookup. */
  {uint32_t name=dc_goal_string(rt,"training-start");
   cp=name?(uint32_t)((two_fn)(uintptr_t)named(rt,"(method get-continue-by-name game-info)"))(named(rt,"*game-info*"),name):0;}
  dc_level_player_evidence.continue_point=cp;
  if(rt->error!=DC_GOAL_OK || !cp || cp==rt->s7)return stop(rt,6);
  /* Every Geyser Rock continue point also wants village1 displayed (lev1, +112
   * retail layout); target-continue loads it beside Geyser Rock. */
  if(word(rt,cp+112u)!=dc_goal_find(rt,"village1"))return stop(rt,11);
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

/* Jak's ground contact each frame, from the original control (the target's
 * root, a collide-shape-moving, +108): trans +12, transv +60, ground-pat +260,
 * status +268 (cshape-moving-flags low word: bit 0 onsurf, bit 1 onground),
 * and the state name (process state +52, state name +0).
 * --dump dc_level_target_trace 6216 */
volatile struct {
  uint32_t version,frames,onsurf_frames,onground_frames,state_count,last_status,last_ground_pat,pad;  /* pad: main re-arms | trans fibers<<16 */
  float first[3],last[3],min_y,max_y;
  char states[8][24];
  float x[DC_LEVEL_TRACE_FRAMES],y[DC_LEVEL_TRACE_FRAMES],z[DC_LEVEL_TRACE_FRAMES];
  uint8_t status[DC_LEVEL_TRACE_FRAMES];
  float speed[DC_LEVEL_TRACE_FRAMES];  /* *cpad-list* pad 0 stick0-speed (+72) */
  uint8_t lefty[DC_LEVEL_TRACE_FRAMES]; /* its hw-cpad lefty byte (+7) */
  uint32_t trans_hook,next_state; /* the target's trans-hook (+56) and next-state (+72) */
  uint8_t state_slot[DC_LEVEL_TRACE_FRAMES]; /* index into states, per frame (0xff none) */
  uint32_t poly_pat[DC_LEVEL_TRACE_FRAMES];  /* the control's poly-pat (+252): the last touched tri's pat-surface */
  float coverage[DC_LEVEL_TRACE_FRAMES],surface_angle[DC_LEVEL_TRACE_FRAMES];  /* control +428, +416 */
} dc_level_target_trace={.version=1};
void dc_level_cache_sample(dc_goal_runtime *rt,uint32_t frame);
/* A test's way to put Jak somewhere: set [1..3] (meters), then [0]; cleared when done. */
volatile float dc_level_warp[4];
void dc_level_player_sample(dc_goal_runtime *rt){
  enum dc_goal_error prior=rt->error;
  uint32_t target=named(rt,"*target*"),root,state,status,i,f;const float *t;const char *name;
  if(!target || target==rt->s7 || (target&3u))goto done;
  root=word(rt,target+108u);
  if(!root || root==rt->s7 || (root&3u) || !(t=dc_goal_resolve(rt,root+12u,12)))goto done;
  if(dc_level_warp[0]!=0){float *to=dc_goal_resolve(rt,root+12u,12);
   for(i=0;i<3u;++i)to[i]=4096.f*dc_level_warp[i+1u];
   dc_level_warp[0]=0;}
  status=word(rt,root+268u);f=dc_level_target_trace.frames;
  if(!f){dc_level_target_trace.first[0]=t[0];dc_level_target_trace.first[1]=t[1];dc_level_target_trace.first[2]=t[2];
         dc_level_target_trace.min_y=dc_level_target_trace.max_y=t[1];}
  dc_level_target_trace.last[0]=t[0];dc_level_target_trace.last[1]=t[1];dc_level_target_trace.last[2]=t[2];
  if(t[1]<dc_level_target_trace.min_y)dc_level_target_trace.min_y=t[1];
  if(t[1]>dc_level_target_trace.max_y)dc_level_target_trace.max_y=t[1];
  if(status&1u)++dc_level_target_trace.onsurf_frames;
  if(status&2u)++dc_level_target_trace.onground_frames;
  dc_level_target_trace.last_status=status;dc_level_target_trace.last_ground_pat=word(rt,root+260u);
  if(f<DC_LEVEL_TRACE_FRAMES){uint32_t c=word(rt,root+428u),a=word(rt,root+416u);memcpy((void *)&dc_level_target_trace.coverage[f],&c,4);memcpy((void *)&dc_level_target_trace.surface_angle[f],&a,4);}
  if(f<DC_LEVEL_TRACE_FRAMES){dc_level_target_trace.poly_pat[f]=word(rt,root+252u);dc_level_target_trace.x[f]=t[0];dc_level_target_trace.y[f]=t[1];dc_level_target_trace.z[f]=t[2];dc_level_target_trace.status[f]=(uint8_t)status;}
  if(f<DC_LEVEL_TRACE_FRAMES){uint32_t list=named(rt,"*cpad-list*"),pad=list?word(rt,list+4u):0;const float *sp;const uint8_t *b;
    if(pad && pad!=rt->s7 && (sp=dc_goal_resolve(rt,pad+72u,4)) && (b=dc_goal_resolve(rt,pad,8))){
      dc_level_target_trace.speed[f]=*sp;dc_level_target_trace.lefty[f]=b[7];}}
  state=word(rt,target+52u);
  name=state>4u && state!=rt->s7?dc_goal_symbol_name(rt,word(rt,state)):NULL;
  if(name){int seen=0;uint32_t n=dc_level_target_trace.state_count;
    for(i=0;i<n && i<8u;++i)if(!strncmp((const char *)dc_level_target_trace.states[i],name,23))seen=1;
    if(!seen && n<8u){unsigned k;for(k=0;name[k]&&k<23u;++k)dc_level_target_trace.states[n][k]=name[k];dc_level_target_trace.state_count=n+1u;}
    if(f<DC_LEVEL_TRACE_FRAMES){dc_level_target_trace.state_slot[f]=0xffu;
      for(i=0;i<dc_level_target_trace.state_count && i<8u;++i)if(!strncmp((const char *)dc_level_target_trace.states[i],name,23))dc_level_target_trace.state_slot[f]=(uint8_t)i;}}
  {extern volatile uint32_t dc_goal_trans_fibers,dc_goal_main_rearms;  /* goal_bridge.c */
   dc_level_target_trace.pad=(dc_goal_main_rearms&0xffffu)|dc_goal_trans_fibers<<16;
   dc_level_target_trace.trans_hook=word(rt,target+56u);dc_level_target_trace.next_state=word(rt,target+72u);}
  dc_level_target_trace.frames=f+1u;
  dc_level_cache_sample(rt,f);
done:
  rt->error=prior;
}

/* Port 0 of the level ELF: neutral, then the stick held forward (up, as on a
 * Dreamcast controller) for DC_LEVEL_WALK_FRAMES, then neutral; frames count
 * the samples of dc_level_target_trace. */
#ifdef DC_LEVEL_PLAY  /* jak-dc-level-play.elf: the controller in port A, live */
#include <dc/maple.h>
#include <dc/maple/controller.h>
volatile int dc_level_play_quit;  /* A+B+X+Y+Start, the console's own reset chord: end the run */
int dc_level_pad_state(int port,dc_goal_pad_state *out){
  maple_device_t *dev;cont_state_t *st;
  memset(out,0,sizeof *out);
  if(port!=0)return 0;
  dev=maple_enum_type(0,MAPLE_FUNC_CONTROLLER);st=dev?(cont_state_t *)maple_dev_status(dev):NULL;
  if(!st)return 0;
  out->buttons=st->buttons;out->ltrig=st->ltrig;out->rtrig=st->rtrig;out->joyx=st->joyx;out->joyy=st->joyy;
  {const uint32_t chord=DC_PAD_A|DC_PAD_B|DC_PAD_X|DC_PAD_Y|DC_PAD_START;
   if((st->buttons&chord)==chord)dc_level_play_quit=1;}
  return 1;
}
#else
int dc_level_pad_state(int port,dc_goal_pad_state *out){
  uint32_t f=dc_level_target_trace.frames;
  memset(out,0,sizeof *out);
  if(port!=0)return 0;
  if(f>=DC_LEVEL_WALK_FIRST && f<DC_LEVEL_WALK_FIRST+DC_LEVEL_WALK_FRAMES)out->joyy=-127;
  return 1;
}
#endif
void dc_level_player_walk(dc_level_walk *out){
  uint32_t n=dc_level_target_trace.frames,i,walk=0xffu;float x0,z0,y0;
  memset(out,0,sizeof *out);
  if(n>DC_LEVEL_TRACE_FRAMES)n=DC_LEVEL_TRACE_FRAMES;
  if(!n)return;
  for(i=0;i<dc_level_target_trace.state_count && i<8u;++i)
    if(!strncmp((const char *)dc_level_target_trace.states[i],"target-walk",23))walk=i;
  x0=dc_level_target_trace.x[0];y0=dc_level_target_trace.y[0];z0=dc_level_target_trace.z[0];
  out->frames=n;out->onsurf_frames=dc_level_target_trace.onsurf_frames;out->onground_frames=dc_level_target_trace.onground_frames;
  for(i=0;i<n;++i){
    float dx=(dc_level_target_trace.x[i]-x0)/4096.f,dz=(dc_level_target_trace.z[i]-z0)/4096.f,dy=(dc_level_target_trace.y[i]-y0)/4096.f;
    float d=sqrtf(dx*dx+dz*dz);
    if(d>out->travel)out->travel=d;
    if(-dy>out->below)out->below=-dy;
    if(dy>out->above)out->above=dy;
    if(dc_level_target_trace.state_slot[i]==walk){++out->walk_frames;if(dc_level_target_trace.status[i]&1u)++out->walk_onsurf;}
  }
}
/* The original *collide-cache* at one walking frame: its tri count, its first
 * 32 tris (three vertices in game units, then pat), Jak's trans and the
 * control's poly-normal (+348). --dump dc_level_cache_snapshot 2096 */
volatile struct { uint32_t frame,num_tris,num_prims,pad;float trans[4],poly_normal[4];float tris[32][16]; } dc_level_cache_snapshot;
void dc_level_cache_sample(dc_goal_runtime *rt,uint32_t frame){
  enum dc_goal_error prior=rt->error;
  uint32_t cache=named(rt,"*collide-cache*"),target=named(rt,"*target*"),root,n;const unsigned char *p;
  if(dc_level_cache_snapshot.frame || frame<60u || !cache || cache==rt->s7 || !target || target==rt->s7)goto done;
  n=word(rt,cache);if(!n || n>461u)goto done;
  root=word(rt,target+108u);
  dc_level_cache_snapshot.frame=frame;dc_level_cache_snapshot.num_tris=n;dc_level_cache_snapshot.num_prims=word(rt,cache+4u);
  if((p=dc_goal_resolve(rt,root+12u,16)))memcpy((void *)dc_level_cache_snapshot.trans,p,16);
  if((p=dc_goal_resolve(rt,root+348u,16)))memcpy((void *)dc_level_cache_snapshot.poly_normal,p,16);
  if(n>32u)n=32u;
  if((p=dc_goal_resolve(rt,cache+4908u,n*64u)))memcpy((void *)dc_level_cache_snapshot.tris,p,n*64u);
done:
  rt->error=prior;
}
