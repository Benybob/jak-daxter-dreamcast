/* Unchanged original fixed-size process pool methods (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "build/original_dead_pool.h"
#include "build/original_dead_pool_schema.h"
#include "build/dead_pool_fixture.h"
typedef struct {uint32_t name,mask,parent,brother,child,ppointer,self;} tree;
typedef struct {
 tree t;uint32_t pool,status,pid,main_thread,top_thread,entity,state,trans_hook,post_hook,event_hook;
 uint32_t allocated_length,next_state,heap_base,heap_top,heap_cur,stack_frame_top;unsigned char connection_list[16];
} process;
typedef char tree_size[sizeof(tree)==28?1:-1];
typedef char process_size[sizeof(process)==108?1:-1];
static uint32_t entry(dc_goal_runtime *r,const char *n){return dc_goal_symbol_value(r,dc_goal_find(r,n));}
static uint32_t word(dc_goal_runtime *r,uint32_t at){uint32_t v=0;const void *p=dc_goal_resolve(r,at,4);if(p)memcpy(&v,p,4);return v;}
uint32_t dc_runtime_dead_pool_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
 unsigned *id,dc_goal_cache_sync sync) {
 dc_goal_module_result original={0},fixture={0},bridge={0};
 uint32_t parent=entry(rt,"process-tree"),proc_type=entry(rt,"process"),type=0,symbol;
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_tree=dc_goal_get_method(rt,parent,0);
 uint32_t old_process=dc_goal_get_method(rt,proc_type,0),old_new=0,old_return=0;
 uint32_t pools[3]={0},members[3]={0},names[3],counts[3]={0,1,3},dead=dc_goal_find(rt,"dead"),i,j;
 typedef uint64_t (*create_fn)(uint64_t,uint64_t,uint64_t);
 typedef uint64_t (*one_fn)(uint64_t);
 typedef uint64_t (*two_fn)(uint64_t,uint64_t);
 typedef void (*return_fn)(uint64_t,uint64_t);
 create_fn create;return_fn give_back;
#define CHECK(a,b,text) check((uint64_t)(a),(uint64_t)(b),++*id,text,context)
 CHECK(DC_ORIGINAL_DEAD_POOL_SIZE,32,"original compiler pool type has only the tree prefix");
 CHECK(DC_ORIGINAL_DEAD_POOL_METHODS,16,"original pool adds two methods to the fourteen-slot parent");
 symbol=dc_goal_intern(rt,"dead-pool");
 type=dc_goal_new_type(rt,symbol,parent,DC_ORIGINAL_DEAD_POOL_FLAGS);
 CHECK(type!=0,1,"original compiler type flags install the derived pool metadata");
 if(!type)return 0;
 CHECK(dc_goal_type_of(rt,type),rt->type_type,"pool metadata is a real runtime type");
 CHECK(word(rt,type+4),parent,"pool type has the original process-tree parent");
 old_new=dc_goal_get_method(rt,type,0);old_return=dc_goal_get_method(rt,type,15);
 CHECK(dc_goal_load_module(rt,goal_original_dead_pool_exports,GOAL_ORIGINAL_DEAD_POOL_EXPORT_COUNT,sync,NULL,&original),1,"two unchanged original pool methods link");
 CHECK(dc_goal_load_module(rt,goal_dead_pool_fixture_exports,GOAL_DEAD_POOL_FIXTURE_EXPORT_COUNT,sync,NULL,&fixture),1,"typed pool new and return dispatch wrappers link");
 CHECK(dc_goal_install_object_bridge(rt,sync,&bridge),1,"bounded global allocation bridge is explicit");
 CHECK(original.functions,2,"two original pool exports");CHECK(fixture.functions,2,"two typed dispatch exports");
 if(original.functions!=2||fixture.functions!=2||!bridge.functions)goto restore;
 CHECK(dc_goal_set_method(rt,parent,0,entry(rt,"(method new process-tree)")),1,"original parent constructor installed");
 CHECK(dc_goal_set_method(rt,proc_type,0,entry(rt,"(method new process)")),1,"original process constructor installed");
 CHECK(dc_goal_set_method(rt,type,0,entry(rt,"(method new dead-pool)")),1,"original pool constructor installed at new slot");
 CHECK(dc_goal_set_method(rt,type,15,entry(rt,"(method return-process dead-pool)")),1,"original return-process installed at typed slot fifteen");
 create=(create_fn)(uintptr_t)entry(rt,"dc-dead-pool-create");give_back=(return_fn)(uintptr_t)entry(rt,"dc-dead-pool-return");
 names[0]=dc_goal_intern(rt,"dc-empty-pool");names[1]=dc_goal_intern(rt,"dc-single-pool");names[2]=dc_goal_intern(rt,"dc-three-pool");
 for(i=0;i<3;++i) {
  uint32_t used=rt->used,handle;tree *t;
  pools[i]=(uint32_t)create(counts[i],256,names[i]);
  CHECK(pools[i]!=0,1,"original constructor returns a pool through typed GOAL dispatch");
  if(!pools[i])goto restore;
  t=dc_goal_resolve(rt,pools[i],sizeof(*t));if(!t)goto restore;
  CHECK(dc_goal_type_of(rt,pools[i]),type,"constructed pool has original derived type");
  CHECK(rt->used-used,32+counts[i]*368,"pool and fixed-size process reservations have exact aligned footprint");
  CHECK(t->name,names[i],"original pool naming retained");CHECK(t->mask,256,"original organizational pool mask");
  CHECK(t->parent,rt->s7,"new pool is not attached implicitly");CHECK(t->brother,rt->s7,"new pool has no organizational sibling");
  CHECK(t->self,pools[i],"pool self identity");CHECK(t->ppointer,pools[i]+24,"pool stable self handle");
  handle=t->child;
  for(j=0;j<counts[i];++j) {
   uint32_t value=word(rt,handle);process *p=dc_goal_resolve(rt,value,sizeof(*p));
   CHECK(p!=NULL,1,"pool child resolves to a complete original process");if(!p)goto restore;
   if(i==2)members[j]=value;
   CHECK(dc_goal_type_of(rt,value),proc_type,"member is an original base process");
   CHECK(handle,value+24,"member is linked by its stable self handle");CHECK(p->t.self,value,"member self identity");
   CHECK(p->pool,pools[i],"member remembers its owning pool");CHECK(p->t.parent,pools[i]+24,"member parent is pool handle");
   CHECK(p->t.name,dead,"pooled process begins named dead");CHECK(p->status,dead,"pooled process begins dead");CHECK(p->pid,0,"pooled process starts with zero PID");
   CHECK(p->main_thread,rt->s7,"pool construction does not create CPU threads");CHECK(p->top_thread,rt->s7,"pool construction does not execute a thread");
   CHECK(p->allocated_length,256,"member keeps requested dynamic length");CHECK(p->heap_base,value+108,"member heap follows original fields");
   CHECK(p->heap_cur,p->heap_base,"new member heap is empty");CHECK(p->heap_top,p->heap_base+256,"member heap extent");
   handle=p->t.brother;
  }
  CHECK(handle,rt->s7,"original constructor produces exactly the requested finite chain");
 }
 {
  one_fn new_tree=(one_fn)(uintptr_t)entry(rt,"dc-new-tree");two_fn move=(two_fn)(uintptr_t)entry(rt,"change-parent");
  uint32_t root=(uint32_t)new_tree(names[0]),used,head;tree *t;process *p;unsigned char before[368];
  CHECK(root!=0,1,"independent active tree is constructed by original code");if(!root)goto restore;
  CHECK(move(members[0],root),members[0],"original reparent detaches a pool member");
  CHECK(word(rt,pools[2]+16),members[1]+24,"pool chain advances when its first member leaves");
  p=dc_goal_resolve(rt,members[0],sizeof(*p));if(!p)goto restore;
  p->status=dc_goal_intern(rt,"ready");p->pid=77;
  memcpy(before,dc_goal_resolve(rt,members[0]-4,sizeof before),sizeof before);
  head=word(rt,pools[2]+16);
  {uint32_t parent_handle=pools[2]+24;memcpy(before+12,&parent_handle,4);memcpy(before+16,&head,4);}
  used=rt->used;give_back(pools[2],members[0]);
  CHECK(rt->error,DC_GOAL_OK,"original return-process completes through typed GOAL dispatch");
  CHECK(rt->used,used,"return-process only reparents and allocates nothing");
  CHECK(memcmp(before,dc_goal_resolve(rt,members[0]-4,sizeof before),sizeof before),0,"only original parent and sibling links change in the complete member reservation");
  t=dc_goal_resolve(rt,root,sizeof(*t));CHECK(t?t->child:0,rt->s7,"return removes member from old tree");
  CHECK(word(rt,pools[2]+16),members[0]+24,"return prepends member to its original pool");
  CHECK(p->status,dc_goal_find(rt,"ready"),"return method itself does not perform deactivation");CHECK(p->pid,77,"return method does not pretend to clear the PID");
  CHECK(word(rt,members[0]+12),members[1]+24,"original sibling chain is retained on return");
  CHECK(word(rt,members[1]+12),members[2]+24,"remaining pool members stay linked");
 }
restore:
 CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"previous allocation method restored");
 CHECK(dc_goal_set_method(rt,parent,0,old_tree),1,"previous tree constructor restored");
 CHECK(dc_goal_set_method(rt,proc_type,0,old_process),1,"previous process constructor restored");
 CHECK(dc_goal_set_method(rt,type,0,old_new),1,"previous pool constructor restored");
 CHECK(dc_goal_set_method(rt,type,15,old_return),1,"previous pool return method restored");
#undef CHECK
 return original.code_bytes+fixture.code_bytes+bridge.code_bytes;
}
