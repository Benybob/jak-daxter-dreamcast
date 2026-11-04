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
  uint32_t pad[2];
} dc_level_logic_evidence={.version=2};
typedef char dc_level_logic_evidence_bytes[sizeof dc_level_logic_evidence==128?1:-1];

/* Why actors are or are not born: the switches actors-update reads, the
 * camera, the level's first entity link and the level itself.
 * --dump dc_level_spawn_probe 736 */
volatile struct {
  uint32_t spawn_actors,vis_actors,birth_dist,birth_max,links,links_allocated;
  uint32_t camera[4];
  uint32_t link0[16];
  uint32_t level,birth_named,sticky,pad2[3];
  uint32_t level_words[128];  /* the level object from its tag */
  uint32_t slot[3];           /* bsp-header methods 16..18 */
  char slot_name[3][28];      /* their GOAL function names */
} dc_level_spawn_probe;
typedef char dc_level_spawn_probe_bytes[sizeof dc_level_spawn_probe==736?1:-1];
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
/* Diagnostic: calls of the original (method actors-update level-group), and
 * the level fields its vis branch reads. --dump dc_level_actors_probe 32 */
volatile struct { uint32_t calls,slot,original,display,all_visible,vis_info0,pc_settings,pad; } dc_level_actors_probe;
static uint64_t actors_wrapper(uint64_t group){
  typedef uint64_t (*one_fn)(uint64_t);
  ++dc_level_actors_probe.calls;
  return ((one_fn)(uintptr_t)dc_level_actors_probe.original)(group);
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
   while(pp && pp!=rt->s7 && n<512u){
     uint32_t proc=word(rt,pp);const char *name;unsigned k;
     if(!proc || proc==rt->s7)break;
     if(n<4u){name=dc_goal_symbol_name(rt,word(rt,word(rt,proc-4u)));
       for(k=0;name && name[k] && k<15u;++k)dc_level_logic_evidence.spawned_type[n][k]=name[k];}
     ++n;pp=word(rt,proc+12u);
   }
   dc_level_logic_evidence.spawned=n;rt->error=DC_GOAL_OK;}
  {typedef uint64_t (*zero_fn)(void);
   uint32_t bank=named(rt,"*ACTOR-bank*"),camera_pos=named(rt,"camera-pos"),cam=0;
   uint32_t links_type=named(rt,"entity-links-array"),links=0;
   dc_level_spawn_probe.spawn_actors=named(rt,"*spawn-actors*");
   dc_level_spawn_probe.level=level;
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
