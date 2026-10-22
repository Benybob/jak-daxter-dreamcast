/* Original base-process activation acceptance oracle (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#include "goal_thread_stack.h"
#include "runtime_cases.h"
#include "build/original_process_activate.h"
#include "build/original_memset32.h"
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef uint64_t (*three)(uint64_t,uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name){return dc_goal_symbol_value(rt,dc_goal_find(rt,name));}
static uint32_t word(dc_goal_runtime *rt,uint32_t at){uint32_t v;memcpy(&v,dc_goal_resolve(rt,at,4),4);return v;}
static void put(dc_goal_runtime *rt,uint32_t at,uint32_t v){memcpy(dc_goal_resolve(rt,at,4),&v,4);}
static int filled(const void *p,unsigned n,unsigned char v){const unsigned char *b=p;while(n--)if(*b++!=v)return 0;return 1;}
uint32_t dc_runtime_activation_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                     unsigned *id,dc_goal_cache_sync sync){
 dc_goal_module_result mem,original;
 uint32_t proc_type=entry(rt,"process"),tree_type=entry(rt,"process-tree"),cpu=entry(rt,"cpu-thread");
 uint32_t ctx=entry(rt,"*kernel-context*"),names[2],owners[2],roots[2],stack,small,raw,thread,pid,i,j,used,symbols;
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_proc=dc_goal_get_method(rt,proc_type,0);
 uint32_t old_tree=dc_goal_get_method(rt,tree_type,0),old_cpu=dc_goal_get_method(rt,cpu,0);
 unsigned char owner_before[1136],root_before[32],context_before[48],zero[88]={0};
 one new_tree=(one)(uintptr_t)entry(rt,"dc-new-tree");
 two new_process=(two)(uintptr_t)entry(rt,"dc-new-process");
 three set;
 dc_goal_thread_stack image;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 CHECK(dc_goal_load_module(rt,goal_original_memset32_exports,GOAL_ORIGINAL_MEMSET32_EXPORT_COUNT,sync,NULL,&mem),1,"unchanged original word memset links");
 CHECK(dc_goal_load_module(rt,goal_original_process_activate_exports,GOAL_ORIGINAL_PROCESS_ACTIVATE_EXPORT_COUNT,sync,NULL,&original),1,"unchanged original activation links with original dependencies");
 if(!mem.functions || !original.functions)return 0;
 CHECK(mem.functions,1,"one original word fill");CHECK(original.functions,1,"one original activation method");
 set=(three)(uintptr_t)entry(rt,"mem-set32!");raw=dc_goal_alloc(rt,64);
 for(i=0;i<5;++i){
  memset(dc_goal_resolve(rt,raw,64),0xa5,64);
  CHECK(set(raw+16,i,UINT64_C(0x87654321abcdef01)),raw+16,"original word fill returns destination");
  CHECK(filled(dc_goal_resolve(rt,raw,16),16,0xa5),1,"word fill prefix guard");
  for(j=0;j<i;++j)CHECK(word(rt,raw+16+4*j),0xabcdef01,"word fill stores low32 bits per word");
  CHECK(filled(dc_goal_resolve(rt,raw+16+4*i,48-4*i),48-4*i,0xa5),1,"word fill exact word count and suffix guard");
 }
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"explicit sized allocator selected");
 CHECK(dc_goal_set_method(rt,proc_type,0,entry(rt,"(method new process)")),1,"original process constructor selected");
 CHECK(dc_goal_set_method(rt,tree_type,0,entry(rt,"(method new process-tree)")),1,"original tree constructor selected");
 CHECK(dc_goal_set_method(rt,cpu,0,entry(rt,"(method new cpu-thread)")),1,"original CPU constructor selected");
 dc_goal_clear_process_context(rt);names[0]=dc_goal_intern(rt,"dc-activation-a");names[1]=dc_goal_string(rt,"dc-activation-b");
 for(i=0;i<2;++i){owners[i]=(uint32_t)new_process(names[0],1024);roots[i]=(uint32_t)new_tree(names[0]);CHECK(owners[i]!=0 && roots[i]!=0,1,"fresh original process and empty original tree");}
 small=(uint32_t)new_process(names[0],384);stack=dc_goal_alloc(rt,8224)+16;
 CHECK(small!=0 && stack>16,1,"exact-fit rejection owner and guarded working stack allocated");
 if(!owners[0] || !owners[1] || !roots[0] || !roots[1] || !small || stack<=16)return mem.code_bytes+original.code_bytes;
 memset(dc_goal_resolve(rt,stack-16,8224),0xd3,8224);used=rt->used;symbols=rt->symbol_count;
 /* Invalid arguments cannot publish a PID/status/thread or alter the tree. */
 for(i=0;i<12;++i){
  uint32_t owner=owners[0],reservation=1136,dest=roots[0],base=stack,bytes=8192,name=names[0];
  switch(i){case 0:reservation=1135;break;case 1:base++;break;case 2:bytes=4095;break;
   case 3:base=owners[0]+108;bytes=4096;break;case 4:bytes=0;break;case 5:base=ctx-4;bytes=4096;break;
   case 6:base=rt->table;break;case 7:name=owners[0];break;case 8:dest=owners[0];break;
   case 9:name=0;break;case 10:reservation=UINT32_MAX;break;case 11:base=0x70000000u;break;}
  memcpy(owner_before,dc_goal_resolve(rt,owners[0]-4,1136),1136);memcpy(root_before,dc_goal_resolve(rt,roots[0]-4,32),32);
  memcpy(context_before,dc_goal_resolve(rt,ctx-4,48),48);
  rt->error=DC_GOAL_OK;
  CHECK(dc_goal_activate_process(rt,owner,reservation,dest,base,bytes,name),0,"unsupported activation arguments rejected before body");
  CHECK(rt->error!=DC_GOAL_OK,1,"invalid activation has explicit error");
  CHECK(memcmp(owner_before,dc_goal_resolve(rt,owners[0]-4,1136),1136),0,"argument rejection leaves whole process reservation unchanged");
  CHECK(memcmp(root_before,dc_goal_resolve(rt,roots[0]-4,32),32),0,"argument rejection leaves destination unchanged");
  CHECK(memcmp(context_before,dc_goal_resolve(rt,ctx-4,48),48),0,"argument rejection leaves PID/context unchanged");
  CHECK(filled(dc_goal_resolve(rt,stack-16,8224),8224,0xd3),1,"argument rejection never touches working stack");
 }
 {
  struct mutation {uint32_t at,value;} cases[]={
   {owners[0]+32,dc_goal_find(rt,"ready")},{owners[0]+8,roots[0]+24},{owners[0]+12,roots[0]+24},
   {owners[0]+16,roots[0]+24},{owners[0]+40,owners[1]},{owners[0]+44,owners[1]},
   {owners[0]+84,owners[0]+124},{roots[0]+16,owners[0]+24},{roots[0]+4,0},
   {ctx+20,owners[0]},{ctx+24,owners[0]},{ctx+12,0x7fffffffu},
   {cpu+16,entry(rt,"mem-set32!")},{dc_goal_find(rt,"mem-set32!"),rt->s7},
   {proc_type+12,(word(rt,proc_type+12)&0xffff0000u)|16u},
   {cpu+8,(word(rt,cpu+8)&0xffff0000u)|129u},
   {owners[0]+80,owners[0]+92},{owners[0]+80,owners[0]-4+1136+16}};
  for(i=0;i<sizeof cases/sizeof cases[0];++i){
   uint32_t saved=word(rt,cases[i].at);put(rt,cases[i].at,cases[i].value);
   memcpy(owner_before,dc_goal_resolve(rt,owners[0]-4,1136),1136);memcpy(root_before,dc_goal_resolve(rt,roots[0]-4,32),32);
   memcpy(context_before,dc_goal_resolve(rt,ctx-4,48),48);
   rt->error=DC_GOAL_OK;
   CHECK(dc_goal_activate_process(rt,owners[0],1136,roots[0],stack,8192,names[0]),0,"stale or unsupported activation state rejected");
   CHECK(rt->error!=DC_GOAL_OK,1,"stale activation state reports error");
   CHECK(memcmp(owner_before,dc_goal_resolve(rt,owners[0]-4,1136),1136),0,"state rejection preserves process bytes");
   CHECK(memcmp(root_before,dc_goal_resolve(rt,roots[0]-4,32),32),0,"state rejection preserves tree bytes");
   CHECK(memcmp(context_before,dc_goal_resolve(rt,ctx-4,48),48),0,"state rejection preserves PID and context");
   CHECK(filled(dc_goal_resolve(rt,stack-16,8224),8224,0xd3),1,"state rejection preserves execution span and fences");
   put(rt,cases[i].at,saved);
  }
 }
 memcpy(owner_before,dc_goal_resolve(rt,small-4,496),496);memcpy(root_before,dc_goal_resolve(rt,roots[0]-4,32),32);
 memcpy(context_before,dc_goal_resolve(rt,ctx-4,48),48);rt->error=DC_GOAL_OK;
 CHECK(dc_goal_activate_process(rt,small,496,roots[0],stack,8192,names[0]),0,"exact384-byte thread capacity rejected before original unchecked bump");
 CHECK(rt->error,DC_GOAL_OUT_OF_MEMORY,"strict process heap headroom required");
 CHECK(word(rt,small+32),dc_goal_find(rt,"dead"),"OOM does not publish ready status");
 CHECK(word(rt,small+84),small+108,"OOM leaves heap cursor");
 CHECK(memcmp(owner_before,dc_goal_resolve(rt,small-4,496),496),0,"OOM leaves complete small reservation unchanged");
 CHECK(memcmp(root_before,dc_goal_resolve(rt,roots[0]-4,32),32),0,"OOM leaves destination unchanged");
 CHECK(memcmp(context_before,dc_goal_resolve(rt,ctx-4,48),48),0,"OOM leaves next PID and kernel context unchanged");
 CHECK(filled(dc_goal_resolve(rt,stack-16,8224),8224,0xd3),1,"OOM leaves execution span and fences unchanged");
 pid=word(rt,ctx+12);
 for(i=0;i<2;++i){
  put(rt,roots[i]+4,0xffffffffu);
  put(rt,owners[i]+92,0x11223344u);put(rt,owners[i]+96,0x55667788u);
  put(rt,owners[i]+100,0x12345678u);put(rt,owners[i]+104,0x87654321u);
  CHECK(dc_goal_activate_process(rt,owners[i],1136,roots[i],stack,8192,names[i]),owners[i],"unchanged original activation returns attached process");
  CHECK(rt->error,DC_GOAL_OK,"activation succeeds without allocator/self binding");
  CHECK(word(rt,owners[i]),names[i],"activation sets symbol or string name");
  CHECK(word(rt,owners[i]+4),0xfffffc3fu,"original inherited mask clears sleep/tree/heap-shrunk bits");
  CHECK(word(rt,owners[i]+32),dc_goal_find(rt,"ready"),"original status becomes ready without running code");
  CHECK(word(rt,owners[i]+48),rt->s7,"original activation resets entity for tree destination");
  CHECK(word(rt,owners[i]+52),rt->s7,"original activation resets state");
  for(j=0;j<3;++j)CHECK(word(rt,owners[i]+56+4*j),rt->s7,"original activation clears each hook to false");
  CHECK(word(rt,owners[i]+72),rt->s7,"original next-state false");
  CHECK(word(rt,owners[i]+88),rt->s7,"original stack-frame-top false");
  CHECK(word(rt,owners[i]+100),rt->s7,"original connection next1 resets to false");
  CHECK(word(rt,owners[i]+104),rt->s7,"original connection prev1 resets to false");
  CHECK(word(rt,owners[i]+92),0x11223344u,"original connection next0 remains untouched");
  CHECK(word(rt,owners[i]+96),0x55667788u,"original connection prev0 remains untouched");
  CHECK(word(rt,owners[i]+28),rt->s7,"original pool remains untouched");
  CHECK(word(rt,owners[i]+68),1024,"original allocated length remains untouched");
  CHECK(word(rt,owners[i]+80),owners[i]+108+1024,"original heap top remains untouched");
  CHECK(word(rt,owners[i]+20),owners[i]+24,"original process ppointer remains self location");
  CHECK(word(rt,owners[i]+24),owners[i],"original process self remains exact owner");
  CHECK(word(rt,owners[i]+36),pid+i,"original process receives next unique PID");
  CHECK(word(rt,ctx+12),pid+i+1,"original next-pid advances once");
  CHECK(word(rt,ctx+20),rt->s7,"activation never installs implicit execution identity");
  thread=owners[i]+112;
  CHECK(word(rt,owners[i]+40),thread,"original activation installs main-thread");
  CHECK(word(rt,owners[i]+44),thread,"original CPU constructor installs top-thread");
  CHECK(word(rt,owners[i]+76),owners[i]+108,"base-process heap offset remains original");
  CHECK(word(rt,owners[i]+84),owners[i]+108+384,"original main-thread header and256-byte backup consume384 bytes");
  CHECK(word(rt,thread-4),cpu,"original CPU type tag");CHECK(word(rt,thread),dc_goal_find(rt,"code"),"original main thread named code");
  CHECK(word(rt,thread+4),owners[i],"original CPU owner");CHECK(word(rt,thread+8),rt->s7,"first main thread has no previous");
  CHECK(word(rt,thread+12),dc_goal_get_method(rt,cpu,10),"suspend hook remains original configured slot");
  CHECK(word(rt,thread+16),dc_goal_get_method(rt,cpu,11),"resume hook remains original configured slot");
  CHECK(word(rt,thread+20),0,"activation does not initialize a runnable PC");
  CHECK(word(rt,thread+24),stack+8192,"original initial SP");CHECK(word(rt,thread+28),stack+8192,"original execution stack top");
  CHECK(word(rt,thread+32),256,"unchanged original default backup capacity");
  CHECK(memcmp(dc_goal_resolve(rt,thread+36,88),zero,88),0,"activation does not synthesize saved registers");
  CHECK(filled(dc_goal_resolve(rt,thread+124,256),256,0),1,"fresh original backup stays zero");
  CHECK(word(rt,owners[i]+8),roots[i]+24,"process parent is exact destination ppointer");
  CHECK(word(rt,roots[i]+16),owners[i]+24,"tree child is exact process ppointer");
  CHECK(word(rt,owners[i]+12),rt->s7,"empty destination gives false brother");
  CHECK(dc_goal_thread_stack_open(&image,rt,owners[i],1136,thread,stack,8192),1,"activated main CPU thread validates with original image helper");
  CHECK(filled(dc_goal_resolve(rt,stack-16,8224),8224,0xd3),1,"activation is data-only and leaves execution stack untouched");
  CHECK(dc_goal_activate_process(rt,owners[i],1136,roots[i],stack,8192,names[i]),0,"repeated activation cannot reset a live process");
 }
 CHECK(rt->used,used,"activation allocates no additional global arena bytes");CHECK(rt->symbol_count,symbols,"activation interns no names");
 CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"object method restored");CHECK(dc_goal_set_method(rt,proc_type,0,old_proc),1,"process method restored");
 CHECK(dc_goal_set_method(rt,tree_type,0,old_tree),1,"tree method restored");CHECK(dc_goal_set_method(rt,cpu,0,old_cpu),1,"CPU method restored");
 rt->error=DC_GOAL_OK;
#undef CHECK
 return mem.code_bytes+original.code_bytes;
}
