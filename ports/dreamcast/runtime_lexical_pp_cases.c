/* Unchanged original send-event-function lexical pp bracket on SH4 (AI-assisted), ISC. */
#include "goal_bridge.h"
#include "process_spans.h"
#include "pp_scope.h"
#include "runtime_cases.h"
#include "build/original_send_event.h"
#include <string.h>
typedef uint64_t (*zero_fn)(void);
typedef uint64_t (*two_fn)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *r,const char *n){return dc_goal_symbol_value(r,dc_goal_find(r,n));}
static uint32_t word(dc_goal_runtime *r,uint32_t p,unsigned off){uint32_t v;memcpy(&v,dc_goal_resolve(r,p+off,4),4);return v;}
static void put(dc_goal_runtime *r,uint32_t p,unsigned off,uint32_t v){memcpy(dc_goal_resolve(r,p+off,4),&v,4);}
enum { PID=36, EVENT_HOOK=64, BLOCK_BYTES=72 };
/* Hook observations: what the callee sees as pp while the original body runs. */
static dc_goal_runtime *hook_rt;
static uint32_t hook_calls,hook_pp,hook_scheduler,hook_depth,hook_args[4];
static uint32_t nested_target,nested_block,nested_pp_after,nested_result;
static uint64_t event_hook(uint64_t from,uint64_t count,uint64_t message,uint64_t block){
 zero_fn get=(zero_fn)(uintptr_t)entry(hook_rt,"dc-c-current-process");
 ++hook_calls;hook_pp=(uint32_t)get();hook_scheduler=word(hook_rt,entry(hook_rt,"*kernel-context*"),20);
 hook_depth=dc_goal_pp_scope_depth(hook_rt);
 hook_args[0]=(uint32_t)from;hook_args[1]=(uint32_t)count;hook_args[2]=(uint32_t)message;hook_args[3]=(uint32_t)block;
 if(nested_target){
  uint32_t target=nested_target;nested_target=0;
  nested_result=(uint32_t)((two_fn)(uintptr_t)entry(hook_rt,"send-event-function"))(target,nested_block);
  nested_pp_after=(uint32_t)get();
 }
 return 0x5a0000u+hook_calls;
}
uint32_t dc_runtime_lexical_pp_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
 unsigned *id,dc_goal_cache_sync sync){
 dc_goal_process_spans table;dc_goal_process_span rows[8],span;dc_goal_pp_scope_stack scopes;
 dc_goal_module_result original={0},bridge={0},hook_module={0};
 unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},before[240];
 dc_goal_code_export hook_export={"dc-lexical-event-hook",code,12,NULL,0};
 uint32_t type=entry(rt,"process"),name=dc_goal_intern(rt,"dc-lexical-process"),message=dc_goal_intern(rt,"dc-lexical-message");
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_process=dc_goal_get_method(rt,type,0);
 uint32_t a,b,c,raw,block,block2,hook,fn,used=rt->used,symbols=rt->symbol_count,ctx=entry(rt,"*kernel-context*");
 two_fn create=(two_fn)(uintptr_t)entry(rt,"dc-new-process"),send;
 zero_fn get=(zero_fn)(uintptr_t)entry(rt,"dc-c-current-process");
#define CHECK(x,y,label) check((uint64_t)(x),(uint64_t)(y),++*id,label,context)
 dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
 CHECK(dc_goal_load_module(rt,goal_original_send_event_exports,GOAL_ORIGINAL_SEND_EVENT_EXPORT_COUNT,sync,NULL,&original),0,
       "lexical bracket requires installed scope bridge imports");
 CHECK(rt->error,DC_GOAL_BAD_MODULE,"missing scope bridge explicit module error");
 CHECK(rt->used,used,"missing scope bridge rolls back arena");CHECK(rt->symbol_count,symbols,"missing scope bridge rolls back symbols");
 rt->error=DC_GOAL_OK;
 CHECK(dc_goal_install_scope_bridge(rt,sync,&bridge),1,"guarded lexical scope bridges installed explicitly");
 CHECK(bridge.functions,2,"enter and leave bridges exported");
 CHECK(dc_goal_load_module(rt,goal_original_send_event_exports,GOAL_ORIGINAL_SEND_EVENT_EXPORT_COUNT,sync,NULL,&original),1,
       "unchanged original send-event-function links with scope imports");
 fn=(uint32_t)(uintptr_t)event_hook;memcpy(code+8,&fn,4);
 CHECK(dc_goal_load_module(rt,&hook_export,1,sync,NULL,&hook_module),1,"native observing event hook registered");
 if(!original.functions||!bridge.functions||!hook_module.functions)goto restore_methods;
 send=(two_fn)(uintptr_t)entry(rt,"send-event-function");hook=entry(rt,"dc-lexical-event-hook");hook_rt=rt;
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"sized allocation bridge selected");
 CHECK(dc_goal_set_method(rt,type,0,entry(rt,"(method new process)")),1,"unchanged original process constructor selected");
 CHECK(dc_goal_register_process_spans(rt,&table,rows,8),1,"ownership ledger registered for scope selection");
 a=(uint32_t)create(name,128);b=(uint32_t)create(name,128);c=(uint32_t)create(name,128);
 CHECK(a&&b&&c,1,"three owned original processes constructed");if(!a||!b||!c)goto detach;
 put(rt,a,PID,7);put(rt,b,PID,8);put(rt,c,PID,9);put(rt,b,EVENT_HOOK,hook);put(rt,c,EVENT_HOOK,hook);
 block=dc_goal_alloc(rt,BLOCK_BYTES);block2=dc_goal_alloc(rt,BLOCK_BYTES);
 CHECK(block&&block2,1,"event message blocks allocated");if(!block||!block2)goto detach;
 memset(dc_goal_resolve(rt,block,BLOCK_BYTES),0,BLOCK_BYTES);memset(dc_goal_resolve(rt,block2,BLOCK_BYTES),0,BLOCK_BYTES);
 put(rt,block,0,b);put(rt,block,4,a);put(rt,block,8,3);put(rt,block,12,message);
 put(rt,block2,0,c);put(rt,block2,4,b);put(rt,block2,8,1);put(rt,block2,12,message);
 CHECK(dc_goal_register_pp_scope(rt,&scopes),1,"lexical scope metadata registered");
 CHECK(dc_goal_bind_process_context(rt,a,240),1,"scheduler A bound with its owned extent");
 CHECK(get(),a,"entry identity is scheduler A");
 memcpy(before,dc_goal_resolve(rt,b-4,240),240);hook_calls=0;
 CHECK(send(b,block),0x5a0001u,"original body returns the event hook result");
 CHECK(rt->error,DC_GOAL_OK,"successful bracket leaves no error");
 CHECK(hook_calls,1,"event hook called exactly once");
 CHECK(hook_pp,b,"event hook runs with lexical pp set to the target process");
 CHECK(hook_scheduler,a,"lexical selection never replaces scheduler current-process");
 CHECK(hook_depth,1,"exactly one lexical scope is live inside the hook");
 CHECK(hook_args[0],a,"original body passes msg from");CHECK(hook_args[1],3,"original body passes num-params");
 CHECK(hook_args[2],message,"original body passes message symbol");CHECK(hook_args[3],block,"original body passes the block");
 CHECK(get(),a,"pp restored to A after the original bracket");
 CHECK(dc_goal_pp_scope_depth(rt),0,"no scope leaks after normal return");
 CHECK(word(rt,ctx,20),a,"scheduler identity unchanged after return");
 CHECK(memcmp(before,dc_goal_resolve(rt,b-4,240),240),0,"send leaves target process bytes unchanged");
 /* Nested dispatch: B's hook sends to C; each level sees its own pp. */
 hook_calls=0;nested_target=c;nested_block=block2;
 CHECK(send(b,block),0x5a0002u,"outer send returns after nested dispatch");
 CHECK(nested_result,0x5a0002u,"nested original send returns inner hook result");
 CHECK(hook_pp,c,"inner hook sees nested lexical pp C");CHECK(hook_depth,2,"nested dispatch holds two live scopes");
 CHECK(hook_args[0],b,"inner hook receives inner message sender");
 CHECK(nested_pp_after,b,"inner restore returns pp to B inside the outer hook");
 CHECK(get(),a,"outer restore returns pp to A");CHECK(dc_goal_pp_scope_depth(rt),0,"nested dispatch leaks no scope");
 /* Original guards: no hook call and no scope for filtered targets. */
 hook_calls=0;put(rt,b,PID,0);
 CHECK(send(b,block),rt->s7,"zero PID target returns false without dispatch");put(rt,b,PID,8);
 put(rt,b,EVENT_HOOK,rt->s7);CHECK(send(b,block),rt->s7,"false event hook returns false without dispatch");
 put(rt,b,EVENT_HOOK,hook);
 CHECK(send(dc_goal_string(rt,"not a process"),block),rt->s7,"non-process target rejected by original valid? check");
 CHECK(hook_calls,0,"filtered sends never call a hook");CHECK(dc_goal_pp_scope_depth(rt),0,"filtered sends enter no scope");
 CHECK(get(),a,"filtered sends keep A");
 /* An owned-looking but unrecorded process cannot be selected. */
 raw=dc_goal_alloc(rt,272);CHECK(raw!=0,1,"raw unowned process storage allocated");if(!raw)goto unbind;
 memset(dc_goal_resolve(rt,raw,272),0,272);memcpy(dc_goal_resolve(rt,raw+16,240),dc_goal_resolve(rt,b-4,240),240);
 CHECK(dc_goal_process_span_lookup(rt,raw+20,&span),0,"copied process has no ownership record");rt->error=DC_GOAL_OK;
 hook_calls=0;
 CHECK(send(raw+20,block),0,"unowned target refused before its hook runs");
 CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"refused selection reports explicit error");
 CHECK(hook_calls,0,"refused selection never runs the hook");CHECK(dc_goal_pp_scope_depth(rt),0,"refused selection enters no scope");
 rt->error=DC_GOAL_OK;CHECK(get(),a,"A remains after refused selection");
 /* A missing callee inside the bracket restores A on the fault exit. */
 put(rt,b,EVENT_HOOK,0);hook_calls=0;
 CHECK(send(b,block),0,"zero event hook takes the guarded call-fault exit");
 CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"call fault inside bracket reports bad function");
 CHECK(dc_goal_pp_scope_depth(rt),0,"call-fault exit restores the entered scope");
 rt->error=DC_GOAL_OK;CHECK(get(),a,"A recovered after call-fault restore");
 put(rt,b,EVENT_HOOK,hook);
 /* Without an execution identity the original body never starts. */
 dc_goal_clear_process_context(rt);hook_calls=0;
 CHECK(send(b,block),0,"missing entry identity refuses before the body");
 CHECK(hook_calls,0,"missing identity never dispatches");rt->error=DC_GOAL_OK;
unbind:
 dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
 CHECK(dc_goal_pp_scope_detach(&scopes,rt),1,"quiescent scope metadata detaches");
detach:
 rt->error=DC_GOAL_OK;CHECK(dc_goal_process_spans_detach(rt),1,"ownership ledger detaches");
restore_methods:
 CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"object allocation method restored");
 CHECK(dc_goal_set_method(rt,type,0,old_process),1,"process constructor method restored");
 hook_rt=NULL;rt->error=DC_GOAL_OK;
#undef CHECK
 return original.code_bytes+bridge.code_bytes+hook_module.code_bytes;
}
