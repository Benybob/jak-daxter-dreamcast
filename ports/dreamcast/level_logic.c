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
#include <string.h>

volatile struct {
  uint32_t version,stage,error,site,sticky;
  uint32_t level,status,bsp,bsp_type_ok,bsp_level,want_result;
  uint32_t real_types,actors,pad[19];
} dc_level_logic_evidence={.version=2};
typedef char dc_level_logic_evidence_bytes[sizeof dc_level_logic_evidence==128?1:-1];

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
int dc_level_want(dc_goal_runtime *rt){
  typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);
  uint32_t state=named(rt,"*load-state*"),want=named(rt,"(method want-levels load-state)");
  uint32_t display=named(rt,"(method want-display-level load-state)"),training=dc_goal_intern(rt,"training");
  if(!rt || rt->error!=DC_GOAL_OK || !state || !want || !display || !training)return stop(rt,1);
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
  dc_level_logic_evidence.stage=8;
  return 1;
}
