/* Original frame code under foreign lexical pp and real native fibers (AI-assisted), ISC. */
#include "goal_bridge.h"
#include "pp_scope.h"
#include "fiber_test_support.h"
#include "runtime_cases.h"
#include <string.h>
#include <arch/irq.h>
typedef uint64_t (*zero_fn)(void);
typedef uint64_t (*two_fn)(uint64_t,uint64_t);
typedef uint64_t (*three_fn)(uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *r,const char *n){return dc_goal_symbol_value(r,dc_goal_find(r,n));}
static uint32_t word(dc_goal_runtime *r,uint32_t p,unsigned off){uint32_t v;memcpy(&v,dc_goal_resolve(r,p+off,4),4);return v;}
static void put(dc_goal_runtime *r,uint32_t p,unsigned off,uint32_t v){memcpy(dc_goal_resolve(r,p+off,4),&v,4);}
static dc_goal_runtime *body_rt;
static dc_goal_pp_scope_stack *body_scopes;
static uint32_t body_process,body_frame,body_type,body_context,body_owner;
static uint32_t body_nested_process;
static unsigned body_mode,body_entered,body_lexical,body_scheduler,body_left,body_yield;
static unsigned body_nested_entered,body_nested_getter;
static enum dc_goal_error body_fault;
static uint64_t body_alloc,body_constructed,body_popped;
static uint64_t native_body(void){
 zero_fn get=(zero_fn)(uintptr_t)entry(body_rt,"dc-c-current-process"),pop=(zero_fn)(uintptr_t)entry(body_rt,"remove-exit");
 three_fn make=(three_fn)(uintptr_t)entry(body_rt,"(method new protect-frame)");
 two_fn alloc=(two_fn)(uintptr_t)entry(body_rt,"dc-c-basic-new");
 body_entered=dc_goal_enter_lexical_scope(body_rt,body_process,240);
 body_lexical=get()==body_process;body_scheduler=word(body_rt,body_context,20)==body_owner;
 if(body_mode==0){
  body_constructed=make(body_frame+16,body_type,(uint32_t)(uintptr_t)pop);
  body_popped=pop();body_alloc=alloc(body_rt->s7+DC_FIX_SYM_PROCESS_TYPE,entry(body_rt,"stack-frame"));
  body_left=dc_goal_leave_lexical_scope(body_rt,body_process,240);
 } else if(body_mode==2 || body_mode==5){
  dc_goal_fiber *f=dc_goal_fiber_current();
  uint64_t rejected;
  if(body_mode==5)body_rt->error=DC_GOAL_DIVIDE_BY_ZERO;
  rejected=dc_goal_fiber_yield(body_rt,99);body_fault=body_rt->error;
  body_yield=rejected==0 && f && f->phase==DC_FIBER_RUNNING && f->root_live;
  body_left=dc_goal_leave_lexical_scope(body_rt,body_process,240);
 } else if(body_mode==3)body_rt->error=DC_GOAL_DIVIDE_BY_ZERO;
 else if(body_mode==4)body_scopes->depth=DC_GOAL_PP_SCOPE_MAX_DEPTH+1;
 else if(body_mode==6||body_mode==7){
  body_nested_entered=dc_goal_enter_lexical_scope(body_rt,body_nested_process,240);
  body_nested_getter=get()==body_nested_process;
  if(body_mode==7)body_rt->error=DC_GOAL_DIVIDE_BY_ZERO;
 }
 return 77;
}
uint32_t dc_runtime_pp_scope_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
 unsigned *id,dc_goal_cache_sync sync){
 uint32_t owners[3],proc_type=entry(rt,"process"),protect=entry(rt,"protect-frame"),frame_type=entry(rt,"stack-frame");
 uint32_t ctx=entry(rt,"*kernel-context*"),old_object=dc_goal_get_method(rt,rt->object_type,0),old_process=dc_goal_get_method(rt,proc_type,0);
 uint32_t frame,cur,used,i,k,words[4],fiber_owner,stack,thread,fn,prior;unsigned intact;
 unsigned char expected[3][240],raw[48],code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 dc_goal_pp_scope_stack scopes;dc_goal_module_result module={0};dc_goal_fiber fiber,saved_fiber;
 dc_goal_code_export exported={"dc-pp-native-body",code,12,NULL,0};uint64_t out;
 two_fn create=(two_fn)(uintptr_t)entry(rt,"dc-new-process"),alloc=(two_fn)(uintptr_t)entry(rt,"dc-c-basic-new");
 three_fn make=(three_fn)(uintptr_t)entry(rt,"(method new protect-frame)"),alloc_object=(three_fn)(uintptr_t)entry(rt,"dc-c-object-new");
 zero_fn get=(zero_fn)(uintptr_t)entry(rt,"dc-c-current-process"),pop=(zero_fn)(uintptr_t)entry(rt,"remove-exit");
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 CHECK(make!=NULL&&get!=NULL&&pop!=NULL&&create!=NULL,1,"original context/frame functions are present");
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"original process allocation bridge selected");
 CHECK(dc_goal_set_method(rt,proc_type,0,entry(rt,"(method new process)")),1,"original base-process constructor selected");
 dc_goal_clear_process_context(rt);
 for(i=0;i<3;++i){
  owners[i]=(uint32_t)create(dc_goal_intern(rt,"dc-pp-owner"),128);
  CHECK(owners[i]!=0,1,"original process reservation for lexical domain");if(!owners[i])goto restore_methods;
  put(rt,owners[i],36,11+i);
 }
 frame=dc_goal_alloc(rt,48);CHECK(frame!=0,1,"independent fenced frame storage");if(!frame)goto restore_methods;
 memset(dc_goal_resolve(rt,frame,48),0xa5,48);
 CHECK(dc_goal_register_pp_scope(rt,&scopes),1,"KOS-owned external lexical metadata registered");
 CHECK(get(),0,"registering metadata alone does not establish pp");
 CHECK(dc_goal_bind_process_context(rt,owners[0],240),1,"scheduler execution identity A explicitly bound");
 for(i=0;i<3;++i)memcpy(expected[i],dc_goal_resolve(rt,owners[i]-4,240),240);
 used=rt->used;
 CHECK(dc_goal_enter_lexical_scope(rt,owners[1],240),1,"lexical process B enters over scheduler A");
 CHECK(get(),owners[1],"actual compiled getter selects B");CHECK(word(rt,ctx,20),owners[0],"scheduler identity remains A");
 CHECK(rt->used,used,"scope enter consumes no arena");
 for(i=0;i<3;++i)CHECK(memcmp(expected[i],dc_goal_resolve(rt,owners[i]-4,240),240),0,"scope enter writes no process reservation");
 memset(raw,0xa5,48);words[0]=protect;words[1]=dc_goal_find(rt,"protect-frame");words[2]=rt->s7;words[3]=(uint32_t)(uintptr_t)pop;
 memcpy(raw+16,words,16);cur=frame+20;memcpy(expected[1]+92,&cur,4);
 CHECK(make(frame+16,protect,(uint32_t)(uintptr_t)pop),frame+20,"unchanged original constructor uses foreign lexical pp");
 CHECK(memcmp(raw,dc_goal_resolve(rt,frame,48),48),0,"original frame bytes and fences under B");
 for(i=0;i<3;++i)CHECK(memcmp(expected[i],dc_goal_resolve(rt,owners[i]-4,240),240),0,"only B frame-top changed under original constructor");
 CHECK(pop(),rt->s7,"original remove-exit pops B while scheduler is A");
 cur=rt->s7;memcpy(expected[1]+92,&cur,4);
 cur=word(rt,owners[1],84);CHECK(alloc(rt->s7+DC_FIX_SYM_PROCESS_TYPE,frame_type),cur+4,"process basic allocation follows lexical B");
 words[0]=frame_type;memcpy(expected[1]+(cur-(owners[1]-4)),words,4);prior=cur+16;memcpy(expected[1]+88,&prior,4);
 for(i=0;i<3;++i)CHECK(memcmp(expected[i],dc_goal_resolve(rt,owners[i]-4,240),240),0,"B allocation changes its tag/cursor only and preserves A/C");
 CHECK(dc_goal_enter_lexical_scope(rt,owners[2],240),1,"nested lexical C enters");
 CHECK(get(),owners[2],"nested getter returns C");
 cur=word(rt,owners[2],84);CHECK(alloc_object(rt->s7+DC_FIX_SYM_PROCESS_TYPE,frame_type,12),cur+4,"sized process allocation follows nested C");
 words[0]=frame_type;memcpy(expected[2]+(cur-(owners[2]-4)),words,4);prior=cur+16;memcpy(expected[2]+88,&prior,4);
 CHECK(dc_goal_leave_lexical_scope(rt,owners[1],240),0,"out-of-order leave cannot pop C");
 CHECK(dc_goal_pp_scope_depth(rt),2,"failed pop retains both scopes");rt->error=DC_GOAL_DIVIDE_BY_ZERO;
 CHECK(get(),owners[2],"scoped getter still authenticates cleanup identity");CHECK(rt->error,DC_GOAL_DIVIDE_BY_ZERO,"scoped getter preserves retained fault");
 CHECK(dc_goal_leave_lexical_scope(rt,owners[2],240),1,"fault cleanup restores B");CHECK(rt->error,DC_GOAL_DIVIDE_BY_ZERO,"inner leave preserves fault");
 CHECK(alloc(rt->s7+DC_FIX_SYM_PROCESS_TYPE,frame_type),0,"retained fault prevents a fresh scoped allocation");
 CHECK(rt->error,DC_GOAL_DIVIDE_BY_ZERO,"failed allocation does not erase original fault");
 CHECK(dc_goal_leave_lexical_scope(rt,owners[1],240),1,"outer fault cleanup restores A");CHECK(rt->error,DC_GOAL_DIVIDE_BY_ZERO,"outer leave preserves fault");
 rt->error=DC_GOAL_OK;CHECK(get(),owners[0],"scheduler A resumes as unscoped pp");
 cur=word(rt,owners[0],84);CHECK(alloc(rt->s7+DC_FIX_SYM_PROCESS_TYPE,frame_type),cur+4,"allocation domain restores A");
 words[0]=frame_type;memcpy(expected[0]+(cur-(owners[0]-4)),words,4);prior=cur+16;memcpy(expected[0]+88,&prior,4);
 for(i=0;i<3;++i)CHECK(memcmp(expected[i],dc_goal_resolve(rt,owners[i]-4,240),240),0,"full reservation bytes match nested allocation oracle");
 CHECK(dc_goal_enter_lexical_scope(rt,owners[1],240),1,"B scope for binding/clear refusals");
 CHECK(dc_goal_bind_process_heap(rt,owners[2],240),0,"allocation rebinding cannot invalidate a live lexical nest");
 dc_goal_clear_process_context(rt);dc_goal_clear_core_bridges(rt);
 CHECK(dc_goal_core_bridges_active(rt),1,"core clear refuses live scope");CHECK(word(rt,ctx,20),owners[0],"clear refusal preserves scheduler field");
 CHECK(get(),owners[1],"clear refusal retains B execution domain");
 {
  uint32_t sr;uint64_t rejected;int rejected_leave;
  rt->error=DC_GOAL_OK;sr=irq_disable();rejected=get();
  rejected_leave=dc_goal_leave_lexical_scope(rt,owners[1],240);irq_restore(sr);
  CHECK(rejected,0,"unsupported masked-IRQ KOS context cannot supply lexical pp");
  CHECK(rejected_leave,0,"unsupported masked-IRQ owner cannot pop a lexical scope");
  CHECK(dc_goal_pp_scope_depth(rt),1,"owner refusal retains lexical metadata for normal cleanup");
  CHECK(word(rt,ctx,20),owners[0],"masked-IRQ refusal preserves scheduler identity");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"masked-IRQ refusal has explicit retained fault");
 }
 CHECK(dc_goal_leave_lexical_scope(rt,owners[1],240),1,"scope can leave after denied clear");rt->error=DC_GOAL_OK;
 for(k=0;k<2;++k){
  CHECK(dc_goal_enter_lexical_scope(rt,owners[1],240),1,"B scope before stale identity test");
  if(k==0){prior=word(rt,owners[0],36);put(rt,owners[0],36,prior+1);}
  else {prior=word(rt,owners[1],84);put(rt,owners[1],84,prior+1);}
  memset(dc_goal_resolve(rt,frame,48),0xa5,48);memset(raw,0xa5,48);
  for(i=0;i<3;++i)memcpy(expected[i],dc_goal_resolve(rt,owners[i]-4,240),240);
  CHECK(make(frame+16,protect,(uint32_t)(uintptr_t)pop),0,"stale scheduler PID or lexical heap rejects before frame stores");
  CHECK(memcmp(raw,dc_goal_resolve(rt,frame,48),48),0,"stale guard preserves frame and fences");
  for(i=0;i<3;++i)CHECK(memcmp(expected[i],dc_goal_resolve(rt,owners[i]-4,240),240),0,"stale guard makes no process body writes");
  CHECK(word(rt,ctx,20),owners[0],"stale lexical guard preserves scheduler for structured leave");
  CHECK(dc_goal_leave_lexical_scope(rt,owners[1],240),1,"stale outgoing span can restore saved A record");
  if(k==0)put(rt,owners[0],36,prior);else put(rt,owners[1],84,prior);rt->error=DC_GOAL_OK;
 }
 CHECK(dc_goal_bind_process_heap(rt,owners[1],240),1,"depth-zero allocation-only binding retains legacy behavior");
 cur=word(rt,owners[1],84);CHECK(alloc(rt->s7+DC_FIX_SYM_PROCESS_TYPE,frame_type),cur+4,"allocation-only domain allocates explicitly");
 CHECK(word(rt,ctx,20),rt->s7,"allocation-only never publishes execution identity");CHECK(get(),0,"allocation-only still cannot supply implicit pp");
 fiber_owner=(uint32_t)create(dc_goal_intern(rt,"dc-pp-fiber-owner"),1024);
 CHECK(fiber_owner!=0,1,"original process for real native scope/fiber tests");if(!fiber_owner)goto detach;
 stack=dc_goal_alloc(rt,16384);CHECK(stack!=0,1,"owned sixteen KiB working stack");if(!stack)goto detach;
 thread=dc_goal_construct_main_thread(rt,fiber_owner,1136,stack,16384,dc_goal_intern(rt,"dc-pp-thread"),512);
 CHECK(thread!=0,1,"unchanged original CPU-thread constructor supplies backup layout");if(!thread)goto detach;
 fn=(uint32_t)(uintptr_t)native_body;memcpy(code+8,&fn,4);
 CHECK(dc_goal_load_module(rt,&exported,1,sync,NULL,&module),1,"registered GCC scalar native callback entry");
 if(!module.functions)goto detach;
 fn=entry(rt,"dc-pp-native-body");
 CHECK(dc_goal_bind_process_context(rt,fiber_owner,1136),1,"scheduler identity matches the native fiber owner");
 memset(&fiber,0,sizeof fiber);body_rt=rt;body_scopes=&scopes;body_process=owners[1];body_nested_process=owners[2];body_frame=frame;body_type=protect;body_context=ctx;body_owner=fiber_owner;
 CHECK(dc_goal_fiber_init(&fiber,rt,fiber_owner,1136,thread,stack,16384,fn),1,"native context initialized before external-scope refusal");
 CHECK(dc_goal_enter_lexical_scope(rt,owners[1],240),1,"root-owned scope before step attempt");saved_fiber=fiber;out=1234;
 CHECK(dc_goal_fiber_step(&fiber,0,&out),0,"fiber step refuses a live root-owned lexical scope");
 CHECK(memcmp(&saved_fiber,&fiber,sizeof fiber),0,"refused step changes no fiber metadata");CHECK(out,1234,"refused step preserves result output");
 CHECK(dc_goal_leave_lexical_scope(rt,owners[1],240),1,"root scope leaves after refused step");rt->error=DC_GOAL_OK;
 CHECK(dc_goal_fiber_clear(&fiber),1,"quiescent test context clears");
 for(body_mode=0;body_mode<8;++body_mode){
  body_entered=body_lexical=body_scheduler=body_left=body_yield=body_nested_entered=body_nested_getter=0;
  body_fault=DC_GOAL_OK;body_alloc=body_constructed=body_popped=0;
  memset(dc_goal_resolve(rt,frame,48),0xa5,48);cur=word(rt,owners[1],84);rt->error=DC_GOAL_OK;
  CHECK(dc_goal_fiber_init(&fiber,rt,fiber_owner,1136,thread,stack,16384,fn),1,"native fiber setup for scoped return mode");
  out=1234;CHECK(dc_runtime_fiber_checked_step(&fiber,0,&out,&intact),body_mode==0?DC_FIBER_COMPLETED:DC_FIBER_FAIL,"actual native scope return/yield/leak outcome");
  CHECK(intact,1,"native scoped body preserves GCC integer/FP state and root stack");
  CHECK(body_entered,1,"scope enters inside the actual native fiber");CHECK(body_lexical,1,"compiled getter selects B from working-stack C code");
  CHECK(body_scheduler,1,"fiber-owned scheduler field remains independent");
  CHECK(word(rt,ctx,20),body_mode==4?rt->s7:fiber_owner,"native return preserves scheduler or invalidates corrupt execution binding");
  CHECK(dc_goal_pp_scope_depth(rt),0,"normal, refused-yield and leaked returns leave no lexical nest");
  CHECK(rt->error,body_mode==0?DC_GOAL_OK:(body_mode==3||body_mode==5||body_mode==7)?DC_GOAL_DIVIDE_BY_ZERO:DC_GOAL_BAD_ARGUMENT,"fixed return cleanup preserves or creates the correct fault");
  CHECK(out,body_mode==0?77:1234,"only completed native body publishes result");
  if(body_mode==0){
   CHECK(body_constructed,frame+20,"original constructor executes under foreign pp in real native fiber");
   CHECK(body_popped,rt->s7,"original remove-exit pops under foreign pp in real native fiber");
   CHECK(body_alloc,cur+4,"process allocation uses foreign pp from working-stack code");CHECK(word(rt,owners[1],84),cur+16,"native body allocates exactly one aligned object on B");
   CHECK(body_left,1,"normal native body leaves its explicit scope");
   memset(raw,0xa5,48);words[0]=protect;words[1]=dc_goal_find(rt,"protect-frame");words[2]=rt->s7;words[3]=(uint32_t)(uintptr_t)pop;
   memcpy(raw+16,words,16);CHECK(memcmp(raw,dc_goal_resolve(rt,frame,48),48),0,"real fiber constructor/pop preserve exact frame bytes and fences");
  } else if(body_mode==2||body_mode==5){
   CHECK(body_yield,1,"yield refused before phase/SP transfer");CHECK(body_left,1,"retained yield fault still permits structured leave");
   CHECK(body_fault,body_mode==5?DC_GOAL_DIVIDE_BY_ZERO:DC_GOAL_BAD_ARGUMENT,"yield refusal preserves an earlier native fault");
  } else if(body_mode==6||body_mode==7){
   CHECK(body_nested_entered,1,"nested lexical C enters inside real native fiber");
   CHECK(body_nested_getter,1,"nested leaked native scope authenticates C before fixed return");
  }
  rt->error=DC_GOAL_OK;CHECK(get(),body_mode==4?0:fiber_owner,"post-return getter restores pairing or rejects discarded corrupt binding");
  CHECK(dc_goal_fiber_clear(&fiber),1,"completed/faulted native C metadata clears without replay");
  if(body_mode==4){
   CHECK(scopes.depth,DC_GOAL_PP_SCOPE_MAX_DEPTH+1,"invalid return does not memset untrusted caller metadata");
   CHECK(dc_goal_pp_scope_abort(&scopes,rt),1,"quiescent owner explicitly discards detached corrupt descriptor");
   CHECK(dc_goal_register_pp_scope(rt,&scopes),1,"explicit fresh registration after corrupt-return teardown");
   CHECK(dc_goal_bind_process_context(rt,fiber_owner,1136),1,"explicit execution rebinding required after corrupt return");
  }
 }
 body_rt=NULL;body_scopes=NULL;
detach:
 rt->error=DC_GOAL_OK;
 CHECK(dc_goal_pp_scope_detach(&scopes,rt),1,"quiescent lexical metadata detaches");
 dc_goal_clear_process_context(rt);
restore_methods:
 CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"previous object constructor restored after scope cases");
 CHECK(dc_goal_set_method(rt,proc_type,0,old_process),1,"previous process constructor restored after scope cases");
 rt->error=DC_GOAL_OK;
#undef CHECK
 return module.code_bytes;
}
