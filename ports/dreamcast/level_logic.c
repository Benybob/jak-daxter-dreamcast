/* Geyser Rock through original level loading (AI-assisted), ISC.
 * The DGO boot's display-loop runs original level-update. Before it, this asks
 * the original load-state for 'training, displayed: load-state update! then
 * calls level-get-for-use, which allocates the level heaps, load-begin sends the
 * RPC-DGO request (served by level_dgo_rpc.c from /cd/TRA.DGO, the logic BSP),
 * load-continue links it (bsp-header relocate attaches it to the loading level),
 * login-begin and level-update-after-load log it in, and a later frame births it
 * and makes it active. The level's code (TRA-CODE.DGO) is loaded at boot first.
 * --dump dc_level_logic_evidence 128 */
#include "level_logic.h"
#include "goal_module.h"
#include <arch/cache.h>
#include <string.h>

volatile struct {
  uint32_t version,stage,error,site,sticky;
  uint32_t level,status,bsp,bsp_type_ok,bsp_level,want_result;
  uint32_t real_types,actors,spawned;
  char spawned_type[4][16];
  uint32_t village1_status,village1_heap_used;  /* the level every Geyser Rock continue point also displays */
} dc_level_logic_evidence={.version=3};
typedef char dc_level_logic_evidence_bytes[sizeof dc_level_logic_evidence==128?1:-1];

/* --dump dc_level_ready_probe 376: frames sampled, the most 'ready actors in a
 * frame, the most still 'ready since the previous frame, last frame's list. */
volatile struct { uint32_t frames,ready_max,stuck_max,count;uint32_t ready[64];int pad[2];char type[4][20];uint32_t first_stuck_frame,pad2[3]; } dc_level_ready_probe;
/* Why actors are or are not born: the switches actors-update reads, the
 * camera, the level's first entity link and the level itself.
 * --dump dc_level_spawn_probe 760 */
volatile struct {
  uint32_t spawn_actors,vis_actors,birth_dist,birth_max,links,links_allocated;
  uint32_t camera[4];
  uint32_t link0[16];
  uint32_t level,birth_named,sticky,heap_used;  /* heap_used: Geyser Rock's level heap */
  uint32_t actor_heap_used,actor_heap_total;     /* original memory-used / memory-total of *nk-dead-pool* */
  uint32_t art_groups,art_typed;                 /* Geyser Rock's level art-group load-dir: entries, art-groups */
  uint32_t drawn_actors,art_error_actors;        /* *entity-pool* actors with a draw-control / in process-drawable-art-error */
  uint32_t ready_actors;                         /* *entity-pool* processes still 'ready (status +32) */
  uint32_t ran_actors;                           /* ... that ran their state code and suspended */
  uint32_t level_words[128];  /* the level object from its tag */
  uint32_t slot[3];           /* bsp-header methods 16..18 */
  char slot_name[3][28];      /* their GOAL function names */
} dc_level_spawn_probe;
typedef char dc_level_spawn_probe_bytes[sizeof dc_level_spawn_probe==760?1:-1];
static uint32_t named(dc_goal_runtime *rt,const char *name){
  uint32_t s=dc_goal_find(rt,name);return s?dc_goal_symbol_value(rt,s):0;
}
static uint32_t word(dc_goal_runtime *rt,uint32_t at){
  uint32_t v=0;const void *p=dc_goal_resolve(rt,at,4);if(p)memcpy(&v,p,4);return v;
}
static int stop(dc_goal_runtime *rt,uint32_t stage){
  dc_level_logic_evidence.stage=stage;dc_level_logic_evidence.error=rt->error;
  dc_level_logic_evidence.site=dc_goal_error_site;dc_level_logic_evidence.sticky=dc_goal_sticky_error;
  return 0;
}
/* Diagnostic wrapper around the original (method birth bsp-header): the level's
 * entity field (+280) and the runtime error before and after it.
 * --dump dc_level_birth_probe 32 */
volatile struct { uint32_t calls,bsp,entity_before,entity_after,error_before,error_after,ambient_after,original; } dc_level_birth_probe;
static dc_goal_runtime *birth_runtime;
static uint64_t birth_wrapper(uint64_t bsp){
  typedef uint64_t (*one_fn)(uint64_t);
  dc_goal_runtime *rt=birth_runtime;uint64_t out;uint32_t level;
  ++dc_level_birth_probe.calls;dc_level_birth_probe.bsp=(uint32_t)bsp;
  level=word(rt,(uint32_t)bsp+120u);
  dc_level_birth_probe.entity_before=word(rt,level+280u);dc_level_birth_probe.error_before=rt->error;
  out=((one_fn)(uintptr_t)dc_level_birth_probe.original)(bsp);
  dc_level_birth_probe.entity_after=word(rt,level+280u);dc_level_birth_probe.ambient_after=word(rt,level+284u);
  dc_level_birth_probe.error_after=rt->error;
  return out;
}
/* actors-update calls that leave a runtime error (births run inside it), the
 * first error, its site and call. --dump dc_level_actor_errors 32 */
volatile struct { uint32_t count,first_error,first_site,first_call,last_error,pad[3]; } dc_level_actor_errors;
/* Diagnostic: calls of the original (method actors-update level-group), and
 * the level fields its vis branch reads. --dump dc_level_actors_probe 32 */
volatile struct { uint32_t calls,slot,original,display,all_visible,vis_info0,pc_settings,pad; } dc_level_actors_probe;
/* Jak after the frames: original (target-pos 0) and his state's name, and the
 * continue point, and the camera master and combiner.
 * --dump dc_level_target_probe 104 */
volatile struct { uint32_t target,state,pos[4]; char state_name[24]; uint32_t camera,combiner; char camera_state[24],combiner_state[24]; } dc_level_target_probe;
/* Children of *camera-pool* after the frames: type and state names.
 * --dump dc_level_camera_pool_probe 260 */
volatile struct { uint32_t count; struct { char type[24]; char state[24]; uint32_t status,pad; } row[5]; } dc_level_camera_pool_probe;
/* Level heaps sized per slot (2026-11-06). Original alloc-levels! takes two
 * equal heaps from the global heap (LEVEL_HEAP_SIZE, 1,664 KiB in
 * dreamcast_memory_budget.gc, so either level fits either). Here Geyser Rock,
 * wanted first, lands in level0 and needs 955,584 bytes; village1, which its
 * continue point adds, lands in level1 and needs 1,526,064. Sizing level0 for
 * Geyser Rock returns 640 KiB to the arena for the renderer. They are taken
 * before the first frame, so alloc-levels! (which allocates only while level0's
 * heap base is 0) keeps them; the art and common packages it would also note
 * are recorded by the DGO boot. dc_level_check confirms which level is where. */
enum {LEVEL0_HEAP_BYTES=1024u*1024u,LEVEL1_HEAP_BYTES=1664u*1024u};
static int alloc_level_heaps(dc_goal_runtime *rt){
  static const uint32_t slot[2]={96u,2704u},bytes[2]={LEVEL0_HEAP_BYTES,LEVEL1_HEAP_BYTES};  /* level-group level0/1 */
  uint32_t group=named(rt,"*level*"),i;
  for(i=0;i<2u;++i){
    uint32_t base=dc_goal_alloc(rt,bytes[i]),heap[4];unsigned char *fields;
    if(!base || !group || !(fields=dc_goal_resolve(rt,group+slot[i]+28u,16)))return 0;  /* level heap, kheap at +28 */
    heap[0]=base;heap[1]=base+bytes[i];heap[2]=base;heap[3]=base+bytes[i];  /* base top current top-base */
    memcpy(fields,heap,16);
  }
  return 1;
}
/* Bytes a level has taken from its level heap (the inline kheap at +28). */
static uint32_t heap_used(dc_goal_runtime *rt,uint32_t level){
  uint32_t base=word(rt,level+28u),current=word(rt,level+36u);
  return base && current>=base?current-base:0;
}
/* The first actors under *entity-pool*: type, state name (or status when it has
 * no state) and draw (bit 0: a draw-control; bits 8..31: process mask). --dump dc_level_actor_table 1060 */
volatile struct { uint32_t count; struct { char type[20],state[20]; uint32_t draw; } row[24]; } dc_level_actor_table;
static uint64_t actors_wrapper(uint64_t group){
  typedef uint64_t (*one_fn)(uint64_t);
  uint64_t out;
  ++dc_level_actors_probe.calls;
  if(birth_runtime && birth_runtime->error==DC_GOAL_OK && !dc_level_actor_errors.first_error)dc_goal_error_site=0;
  out=((one_fn)(uintptr_t)dc_level_actors_probe.original)(group);
  if(birth_runtime && birth_runtime->error!=DC_GOAL_OK){
    ++dc_level_actor_errors.count;dc_level_actor_errors.last_error=birth_runtime->error;
    if(!dc_level_actor_errors.first_error){dc_level_actor_errors.first_error=birth_runtime->error;
      dc_level_actor_errors.first_site=dc_goal_error_site;dc_level_actor_errors.first_call=dc_level_actors_probe.calls;}}
  return out;
}
static void sync_code(uint32_t allocation,uint32_t bytes,void *context){
  (void)context;dcache_flush_range(allocation,bytes);icache_flush_range(allocation,bytes);
}
static void wrap_actors_update(dc_goal_runtime *rt){
  static unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  dc_goal_code_export fn={"dc-level-actors-wrapper",code,12,NULL,0};dc_goal_module_result loaded;
  uint32_t type=named(rt,"level-group"),want=dc_goal_find(rt,"(method actors-update level-group)"),i;
  uint32_t target=(uint32_t)(uintptr_t)actors_wrapper;
  for(i=9;i<64 && !dc_level_actors_probe.slot;++i){
    dc_goal_code_entry_info info;uint32_t m=dc_goal_get_method(rt,type,(uint16_t)i);
    if(m && dc_goal_code_entry(rt,m,&info) && info.symbol==want){dc_level_actors_probe.slot=i;dc_level_actors_probe.original=m;}
    rt->error=DC_GOAL_OK;
  }
  if(!dc_level_actors_probe.slot)return;
  memcpy(code+8,&target,4);
  if(dc_goal_load_module(rt,&fn,1,sync_code,NULL,&loaded))
    dc_goal_set_method(rt,type,(uint16_t)dc_level_actors_probe.slot,named(rt,"dc-level-actors-wrapper"));
  rt->error=DC_GOAL_OK;
}
static void wrap_birth(dc_goal_runtime *rt){
  static unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  dc_goal_code_export fn={"dc-level-birth-wrapper",code,12,NULL,0};dc_goal_module_result loaded;
  uint32_t type=named(rt,"bsp-header"),target=(uint32_t)(uintptr_t)birth_wrapper;
  birth_runtime=rt;dc_level_birth_probe.original=dc_goal_get_method(rt,type,18);
  memcpy(code+8,&target,4);
  if(dc_goal_load_module(rt,&fn,1,sync_code,NULL,&loaded))
    dc_goal_set_method(rt,type,18,named(rt,"dc-level-birth-wrapper"));
  rt->error=DC_GOAL_OK;
}
int dc_level_want(dc_goal_runtime *rt){
  typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);
  uint32_t state=named(rt,"*load-state*"),want=named(rt,"(method want-levels load-state)");
  uint32_t display=named(rt,"(method want-display-level load-state)"),training=dc_goal_intern(rt,"training");
  if(!rt || rt->error!=DC_GOAL_OK || !state || !want || !display || !training)return stop(rt,1);
  if(!alloc_level_heaps(rt))return stop(rt,14);
  wrap_birth(rt);wrap_actors_update(rt);
  dc_level_logic_evidence.want_result=(uint32_t)((three_fn)(uintptr_t)want)(state,training,rt->s7);
  ((three_fn)(uintptr_t)display)(state,training,dc_goal_intern(rt,"display"));
  if(rt->error!=DC_GOAL_OK)return stop(rt,2);
  dc_level_logic_evidence.stage=3;
  return 1;
}
int dc_level_check(dc_goal_runtime *rt){
  typedef uint64_t (*two_fn)(uint64_t,uint64_t);
  uint32_t group=named(rt,"*level*"),get=named(rt,"(method level-get level-group)");
  uint32_t status=named(rt,"(method level-status level-group)"),training=dc_goal_find(rt,"training");
  uint32_t level,bsp,i,types=named(rt,"bsp-header");
  if(!rt || !group || !get || !status || !training)return stop(rt,4);
  dc_level_logic_evidence.status=(uint32_t)((two_fn)(uintptr_t)status)(group,training);
  level=(uint32_t)((two_fn)(uintptr_t)get)(group,training);
  dc_level_logic_evidence.level=level;
  if(rt->error!=DC_GOAL_OK || dc_level_logic_evidence.status!=dc_goal_find(rt,"active"))return stop(rt,5);
  dc_level_spawn_probe.heap_used=heap_used(rt,level);
  {uint32_t group_=named(rt,"*level*");  /* the slot each level is in (see alloc_level_heaps) */
   if(level!=group_+96u)return stop(rt,15);}
  {typedef uint64_t (*one_fn)(uint64_t);
   uint32_t pool=named(rt,"*nk-dead-pool*");
   uint32_t used=named(rt,"(method memory-used dead-pool-heap)"),total=named(rt,"(method memory-total dead-pool-heap)");
   if(pool && used)dc_level_spawn_probe.actor_heap_used=(uint32_t)((one_fn)(uintptr_t)used)(pool);
   if(pool && total)dc_level_spawn_probe.actor_heap_total=(uint32_t)((one_fn)(uintptr_t)total)(pool);
   rt->error=DC_GOAL_OK;}
  /* village1, wanted by Jak's continue point, loads beside it. */
  {uint32_t village1=dc_goal_find(rt,"village1"),other;
   if(!village1)return stop(rt,9);
   dc_level_logic_evidence.village1_status=(uint32_t)((two_fn)(uintptr_t)status)(group,village1);
   other=(uint32_t)((two_fn)(uintptr_t)get)(group,village1);
   if(other && other!=rt->s7)dc_level_logic_evidence.village1_heap_used=heap_used(rt,other);
   rt->error=DC_GOAL_OK;
   if(dc_level_logic_evidence.village1_status!=dc_goal_find(rt,"active"))return stop(rt,10);}
  /* Geyser Rock's art groups, linked ahead of its BSP from TRAINING.DGO: original
   * art-group relocate adds each to the loading level's load-dir (level art-group,
   * +48; its art-group-array, +8), which level login then logs in. */
  {uint32_t dir=word(rt,level+48u),array=dir?word(rt,dir+8u):0,type=named(rt,"art-group"),n;
   dc_level_spawn_probe.art_groups=n=array?word(rt,array):0;
   for(i=0;i<n && i<64u;++i){
     uint32_t art=word(rt,array+12u+4u*i);
     if(art && art!=rt->s7 && word(rt,art-4u)==type)++dc_level_spawn_probe.art_typed;
   }
   rt->error=DC_GOAL_OK;
   if(n!=11u || dc_level_spawn_probe.art_typed!=11u)return stop(rt,11);}
  /* Find the level's bsp field by its contents: a bsp-header whose level field
   * (+120 from the object, retail layout) is this level. */
  for(i=0;i<256u && !dc_level_logic_evidence.bsp;i+=4){
    uint32_t candidate=word(rt,level+i);
    if(candidate>4u && candidate!=rt->s7 && word(rt,candidate-4u)==types && word(rt,candidate+120u)==level)
      dc_level_logic_evidence.bsp=candidate;
  }
  rt->error=DC_GOAL_OK;
  bsp=dc_level_logic_evidence.bsp;
  if(!bsp)return stop(rt,6);
  dc_level_logic_evidence.bsp_type_ok=1;dc_level_logic_evidence.bsp_level=word(rt,bsp+120u);
  /* actors (+108): a drawable-inline-array-actor, length int16 at +2. */
  {uint32_t actors=word(rt,bsp+108u);const unsigned char *p=actors?dc_goal_resolve(rt,actors+2u,2):NULL;
   dc_level_logic_evidence.actors=p?(uint32_t)(p[0]|p[1]<<8):0;}
  rt->error=DC_GOAL_OK;
  /* The level's own actor types are its code's deftypes (a forward type has no parent). */
  {static const char *const own[]={"tra-pontoon","training-cam","scarecrow-a","scarecrow-b",
                                   "tra-iris-door","training-water","training-part"};
   for(i=0;i<sizeof own/sizeof own[0];++i){
     uint32_t t=named(rt,own[i]);
     if(t && word(rt,t+4)!=0)++dc_level_logic_evidence.real_types;
   }
   if(dc_level_logic_evidence.real_types!=sizeof own/sizeof own[0])return stop(rt,7);}
  /* Actors the original actors-update births join *entity-pool*: count its
   * children (process-tree: brother +12, child +16, both ppointers). */
  {uint32_t pool=named(rt,"*entity-pool*"),pp=pool?word(rt,pool+16u):0,n=0;
   uint32_t draw_type=named(rt,"draw-control"),art_error=dc_goal_find(rt,"process-drawable-art-error"),ready=dc_goal_find(rt,"ready"),suspended=dc_goal_find(rt,"suspended");
   while(pp && pp!=rt->s7 && n<512u){
     uint32_t proc=word(rt,pp),draw,state;const char *name;unsigned k;
     if(!proc || proc==rt->s7)break;
     if(n<4u){name=dc_goal_symbol_name(rt,word(rt,word(rt,proc-4u)));
       for(k=0;name && name[k] && k<15u;++k)dc_level_logic_evidence.spawned_type[n][k]=name[k];}
     /* initialize-skeleton makes a draw-control (process-drawable draw, +116)
      * from the actor's art group, or goes to process-drawable-art-error. */
     draw=word(rt,proc+116u);state=word(rt,proc+52u);
     if(draw>4u && draw!=rt->s7 && word(rt,draw-4u)==draw_type)++dc_level_spawn_probe.drawn_actors;
     if(state>4u && state!=rt->s7 && word(rt,state)==art_error)++dc_level_spawn_probe.art_error_actors;
     if(word(rt,proc+32u)==ready)++dc_level_spawn_probe.ready_actors;  /* activated, init never run */
     if(word(rt,proc+32u)==suspended)++dc_level_spawn_probe.ran_actors;
     if(dc_level_actor_table.count<24u){
       uint32_t r=dc_level_actor_table.count++;const char *t=dc_goal_symbol_name(rt,word(rt,word(rt,proc-4u)));
       const char *st=state>4u && state!=rt->s7?dc_goal_symbol_name(rt,word(rt,state)):dc_goal_symbol_name(rt,word(rt,proc+32u));
       for(k=0;t && t[k] && k<19u;++k)dc_level_actor_table.row[r].type[k]=t[k];
       for(k=0;st && st[k] && k<19u;++k)dc_level_actor_table.row[r].state[k]=st[k];
       dc_level_actor_table.row[r].draw=(draw>4u && draw!=rt->s7 && word(rt,draw-4u)==draw_type)|(word(rt,proc+4u)&0xffffffu)<<8;}  /* draw | mask<<8 */
     ++n;pp=word(rt,proc+12u);
   }
   dc_level_logic_evidence.spawned=n;rt->error=DC_GOAL_OK;
   /* Every born actor ran its original init (none left 'ready), and actors with
    * a Geyser Rock art group made their draw-control from it. */
   /* (as Jak moves, actors-update keeps birthing: one born in the last frame is
    * still 'ready, so the per-frame sample asks that none stays 'ready a frame) */
   if(dc_level_ready_probe.stuck_max || !dc_level_ready_probe.frames || !dc_level_spawn_probe.drawn_actors)return stop(rt,12);
   /* and the dispatcher runs them: they reach their states and suspend. */
   if(!dc_level_spawn_probe.ran_actors)return stop(rt,13);}
  {typedef uint64_t (*zero_fn)(void);
   uint32_t bank=named(rt,"*ACTOR-bank*"),camera_pos=named(rt,"camera-pos"),cam=0;
   uint32_t links_type=named(rt,"entity-links-array"),links=0;
   dc_level_spawn_probe.spawn_actors=named(rt,"*spawn-actors*");
   dc_level_spawn_probe.level=level;
   {uint32_t pool=named(rt,"*camera-pool*"),pp=pool?word(rt,pool+16u):0,n=0;
    while(pp && pp!=rt->s7 && n<16u){
      uint32_t proc=word(rt,pp);const char *nm;unsigned k;
      if(!proc || proc==rt->s7)break;
      if(n<5u){
        nm=dc_goal_symbol_name(rt,word(rt,word(rt,proc-4u)));
        for(k=0;nm && nm[k] && k<23u;++k)dc_level_camera_pool_probe.row[n].type[k]=nm[k];
        nm=word(rt,proc+52u)!=rt->s7?dc_goal_symbol_name(rt,word(rt,word(rt,proc+52u))):NULL;
        for(k=0;nm && nm[k] && k<23u;++k)dc_level_camera_pool_probe.row[n].state[k]=nm[k];
        dc_level_camera_pool_probe.row[n].status=word(rt,proc+32u);
        nm=dc_goal_symbol_name(rt,dc_level_camera_pool_probe.row[n].status);
        if(nm && !dc_level_camera_pool_probe.row[n].state[0])
          for(k=0;nm[k] && k<22u;++k)dc_level_camera_pool_probe.row[n].state[k+1u]=nm[k];
      }
      ++n;pp=word(rt,proc+12u);
    }
    dc_level_camera_pool_probe.count=n;rt->error=DC_GOAL_OK;}
   {typedef uint64_t (*one_fn)(uint64_t);uint32_t t=named(rt,"*target*"),fn=named(rt,"target-pos"),v;
    const char *n;unsigned k;
    dc_level_target_probe.target=t;
    if(t && t!=rt->s7){
      dc_level_target_probe.state=word(rt,t+52u);
      n=dc_goal_symbol_name(rt,word(rt,dc_level_target_probe.state));
      for(k=0;n && n[k] && k<23u;++k)dc_level_target_probe.state_name[k]=n[k];
      if(fn){v=(uint32_t)((one_fn)(uintptr_t)fn)(0);for(k=0;k<4u;++k)dc_level_target_probe.pos[k]=word(rt,v+4u*k);}
    }
    {uint32_t c=named(rt,"*camera*"),m=named(rt,"*camera-combiner*");
     dc_level_target_probe.camera=c;dc_level_target_probe.combiner=m;
     if(c && c!=rt->s7){n=dc_goal_symbol_name(rt,word(rt,word(rt,c+52u)));
       for(k=0;n && n[k] && k<23u;++k)dc_level_target_probe.camera_state[k]=n[k];}
     if(m && m!=rt->s7){n=dc_goal_symbol_name(rt,word(rt,word(rt,m+52u)));
       for(k=0;n && n[k] && k<23u;++k)dc_level_target_probe.combiner_state[k]=n[k];}}
    rt->error=DC_GOAL_OK;}
   dc_level_actors_probe.display=word(rt,level+372u);dc_level_actors_probe.all_visible=word(rt,level+392u);
   dc_level_actors_probe.vis_info0=word(rt,level+404u);dc_level_actors_probe.pc_settings=named(rt,"*pc-settings*");
   dc_level_spawn_probe.birth_named=named(rt,"(method birth bsp-header)");
   dc_level_spawn_probe.sticky=dc_goal_sticky_error;
   for(i=0;i<3u;++i){
     dc_goal_code_entry_info info;const char *n=NULL;unsigned k;
     dc_level_spawn_probe.slot[i]=dc_goal_get_method(rt,types,(uint16_t)(16u+i));
     if(dc_goal_code_entry(rt,dc_level_spawn_probe.slot[i],&info))n=dc_goal_symbol_name(rt,info.symbol);
     for(k=0;n && n[k] && k<27u;++k)dc_level_spawn_probe.slot_name[i][k]=n[k];
     rt->error=DC_GOAL_OK;
   }
   for(i=0;i<128u;++i)dc_level_spawn_probe.level_words[i]=word(rt,level-4u+4u*i);
   dc_level_spawn_probe.vis_actors=named(rt,"*vis-actors*");
   if(bank){dc_level_spawn_probe.birth_dist=word(rt,bank+4u);dc_level_spawn_probe.birth_max=word(rt,bank+8u);}
   if(camera_pos)cam=(uint32_t)((zero_fn)(uintptr_t)camera_pos)();
   for(i=0;i<4u && cam;++i)dc_level_spawn_probe.camera[i]=word(rt,cam+4u*i);
   for(i=0;i<8192u && !links;i+=4){
     uint32_t candidate=word(rt,level+i);
     if(candidate>4u && candidate!=rt->s7 && word(rt,candidate-4u)==links_type)links=candidate;
   }
   if(links){
     dc_level_spawn_probe.links=word(rt,links);dc_level_spawn_probe.links_allocated=word(rt,links+4u);
     for(i=0;i<16u;++i)dc_level_spawn_probe.link0[i]=word(rt,links+12u+4u*i);
   }
   rt->error=DC_GOAL_OK;}
  dc_level_logic_evidence.stage=8;
  return 1;
}
void dc_level_actors_sample(dc_goal_runtime *rt){
  enum dc_goal_error prior=rt->error;
  uint32_t pool=named(rt,"*entity-pool*"),pp=pool?word(rt,pool+16u):0,ready=dc_goal_find(rt,"ready");
  uint32_t now[64],count=0,stuck=0,n=0,i;
  while(pp && pp!=rt->s7 && n<512u){
    uint32_t proc=word(rt,pp);
    if(!proc || proc==rt->s7)break;
    if(word(rt,proc+32u)==ready && count<64u){
      for(i=0;i<dc_level_ready_probe.count;++i)if(dc_level_ready_probe.ready[i]==proc){
        ++stuck;if(!dc_level_ready_probe.first_stuck_frame)dc_level_ready_probe.first_stuck_frame=dc_level_ready_probe.frames;}
      if(count<4u){const char *t=dc_goal_symbol_name(rt,word(rt,word(rt,proc-4u)));unsigned k;
        for(k=0;k<20u;++k)dc_level_ready_probe.type[count][k]=0;
        for(k=0;t && t[k] && k<19u;++k)dc_level_ready_probe.type[count][k]=t[k];}
      now[count++]=proc;}
    ++n;pp=word(rt,proc+12u);
  }
  for(i=0;i<count;++i)dc_level_ready_probe.ready[i]=now[i];
  dc_level_ready_probe.count=count;++dc_level_ready_probe.frames;
  if(count>dc_level_ready_probe.ready_max)dc_level_ready_probe.ready_max=count;
  if(stuck>dc_level_ready_probe.stuck_max)dc_level_ready_probe.stuck_max=stuck;
  rt->error=prior;
}
