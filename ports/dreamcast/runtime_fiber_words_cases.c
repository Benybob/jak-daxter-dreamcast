/* Explicit scalar words survive suspension on a shared stack (AI-assisted), ISC. */
#include "fiber_test_support.h"
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "build/fiber_words_fixture.h"
#include <string.h>
typedef uint64_t (*two)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name){return dc_goal_symbol_value(rt,dc_goal_find(rt,name));}
static uint32_t word(dc_goal_runtime *rt,uint32_t p,unsigned o){uint32_t v;memcpy(&v,dc_goal_resolve(rt,p+o,4),4);return v;}
static void put(dc_goal_runtime *rt,uint32_t p,unsigned o,uint32_t v){memcpy(dc_goal_resolve(rt,p+o,4),&v,4);}
static int filled(const void *p,unsigned n,unsigned char v){const unsigned char *b=p;while(n--)if(*b++!=v)return 0;return 1;}
uint32_t dc_runtime_fiber_words_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id,dc_goal_cache_sync sync){
 dc_goal_fiber f[2];dc_goal_module_result bridge,module;
 uint32_t owners[2],threads[2],frames[2][2],allocation,stack,proc=entry(rt,"process"),old_object,old_proc;
 uint32_t used,symbols,entries[2],i,j,arity[2]={6,8},weights[6]={1,3,5,7,11,13};
 uint64_t words[2][8],copied[2][8],sum[2]={0},extra,value;
 const uint64_t send[3]={UINT64_C(0xabcdef0123456789),UINT64_C(0x9876543210abcdef),UINT64_C(0x76543210fedcba98)};
 unsigned intact;unsigned char header[2][128],backup[32];
 two new_process=(two)(uintptr_t)entry(rt,"dc-new-process");
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 CHECK(dc_goal_install_fiber_bridge(rt,sync,&bridge),1,"explicit word fiber yield bridge installed");
 CHECK(dc_goal_set_symbol(rt,dc_goal_intern(rt,"dc-fiber-pop"),entry(rt,"remove-exit")),1,"word bodies use unchanged original pop under explicit binding");
 CHECK(dc_goal_load_module(rt,goal_fiber_words_fixture_exports,GOAL_FIBER_WORDS_FIXTURE_EXPORT_COUNT,sync,NULL,&module),1,"six/eight-word compiled GOAL fixture links");
 if(!bridge.functions || !module.functions)return 0;
 CHECK(module.functions,2,"six and eight explicit scalar entry prototypes");
 entries[0]=entry(rt,"dc-fiber-words-six");entries[1]=entry(rt,"dc-fiber-words-eight");
 old_object=dc_goal_get_method(rt,rt->object_type,0);old_proc=dc_goal_get_method(rt,proc,0);
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"word owner sized allocation selected");
 CHECK(dc_goal_set_method(rt,proc,0,entry(rt,"(method new process)")),1,"word owner original construction selected");
 dc_goal_clear_process_context(rt);allocation=dc_goal_alloc(rt,16416);stack=allocation+16;
 CHECK(allocation!=0,1,"word contexts share one owned working stack with fences");if(!allocation)return module.code_bytes+bridge.code_bytes;
 memset(dc_goal_resolve(rt,allocation,16416),0xa5,16416);
 for(i=0;i<2;++i){
  uint32_t name=dc_goal_intern(rt,i?"dc-word-owner-b":"dc-word-owner-a");
  owners[i]=(uint32_t)new_process(name,12288);CHECK(owners[i]!=0,1,"original process owns word context backup");
  if(!owners[i])return module.code_bytes+bridge.code_bytes;
  threads[i]=dc_goal_construct_main_thread(rt,owners[i],12400,stack,16384,name,8192);
  CHECK(threads[i]!=0,1,"word context original CPU backup constructed");if(!threads[i])return module.code_bytes+bridge.code_bytes;
  memcpy(header[i],dc_goal_resolve(rt,threads[i]-4,128),128);
  for(j=0;j<2;++j){frames[i][j]=dc_goal_new_basic(rt,entry(rt,"stack-frame"),12);CHECK(frames[i][j]!=0,1,"original frame for word-body process identity");
   if(!frames[i][j])return module.code_bytes+bridge.code_bytes;
   put(rt,frames[i][j],0,name);put(rt,frames[i][j],4,j?frames[i][0]:rt->s7);}
  put(rt,owners[i],88,frames[i][1]);
  for(j=0;j<8;++j){words[i][j]=copied[i][j]=(UINT64_C(0xabcdef1234567890)^((j+1)*UINT64_C(0x1234567887654321)))+i*UINT64_C(0xdeadbeef10203040);if(j<6)sum[i]+=words[i][j]*weights[j];}
  CHECK(dc_goal_fiber_init_words(&f[i],rt,owners[i],12400,threads[i],stack,16384,entries[i],arity[i],words[i]),1,"explicit startup words copied into owned C context");
  CHECK(f[i].argument_count,arity[i],"entry arity is explicit");CHECK(memcmp(f[i].arguments,copied[i],arity[i]*8),0,"both halves of every copied word match");
  memset(words[i],0,sizeof words[i]);CHECK(memcmp(f[i].arguments,copied[i],arity[i]*8),0,"caller may overwrite source vector after init");
 }
 extra=copied[1][6]+17*copied[1][7];used=rt->used;symbols=rt->symbol_count;
 /* Bad saved count is rejected before first entry, without reading arguments. */
 f[0].argument_count=9;value=UINT64_C(0x1122334455667788);
 CHECK(dc_goal_fiber_step(&f[0],0,&value),0,"invalid NEW arity rejected before native entry");CHECK(f[0].phase,DC_FIBER_FAULT,"invalid NEW arity closes context");
 CHECK(value,UINT64_C(0x1122334455667788),"invalid NEW arity preserves output");CHECK(filled(dc_goal_resolve(rt,allocation,16416),16416,0xa5),1,"invalid NEW arity does not touch execution stack");
 f[0].argument_count=6;CHECK(dc_goal_fiber_step(&f[0],0,&value),0,"repairing invalid arity does not resurrect context");
 CHECK(dc_goal_fiber_init_words(&f[0],rt,owners[0],12400,threads[0],stack,16384,entries[0],6,f[0].arguments),1,"explicit reinit copies old owned vector before clearing");
 for(i=0;i<2;++i){
  CHECK(dc_goal_bind_process_context(rt,owners[i],12400),1,"word body explicit owner selected");
  CHECK(dc_runtime_fiber_checked_step(&f[i],0,&value,&intact),DC_FIBER_YIELDED,"six/eight-word GOAL body reaches first yield");
  CHECK(intact,1,"word entry preserves root GPR/FPR/control/canaries");CHECK(value,sum[i]^UINT64_C(0x123456789abcdef0),"first yield uses all six full64-bit argument words");
  CHECK(word(rt,owners[i],88),frames[i][0],"first original pop affects the explicitly selected owner");
  CHECK(f[i].image.saved,1,"word continuation has an exact pending image");CHECK(f[i].image.image_bytes<=8192 && f[i].image.image_bytes>0,1,"word continuation fits explicit backup");
  dc_goal_clear_process_context(rt);memset(dc_goal_resolve(rt,stack,16384),0xd3,16384);
 }
 for(i=0;i<2;++i){
  CHECK(dc_goal_bind_process_context(rt,owners[i],12400),1,"resume word owner explicitly rebound");
  CHECK(dc_runtime_fiber_checked_step(&f[i],send[i],&value,&intact),i?DC_FIBER_YIELDED:DC_FIBER_COMPLETED,"word continuations resume after shared stack destruction");
  CHECK(intact,1,"word resume preserves complete root C state");
  CHECK(value,i?(extra^UINT64_C(0x1020304050607080)):(sum[0]+send[0]),"word results include resumed value and stack-passed arguments");
  CHECK(word(rt,owners[i],88),rt->s7,"second original pop completes the matching owner chain");
  dc_goal_clear_process_context(rt);memset(dc_goal_resolve(rt,stack,16384),0xd3,16384);
 }
 CHECK(dc_goal_bind_process_context(rt,owners[1],12400),1,"eight-word outer frame explicit final owner");
 CHECK(dc_runtime_fiber_checked_step(&f[1],send[2],&value,&intact),DC_FIBER_COMPLETED,"eight-word outer frame completes after second restore");
 CHECK(intact,1,"eight-word final return preserves root state");CHECK(value,sum[1]+send[1]+extra+send[2],"eight full64-bit words and both resume values survive nested yields");
 dc_goal_clear_process_context(rt);
 /* A malformed pending count must not restore the poisoned destination. */
 CHECK(dc_goal_fiber_init_words(&f[0],rt,owners[0],12400,threads[0],stack,16384,entries[0],6,copied[0]),1,"fresh word context for pending-count failure");
 CHECK(dc_goal_bind_process_context(rt,owners[0],12400),1,"pending-count owner bound");
 CHECK(dc_runtime_fiber_checked_step(&f[0],0,&value,&intact),DC_FIBER_YIELDED,"pending-count probe creates real suspended image");
 memcpy(backup,dc_goal_resolve(rt,threads[0]+124,32),32);memset(dc_goal_resolve(rt,stack,16384),0xd3,16384);
 f[0].argument_count=9;value=UINT64_C(0x1122334455667788);
 CHECK(dc_goal_fiber_step(&f[0],0,&value),0,"invalid paused arity rejected before stack restoration");CHECK(f[0].phase,DC_FIBER_FAULT,"invalid paused arity permanently closes image");
 CHECK(value,UINT64_C(0x1122334455667788),"invalid paused arity preserves output");CHECK(filled(dc_goal_resolve(rt,stack,16384),16384,0xd3),1,"invalid paused arity leaves destination poisoned");
 CHECK(memcmp(backup,dc_goal_resolve(rt,threads[0]+124,32),32),0,"invalid paused arity leaves original backup prefix unchanged");
 f[0].argument_count=6;CHECK(dc_goal_fiber_step(&f[0],0,&value),0,"count repair cannot revive faulted pending frame");dc_goal_clear_process_context(rt);
 for(i=0;i<2;++i){CHECK(memcmp(header[i],dc_goal_resolve(rt,threads[i]-4,128),128),0,"word execution does not repurpose original CPU register header");
  CHECK(word(rt,owners[i],32),dc_goal_find(rt,"dead"),"word fibers do not invent scheduler status transitions");
  CHECK(word(rt,owners[i],40),rt->s7,"word fibers do not install original main-thread");CHECK(dc_goal_fiber_clear(&f[i]),1,"word context metadata released");}
 CHECK(filled(dc_goal_resolve(rt,allocation,16),16,0xa5),1,"word stack lower fence");CHECK(filled(dc_goal_resolve(rt,stack+16384,16),16,0xa5),1,"word stack upper fence");
 CHECK(rt->used,used,"word yield/resume allocates no global arena bytes");CHECK(rt->symbol_count,symbols,"word execution interns no names");
 CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"word sized constructor restored");CHECK(dc_goal_set_method(rt,proc,0,old_proc),1,"word original constructor restored");
 rt->error=DC_GOAL_OK;
#undef CHECK
 return module.code_bytes+bridge.code_bytes;
}
