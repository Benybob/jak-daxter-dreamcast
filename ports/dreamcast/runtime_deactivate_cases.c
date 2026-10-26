/* Unchanged original process deactivation on SH4 (AI-assisted), ISC. */
#include "goal_bridge.h"
#include "process_spans.h"
#include "pp_scope.h"
#include "runtime_cases.h"
#include "build/original_deactivate.h"
#include <string.h>
typedef uint64_t (*zero_fn)(void);
typedef uint64_t (*one_fn)(uint64_t);
typedef uint64_t (*two_fn)(uint64_t,uint64_t);
typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *r,const char *n){return dc_goal_symbol_value(r,dc_goal_find(r,n));}
static uint32_t word(dc_goal_runtime *r,uint32_t p,unsigned off){uint32_t v;memcpy(&v,dc_goal_resolve(r,p+off,4),4);return v;}
static void put(dc_goal_runtime *r,uint32_t p,unsigned off,uint32_t v){memcpy(dc_goal_resolve(r,p+off,4),&v,4);}
/* Original process field offsets (base 112-byte schema). */
enum {PARENT=8,BROTHER=12,CHILD=16,SELF=24,POOL=28,STATUS=32,PID=36,ENTITY=48,STATE=52,NEXT_STATE=72,FRAMES=88,
      MEMBER_BYTES=368,DEACTIVATE_SLOT=10,RETURN_SLOT=15};
static dc_goal_runtime *exit_rt;
static uint32_t exit_order[4],exit_pp[4],exit_count;
static uint64_t record_exit(uint32_t tag){
 zero_fn get=(zero_fn)(uintptr_t)entry(exit_rt,"dc-c-current-process");
 if(exit_count<4){exit_order[exit_count]=tag;exit_pp[exit_count]=(uint32_t)get();}
 ++exit_count;return 0;
}
static uint64_t first_exit(void){return record_exit(1);}
static uint64_t second_exit(void){return record_exit(2);}
static int in_pool(dc_goal_runtime *rt,uint32_t pool,uint32_t process){
 uint32_t handle=word(rt,pool,CHILD);unsigned guard=0;
 while(handle!=rt->s7 && guard++<16){if(word(rt,handle,0)==process)return 1;handle=word(rt,word(rt,handle,0),BROTHER);}
 return 0;
}
uint32_t dc_runtime_deactivate_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
 unsigned *id,dc_goal_cache_sync sync){
 dc_goal_process_spans table;dc_goal_process_span rows[8];dc_goal_pp_scope_stack scopes;
 dc_goal_module_result original={0},scope_bridge={0},transfer={0},exits={0};
 unsigned char code[2][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
 dc_goal_code_export exit_exports[2]={{"dc-deactivate-exit-1",code[0],12,NULL,0},{"dc-deactivate-exit-2",code[1],12,NULL,0}};
 uint32_t type=entry(rt,"process"),tree_type=entry(rt,"process-tree"),pool_type=entry(rt,"dead-pool");
 uint32_t protect=entry(rt,"protect-frame"),name=dc_goal_intern(rt,"dc-deactivate-process");
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_process=dc_goal_get_method(rt,type,0);
 uint32_t old_tree=dc_goal_get_method(rt,tree_type,0),old_pool=dc_goal_get_method(rt,pool_type,0);
 uint32_t old_return=dc_goal_get_method(rt,pool_type,RETURN_SLOT),old_deactivate=dc_goal_get_method(rt,type,DEACTIVATE_SLOT);
 uint32_t a,pool,m0,m1,m2,f1,f2,fn,ready=dc_goal_intern(rt,"ready"),dead=dc_goal_find(rt,"dead");
 uint32_t init=dc_goal_intern(rt,"initialize"),used=rt->used,symbols=rt->symbol_count;
 two_fn create=(two_fn)(uintptr_t)entry(rt,"dc-new-process"),move=(two_fn)(uintptr_t)entry(rt,"change-parent");
 three_fn pool_create=(three_fn)(uintptr_t)entry(rt,"dc-dead-pool-create");one_fn deactivate;
 zero_fn get=(zero_fn)(uintptr_t)entry(rt,"dc-c-current-process");
#define CHECK(x,y,label) check((uint64_t)(x),(uint64_t)(y),++*id,label,context)
 dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
 CHECK(dc_goal_load_module(rt,goal_original_deactivate_exports,GOAL_ORIGINAL_DEACTIVATE_EXPORT_COUNT,sync,NULL,&original),0,
       "deactivation requires the explicit return-from-thread transfer import");
 CHECK(rt->error,DC_GOAL_BAD_MODULE,"missing transfer import explicit module error");
 CHECK(rt->used,used,"missing transfer import rolls back arena");CHECK(rt->symbol_count,symbols,"missing transfer import rolls back symbols");
 rt->error=DC_GOAL_OK;
 if(!entry(rt,"dc-c-enter-process-scope"))CHECK(dc_goal_install_scope_bridge(rt,sync,&scope_bridge),1,"scope bridges installed");
 CHECK(dc_goal_install_transfer_bridge(rt,sync,&transfer),1,"explicit return-from-thread transfer installed");
 CHECK(dc_goal_load_module(rt,goal_original_deactivate_exports,GOAL_ORIGINAL_DEACTIVATE_EXPORT_COUNT,sync,NULL,&original),1,
       "unchanged original deactivate links with scope and transfer imports");
 fn=(uint32_t)(uintptr_t)first_exit;memcpy(code[0]+8,&fn,4);fn=(uint32_t)(uintptr_t)second_exit;memcpy(code[1]+8,&fn,4);
 CHECK(dc_goal_load_module(rt,exit_exports,2,sync,NULL,&exits),1,"native observing protect-frame exits registered");
 /* Original protect-frame: stack-frame plus a 4-byte exit, inherited 9 methods.
  * Deactivation dispatches on its type identity, so it must be a real type. */
 if(!protect && entry(rt,"stack-frame"))
  protect=dc_goal_new_type(rt,dc_goal_intern(rt,"protect-frame"),entry(rt,"stack-frame"),((uint64_t)9<<32)|12u);
 CHECK(pool_type!=0,1,"original dead-pool type available from earlier pool cases");
 CHECK(protect!=0,1,"original protect-frame type available");
 CHECK(entry(rt,"protect-frame"),protect,"protect-frame symbol names the frame type deactivate compares against");
 CHECK(entry(rt,"dc-dead-pool-create")!=0 && entry(rt,"change-parent")!=0 && entry(rt,"dc-new-process")!=0,1,
       "original pool, reparent and process helpers linked");
 if(!original.functions||!transfer.functions||!exits.functions||!pool_type||!protect)goto restore;
 deactivate=(one_fn)(uintptr_t)entry(rt,"(method deactivate process)");exit_rt=rt;
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"sized allocation bridge selected");
 CHECK(dc_goal_set_method(rt,type,0,entry(rt,"(method new process)")),1,"original process constructor selected");
 CHECK(dc_goal_set_method(rt,tree_type,0,entry(rt,"(method new process-tree)")),1,"original tree constructor selected");
 CHECK(dc_goal_set_method(rt,pool_type,0,entry(rt,"(method new dead-pool)")),1,"original pool constructor selected");
 CHECK(dc_goal_set_method(rt,pool_type,RETURN_SLOT,entry(rt,"(method return-process dead-pool)")),1,"original return-process selected");
 CHECK(dc_goal_set_method(rt,type,DEACTIVATE_SLOT,entry(rt,"(method deactivate process)")),1,"original deactivate installed at process slot ten");
 CHECK(dc_goal_register_process_spans(rt,&table,rows,8),1,"ownership ledger registered before construction");
 a=(uint32_t)create(name,128);pool=(uint32_t)pool_create(3,256,name);
 CHECK(a&&pool,1,"scheduler process and three-member original pool constructed");if(!a||!pool)goto detach;
 m0=word(rt,word(rt,pool,CHILD),0);m1=word(rt,word(rt,m0,BROTHER),0);m2=word(rt,word(rt,m1,BROTHER),0);
 CHECK(move(m1,m0),m1,"original change-parent makes m1 a child of m0");
 CHECK(in_pool(rt,pool,m1),0,"child left the pool chain");
 put(rt,m0,STATUS,ready);put(rt,m0,PID,31);put(rt,m1,STATUS,ready);put(rt,m1,PID,32);
 f1=dc_goal_new_basic(rt,protect,12);f2=dc_goal_new_basic(rt,protect,12);
 CHECK(f1&&f2,1,"two original-schema protect frames allocated");if(!f1||!f2)goto detach;
 put(rt,f1,0,name);put(rt,f1,4,rt->s7);put(rt,f1,8,entry(rt,"dc-deactivate-exit-1"));
 put(rt,f2,0,name);put(rt,f2,4,f1);put(rt,f2,8,entry(rt,"dc-deactivate-exit-2"));put(rt,m0,FRAMES,f2);
 CHECK(dc_goal_register_pp_scope(rt,&scopes),1,"lexical scope metadata registered");
 CHECK(dc_goal_bind_process_context(rt,a,240),1,"scheduler A bound");
 /* Deactivating another process: frames, children, pool return. */
 exit_count=0;
 deactivate(m0);
 CHECK(rt->error,DC_GOAL_OK,"original deactivation of a non-running process succeeds");
 CHECK(exit_count,2,"both protect-frame exits run");
 CHECK(exit_order[0],2,"top frame exit runs first");CHECK(exit_order[1],1,"older frame exit runs second");
 CHECK(exit_pp[0],m0,"first exit runs with lexical pp = deactivated process");
 CHECK(exit_pp[1],m0,"second exit runs with lexical pp = deactivated process");
 CHECK(word(rt,m0,STATUS),dead,"process status becomes dead");CHECK(word(rt,m0,PID),0,"process PID cleared");
 CHECK(word(rt,m0,STATE),rt->s7,"state cleared");CHECK(word(rt,m0,NEXT_STATE),rt->s7,"next-state cleared");
 CHECK(word(rt,m0,ENTITY),rt->s7,"entity cleared");
 CHECK(word(rt,m1,STATUS),dead,"child deactivated recursively through method slot ten");
 CHECK(word(rt,m1,PID),0,"child PID cleared");
 CHECK(in_pool(rt,pool,m0),1,"process returned to its original pool");
 CHECK(in_pool(rt,pool,m1),1,"child returned to its original pool");
 CHECK(word(rt,m0,CHILD),rt->s7,"deactivated process has no children left");
 CHECK(get(),a,"caller pp restored after deactivation");
 CHECK(dc_goal_pp_scope_depth(rt),0,"deactivation leaks no lexical scope");
 CHECK(word(rt,entry(rt,"*kernel-context*"),20),a,"scheduler current-process unchanged");
 /* Already dead: original early exit, no callbacks. */
 exit_count=0;deactivate(m0);
 CHECK(exit_count,0,"dead process deactivation runs no exits");CHECK(rt->error,DC_GOAL_OK,"dead process deactivation is a no-op");
 /* Initializing process: original throws back to its initializer; throw is not native yet. */
 put(rt,m1,STATUS,init);put(rt,m1,PID,33);put(rt,m1,FRAMES,rt->s7);
 deactivate(m1);
 CHECK(word(rt,m1,STATUS),dead,"initializing process marked dead before its throw");
 CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"unbound original throw faults explicitly instead of continuing");
 CHECK(dc_goal_pp_scope_depth(rt),0,"initialize path leaks no scope");rt->error=DC_GOAL_OK;
 CHECK(get(),a,"A remains after initialize-path fault");
 /* Deactivating the running process: cleanup runs, then the transfer refuses. */
 dc_goal_clear_process_context(rt);
 CHECK(dc_goal_bind_process_context(rt,m2,MEMBER_BYTES),1,"pool member bound as scheduler current-process");
 put(rt,m2,STATUS,ready);put(rt,m2,PID,34);
 CHECK(deactivate(m2),0,"self-deactivation exits through the refused transfer");
 CHECK(rt->error,DC_GOAL_UNSUPPORTED_TRANSFER,"return-from-thread reports unsupported transfer explicitly");
 CHECK(word(rt,m2,STATUS),dead,"running process marked dead before the transfer");
 CHECK(word(rt,m2,PID),0,"running process PID cleared before the transfer");
 CHECK(in_pool(rt,pool,m2),1,"running process returned to its pool before the transfer");
 CHECK(dc_goal_pp_scope_depth(rt),0,"self-deactivation leaks no scope");
 rt->error=DC_GOAL_OK;
 /* No execution identity: the original body never starts. */
 dc_goal_clear_process_context(rt);put(rt,m0,STATUS,ready);put(rt,m0,PID,35);exit_count=0;
 deactivate(m0);
 CHECK(word(rt,m0,STATUS),ready,"missing identity refuses before any original write");
 CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"missing identity explicit error");rt->error=DC_GOAL_OK;
 dc_goal_clear_process_context(rt);
 CHECK(dc_goal_pp_scope_detach(&scopes,rt),1,"quiescent scope metadata detaches");
detach:
 rt->error=DC_GOAL_OK;dc_goal_clear_process_context(rt);
 CHECK(dc_goal_process_spans_detach(rt),1,"ownership ledger detaches");
restore:
 CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"object allocation method restored");
 CHECK(dc_goal_set_method(rt,type,0,old_process),1,"process constructor restored");
 CHECK(dc_goal_set_method(rt,tree_type,0,old_tree),1,"tree constructor restored");
 if(pool_type){
  CHECK(dc_goal_set_method(rt,pool_type,0,old_pool),1,"pool constructor restored");
  CHECK(dc_goal_set_method(rt,pool_type,RETURN_SLOT,old_return),1,"pool return restored");
 }
 CHECK(dc_goal_set_method(rt,type,DEACTIVATE_SLOT,old_deactivate),1,"process deactivate slot restored");
 exit_rt=NULL;rt->error=DC_GOAL_OK;
#undef CHECK
 return original.code_bytes+scope_bridge.code_bytes+transfer.code_bytes+exits.code_bytes;
}
