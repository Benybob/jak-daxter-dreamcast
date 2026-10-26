/* Actual original constructors and owned pp selection on SH4 (AI-assisted), ISC. */
#include "goal_bridge.h"
#include "process_spans.h"
#include "pp_scope.h"
#include "fiber_test_support.h"
#include "runtime_cases.h"
#include <string.h>
typedef uint64_t (*zero_fn)(void);
typedef uint64_t (*two_fn)(uint64_t,uint64_t);
typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);
typedef uint64_t (*four_fn)(uint64_t,uint64_t,uint64_t,uint64_t);
typedef void (*return_fn)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *r,const char *n){return dc_goal_symbol_value(r,dc_goal_find(r,n));}
static uint32_t word(dc_goal_runtime *r,uint32_t p,unsigned off){uint32_t v;memcpy(&v,dc_goal_resolve(r,p+off,4),4);return v;}
static void put(dc_goal_runtime *r,uint32_t p,unsigned off,uint32_t v){memcpy(dc_goal_resolve(r,p+off,4),&v,4);}
/* Complete byte oracle; a failure reports [offset+1:16][actual:8][expected:8]. */
static uint32_t byte_difference(const unsigned char *actual,const unsigned char *expected,unsigned bytes){
 unsigned i;for(i=0;i<bytes;++i)if(actual[i]!=expected[i])return ((i+1)<<16)|((uint32_t)actual[i]<<8)|expected[i];return 0;
}
static dc_goal_runtime *body_rt;
static uint32_t body_process,body_previous;
static unsigned body_lookup,body_enter,body_identity,body_retire,body_leave,body_clear;
static uint64_t native_body(void){
 dc_goal_process_span span;
 zero_fn get=(zero_fn)(uintptr_t)entry(body_rt,"dc-c-current-process");
 body_lookup=dc_goal_process_span_lookup(body_rt,body_process,&span)&&span.bytes==240;
 body_enter=dc_goal_enter_known_scope(body_rt,body_process);
 body_identity=get()==body_process;
 body_retire=!dc_goal_process_span_retire(body_rt,&span)&&body_rt->error==DC_GOAL_BAD_ARGUMENT;
 body_rt->error=DC_GOAL_OK;body_leave=dc_goal_leave_known_scope(body_rt,body_previous);
 dc_goal_clear_core_bridges(body_rt);
 body_clear=dc_goal_core_bridges_active(body_rt)&&body_rt->error==DC_GOAL_BAD_ARGUMENT;
 body_rt->error=DC_GOAL_OK;return 77;
}
uint32_t dc_runtime_process_spans_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
 unsigned *id,dc_goal_cache_sync sync){
 dc_goal_process_spans table;dc_goal_process_span rows[8],a_span,b_span,old_span,out_span;
 dc_goal_pp_scope_stack scopes;dc_goal_fiber fiber;dc_goal_module_result module={0};
 unsigned i,intact;uint32_t a,b,c,d,raw,type=entry(rt,"process"),name=dc_goal_intern(rt,"dc-owned-process");
 uint32_t ctx=entry(rt,"*kernel-context*"),used,prior,count,frame_type=entry(rt,"stack-frame");
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_process=dc_goal_get_method(rt,type,0);
 uint32_t tree_type=entry(rt,"process-tree"),pool_type=entry(rt,"dead-pool"),pool,member,next;
 uint32_t old_tree=dc_goal_get_method(rt,tree_type,0),old_pool=dc_goal_get_method(rt,pool_type,0),old_return=dc_goal_get_method(rt,pool_type,15);
 uint32_t owner,stack,thread,fn,value;uint64_t result;
 unsigned char expected[272],code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 const unsigned false_offsets[]={8,12,16,28,40,44,48,52,56,60,64,72,88};
 dc_goal_code_export exported={"dc-owned-span-body",code,12,NULL,0};
 two_fn create=(two_fn)(uintptr_t)entry(rt,"dc-new-process");
 three_fn alloc=(three_fn)(uintptr_t)entry(rt,"dc-c-object-new");
 four_fn raw_ctor=(four_fn)(uintptr_t)entry(rt,"(method new process)");
 zero_fn get=(zero_fn)(uintptr_t)entry(rt,"dc-c-current-process"),pop=(zero_fn)(uintptr_t)entry(rt,"remove-exit");
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 dc_goal_clear_process_context(rt);rt->error=DC_GOAL_OK;
 CHECK(sizeof(dc_goal_process_span),16,"native ownership row metadata cost");
 CHECK(sizeof(dc_goal_process_spans),32,"native ownership descriptor metadata cost");
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"ownership captures actual original object allocator");
 CHECK(dc_goal_set_method(rt,type,0,entry(rt,"(method new process)")),1,"ownership uses unchanged original process constructor");
 CHECK(dc_goal_register_process_spans(rt,&table,rows,8),1,"register caller-owned process span storage before construction");
 if(dc_goal_process_spans_registered(rt)!=1)goto restore;
 a=(uint32_t)create(name,128);CHECK(a!=0,1,"symbol constructor captured by trusted sized allocator");if(!a)goto detach;
 CHECK(dc_goal_process_span_lookup(rt,a,&a_span),1,"original symbol constructor has automatic ownership record");
 CHECK(a_span.bytes,240,"captured requested bytes include original base prefix");
 CHECK(a_span.generation!=0,1,"physical ownership has nonzero lifetime generation");
 CHECK(word(rt,ctx,20),rt->s7,"automatic ownership capture does not publish scheduler identity");
 CHECK(get(),0,"allocation capture alone cannot supply implicit pp");rt->error=DC_GOAL_OK;
 raw=dc_goal_alloc(rt,272);CHECK(raw!=0,1,"trusted owner allocates fenced raw constructor storage");if(!raw)goto detach;
 b=raw+20;memset(expected,0xa5,sizeof expected);memcpy(expected+16,&type,4);
 memcpy(dc_goal_resolve(rt,raw,272),expected,272);used=rt->used;count=table.count;
 CHECK(raw_ctor(raw+16,type,name,128),b,"unchanged original raw-address constructor executes");
 CHECK(rt->used,used,"raw constructor calls no allocator");CHECK(table.count,count,"raw constructor does not invent an allocator record");
 value=name;memcpy(expected+20,&value,4);value=dc_goal_find(rt,"dead");memcpy(expected+52,&value,4);
 value=0;memcpy(expected+56,&value,4);value=128;memcpy(expected+88,&value,4);
 for(i=0;i<sizeof false_offsets/sizeof false_offsets[0];++i)memcpy(expected+20+false_offsets[i],&rt->s7,4);
 value=b+108;memcpy(expected+96,&value,4);memcpy(expected+104,&value,4);
 value=b+236;memcpy(expected+100,&value,4);value=b;memcpy(expected+44,&value,4);
 value=b+24;memcpy(expected+40,&value,4);
 CHECK(byte_difference(dc_goal_resolve(rt,raw,272),expected,272),0,"independent complete raw constructor bytes, untouched fields and fences");
 put(rt,a,36,41);put(rt,b,36,42);
 CHECK(dc_goal_register_pp_scope(rt,&scopes),1,"separate lexical scope metadata registered");
 CHECK(dc_goal_bind_process_context(rt,a,256),0,"forged explicit extent rejected by allocator record");rt->error=DC_GOAL_OK;
 CHECK(dc_goal_bind_process_context(rt,a,240),1,"scheduler binds exact automatically captured extent");
 CHECK(dc_goal_enter_known_scope(rt,b),0,"valid raw process still requires trusted ownership note");
 CHECK(dc_goal_pp_scope_depth(rt),0,"missing ownership cannot enter a lexical scope");rt->error=DC_GOAL_OK;
 CHECK(dc_goal_note_process_span(rt,b,240,&b_span),1,"trusted raw buffer owner records actual bytes while A runs");
 CHECK(get(),a,"recording B leaves execution identity A");
 CHECK(dc_goal_enter_known_scope(rt,b),1,"explicit process selection looks up owned raw extent");
 CHECK(get(),b,"unchanged compiled implicit getter selects owned B");
 CHECK(word(rt,ctx,20),a,"owned lexical B does not replace scheduler A");
 CHECK(pop(),rt->s7,"original remove-exit reads owned lexical B with empty frame chain");
 c=(uint32_t)create(name,128);CHECK(c!=0,1,"fresh symbol process captured while another lexical process runs");
 CHECK(dc_goal_process_span_lookup(rt,c,&out_span),1,"fresh allocation has independent trusted span");
 CHECK(get(),b,"fresh process allocation never changes lexical pp");
 prior=word(rt,b,84);used=rt->used;
 CHECK(alloc(rt->s7+DC_FIX_SYM_PROCESS_TYPE,type,112),0,"nested process suballocation refuses without parent ownership protocol");
 CHECK(word(rt,b,84),prior,"refused nested process allocation does not consume process heap");
 CHECK(rt->used,used,"refused nested process allocation does not consume global arena");rt->error=DC_GOAL_OK;
 CHECK(dc_goal_leave_known_scope(rt,c),0,"restore operation rejects a process other than saved A");
 CHECK(dc_goal_pp_scope_depth(rt),1,"wrong restoration retains current scope");rt->error=DC_GOAL_OK;
 CHECK(dc_goal_leave_known_scope(rt,a),1,"explicit restore uses saved owned A");CHECK(get(),a,"known-scope restore recovers A");
 old_span=b_span;CHECK(dc_goal_process_span_retire(rt,&b_span),0,"physical ownership cannot retire while any execution binding lives");
 rt->error=DC_GOAL_OK;dc_goal_clear_process_context(rt);
 CHECK(dc_goal_process_span_retire(rt,&b_span),1,"quiescent explicit retirement forgets authority only");
 CHECK(dc_goal_note_process_span(rt,b,240,&b_span),1,"same physical span can be explicitly owned in a later lifetime");
 CHECK(b_span.generation!=old_span.generation,1,"reclaimed physical ownership gets fresh generation");
 CHECK(dc_goal_process_span_check(rt,&old_span),0,"stale generation fails after address reuse");rt->error=DC_GOAL_OK;
 /* Retagging is only an ownership oracle here; derived heap execution is pending. */
 put(rt,b-4,0,entry(rt,"process-tree"));CHECK(dc_goal_process_span_lookup(rt,b,&out_span),1,"type retag leaves physical ownership intact");
 CHECK(out_span.bytes,240,"type metadata cannot resize ownership");
 CHECK(dc_goal_bind_process_context(rt,b,240),0,"ownership does not bypass heap/type validation");
 put(rt,b-4,0,type);rt->error=DC_GOAL_OK;
 CHECK(dc_goal_set_method(rt,tree_type,0,entry(rt,"(method new process-tree)")),1,"original pool parent constructor selected for new ownership integration");
 CHECK(dc_goal_set_method(rt,pool_type,0,entry(rt,"(method new dead-pool)")),1,"original fixed pool constructor selected for ownership capture");
 CHECK(dc_goal_set_method(rt,pool_type,15,entry(rt,"(method return-process dead-pool)")),1,"original pool return selected for ownership lifetime test");
 count=table.count;pool=(uint32_t)((three_fn)(uintptr_t)entry(rt,"dc-dead-pool-create"))(2,128,name);
 CHECK(pool!=0,1,"unchanged original fixed pool builds two processes through captured allocator");
 CHECK(table.count,count+2,"both original fixed-pool members automatically captured without tracking organizational pool");
 if(!pool)goto scope_detach;
 member=word(rt,word(rt,pool,16),0);next=word(rt,word(rt,member,12),0);
 CHECK(dc_goal_process_span_lookup(rt,member,&old_span),1,"first original fixed-pool member has ownership");
 CHECK(dc_goal_process_span_lookup(rt,next,&out_span),1,"second original fixed-pool member has ownership");
 CHECK(old_span.bytes,240,"pool member extent comes from exact process allocator bytes");
 ((return_fn)(uintptr_t)entry(rt,"dc-dead-pool-return"))(pool,member);
 CHECK(dc_goal_process_span_lookup(rt,member,&out_span),1,"original return-process preserves existing physical ownership");
 CHECK(out_span.generation,old_span.generation,"pool return does not retire or recreate a memory generation");
 CHECK(table.count,count+2,"pool return does not lose ownership records");
 owner=(uint32_t)create(name,1024);CHECK(owner!=0,1,"original process for real owned-lookup fiber");if(!owner)goto scope_detach;
 stack=dc_goal_alloc(rt,16384);CHECK(stack!=0,1,"owned native working stack for real lookup output");if(!stack)goto scope_detach;
 thread=dc_goal_construct_main_thread(rt,owner,1136,stack,16384,name,512);
 CHECK(thread!=0,1,"original thread constructor supplies owned fiber layout");if(!thread)goto scope_detach;
 fn=(uint32_t)(uintptr_t)native_body;memcpy(code+8,&fn,4);
 CHECK(dc_goal_load_module(rt,&exported,1,sync,NULL,&module),1,"registered native ownership callback");if(!module.functions)goto scope_detach;
 CHECK(dc_goal_bind_process_context(rt,owner,1136),1,"explicit owned scheduler for real native callback");
 body_rt=rt;body_process=b;body_previous=owner;memset(&fiber,0,sizeof fiber);
 CHECK(dc_goal_fiber_init(&fiber,rt,owner,1136,thread,stack,16384,entry(rt,"dc-owned-span-body")),1,"real native ownership fiber initializes");
 result=1234;CHECK(dc_runtime_fiber_checked_step(&fiber,0,&result,&intact),DC_FIBER_COMPLETED,"owned lookup/scope callback returns on original native stack path");
 CHECK(intact,1,"owned lookup preserves integer/FP registers, canaries and root stack");CHECK(result,77,"native ownership callback returns expected scalar");
 CHECK(body_lookup,1,"lookup output on actual current working stack accepted");CHECK(body_enter,1,"known-byte lookup selects B from actual native fiber");
 CHECK(body_identity,1,"compiled getter authenticates owned B in real fiber");CHECK(body_retire,1,"fiber cannot retire borrowed physical ownership");
 CHECK(body_leave,1,"real fiber restores saved scheduler domain");CHECK(body_clear,1,"core teardown refuses current fiber even with no lexical scope");
 CHECK(dc_goal_fiber_clear(&fiber),1,"completed ownership test fiber clears");body_rt=NULL;dc_goal_clear_process_context(rt);
 while(table.count<8){d=(uint32_t)alloc(rt->s7+DC_FIX_SYM_GLOBAL_HEAP,type,112);CHECK(d!=0,1,"physical capture can fill caller-sized ownership capacity");if(!d)break;}
 used=rt->used;count=table.count;
 CHECK(alloc(rt->s7+DC_FIX_SYM_GLOBAL_HEAP,type,112),0,"full ownership table refuses before original allocator commits memory");
 CHECK(rt->used,used,"full ownership capacity is allocation-atomic");CHECK(table.count,count,"full ownership refusal does not evict a process");
 CHECK(rt->error,DC_GOAL_OUT_OF_MEMORY,"full process ownership capacity reports OOM");rt->error=DC_GOAL_OK;
 CHECK(alloc(rt->s7+DC_FIX_SYM_GLOBAL_HEAP,frame_type,12)!=0,1,"full process table still permits unrelated basic allocations");
 CHECK(dc_goal_process_span_note(rt,b,240,&out_span),1,"full table permits idempotent known-span note");
 CHECK(out_span.generation,b_span.generation,"idempotent full-table note retains lifetime");
scope_detach:
 rt->error=DC_GOAL_OK;dc_goal_clear_process_context(rt);CHECK(dc_goal_pp_scope_detach(&scopes,rt),1,"quiescent lexical metadata detaches after ownership tests");
detach:
 CHECK(dc_goal_process_spans_detach(rt),1,"quiescent ownership metadata detaches without dangling caller storage");
 CHECK(dc_goal_process_spans_registered(rt),0,"detached ledger leaves no registered owner");
restore:
 CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"previous original object allocation method restored");
 CHECK(dc_goal_set_method(rt,type,0,old_process),1,"previous original process constructor method restored");
 CHECK(dc_goal_set_method(rt,tree_type,0,old_tree),1,"previous original tree constructor method restored");
 CHECK(dc_goal_set_method(rt,pool_type,0,old_pool),1,"previous original pool constructor method restored");
 CHECK(dc_goal_set_method(rt,pool_type,15,old_return),1,"previous original pool return method restored");
 rt->error=DC_GOAL_OK;
#undef CHECK
 return module.code_bytes;
}
