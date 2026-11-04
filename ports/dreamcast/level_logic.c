/* A level's logic BSP through original login (AI-assisted), ISC.
 * After the DGO boot, links /cd/TRA-LOGIC.DGO (build_logic_bsp.py from the
 * owner's TRA.DGO), checks it is an original bsp-header whose drawable trees
 * are the kept logic trees, and calls the original (method login bsp-header).
 * That logs in the drawable-tree-array, then the actor, collide-fragment and
 * ambient trees, then the level's adgif shaders. --dump dc_level_logic_evidence 128 */
#include "level_logic.h"
#include "goal_art.h"
#include "goal_module.h"
#include <string.h>

volatile struct {
  uint32_t version,stage,error,site,sticky;
  uint32_t root,used_before,used_after,payload,pointers,bindings;
  uint32_t trees,login_entry,login_result,used_after_login;
  uint32_t tree_type[8];
  uint32_t real_types,pad[8];
} dc_level_logic_evidence={.version=1};
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
int dc_level_logic(dc_goal_runtime *rt){
  typedef uint64_t (*one_fn)(uint64_t);
  dc_goal_art_result result;dc_goal_code_entry_info info;
  uint32_t root=0,type,trees,n,i,login;FILE *f;int ok;
  if(!rt || rt->error!=DC_GOAL_OK)return 0;
  dc_level_logic_evidence.used_before=rt->used;
  f=fopen("/cd/TRA-LOGIC.DGO","rb");
  if(!f)return stop(rt,1);
  ok=dc_goal_load_v4_single(rt,f,"TRA-LOGIC.DGO","training-vis","bsp-header",&root,&result);
  fclose(f);
  dc_level_logic_evidence.payload=result.payload;dc_level_logic_evidence.pointers=result.pointers;
  dc_level_logic_evidence.bindings=result.bindings;dc_level_logic_evidence.used_after=rt->used;
  if(!ok)return stop(rt,2);
  dc_level_logic_evidence.root=root;
  type=named(rt,"bsp-header");
  if(!type || word(rt,root-4)!=type)return stop(rt,3);
  /* drawable-trees is at +40 from the tag; drawable-group length (int16) at +6
   * and data[] at +32, both from that array's tag. */
  trees=word(rt,root+36);
  {const unsigned char *p=dc_goal_resolve(rt,trees+2,2);n=p?(uint32_t)(p[0]|p[1]<<8):0;}
  dc_level_logic_evidence.trees=n;
  if(word(rt,trees-4)!=named(rt,"drawable-tree-array") || n<1 || n>8)return stop(rt,4);
  for(i=0;i<n;++i)dc_level_logic_evidence.tree_type[i]=word(rt,word(rt,trees+28+4*i)-4);
  login=dc_goal_get_method(rt,type,9);
  dc_level_logic_evidence.login_entry=login;
  if(!dc_goal_code_entry(rt,login,&info) || info.symbol!=dc_goal_find(rt,"(method login bsp-header)"))
    return stop(rt,5);
  dc_level_logic_evidence.login_result=(uint32_t)((one_fn)(uintptr_t)login)(root);
  dc_level_logic_evidence.used_after_login=rt->used;
  if(rt->error!=DC_GOAL_OK || dc_level_logic_evidence.login_result!=root)return stop(rt,6);
  /* The level's own actor types are the deftypes of its code, not forward
   * types (a forward type has no parent). */
  {static const char *const own[]={"tra-pontoon","training-cam","scarecrow-a","scarecrow-b",
                                   "tra-iris-door","training-water","training-part"};
   for(i=0;i<sizeof own/sizeof own[0];++i){
     uint32_t t=named(rt,own[i]);
     if(t && word(rt,t+4)!=0)++dc_level_logic_evidence.real_types;
   }
   if(dc_level_logic_evidence.real_types!=sizeof own/sizeof own[0])return stop(rt,8);}
  dc_level_logic_evidence.stage=7;
  return 1;
}
