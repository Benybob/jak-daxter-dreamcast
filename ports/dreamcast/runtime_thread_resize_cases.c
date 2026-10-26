/* Original activation/resize/argument-yield chain (AI-assisted), ISC. */
#include "goal_bridge.h"
#include "goal_thread_resize.h"
#include "fiber_test_support.h"
#include "runtime_cases.h"
#include "build/original_thread_resize.h"
#include <stdlib.h>
#include <string.h>
typedef uint64_t (*one)(uint64_t);
typedef uint64_t (*two)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name){return dc_goal_symbol_value(rt,dc_goal_find(rt,name));}
static uint32_t word(dc_goal_runtime *rt,uint32_t p,unsigned o){uint32_t v;memcpy(&v,dc_goal_resolve(rt,p+o,4),4);return v;}
static void patch(unsigned char *p,unsigned offset,uint32_t value){memcpy(p+offset,&value,4);}
static int filled(const void *p,unsigned n,unsigned char v){const unsigned char *b=p;while(n--)if(*b++!=v)return 0;return 1;}
static unsigned active_preflight,active_result,alias_calls;
extern uint32_t dc_runtime_fiber_word_images[2];
uint32_t dc_runtime_profile_images[2];
static void alias_probe(uint64_t thread,uint64_t size){(void)thread;(void)size;++alias_calls;}
static uint64_t active_probe(void){
 dc_goal_fiber *f=dc_goal_fiber_current();if(!f)return 0;
 active_preflight=(unsigned)dc_goal_thread_resize_check(f->runtime,f->process,f->reservation,f->thread,f->stack_base,f->stack_bytes,4096);
 active_result=(unsigned)dc_goal_resize_main_backup(f->runtime,f->process,f->reservation,f->thread,f->stack_base,f->stack_bytes,4096);
 return UINT64_C(0xabcdef0123456789);
}
uint32_t dc_runtime_thread_resize_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,unsigned *id,dc_goal_cache_sync sync){
 dc_goal_module_result module={0},probe_module={0};
 dc_goal_thread_stack stale;dc_goal_process_heap heap;dc_goal_fiber fiber;
 uint32_t owners[2],roots[2],threads[2],reservation[2]={12400,2160},allocation,stack,name,proc=entry(rt,"process"),tree=entry(rt,"process-tree"),cpu=entry(rt,"cpu-thread");
 uint32_t old_object=dc_goal_get_method(rt,rt->object_type,0),old_proc=dc_goal_get_method(rt,proc,0),old_tree=dc_goal_get_method(rt,tree,0),old_cpu=dc_goal_get_method(rt,cpu,0);
 uint32_t slot9=dc_goal_get_method(rt,cpu,9),format_symbol,old_format,setter_symbol,setter_body,cur,newcur,i,used,symbols,probe_target;
 unsigned intact;unsigned char *before=malloc(12400),cpu_before[128],backup[32];
 uint64_t value,args[8],sum=0;const unsigned weights[6]={1,3,5,7,11,13};
 const uint32_t sizes[]={256,0,16,256,1024,4096,8192,8192};
 unsigned char code[2][12]={{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0},{1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0}};
 dc_goal_code_export probe_exports[2]={{"dc-thread-resize-active-probe",code[0],12,NULL,0},{"dc-thread-resize-alias",code[1],12,NULL,0}};
 one new_tree=(one)(uintptr_t)entry(rt,"dc-new-tree");two new_proc=(two)(uintptr_t)entry(rt,"dc-new-process");
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
 CHECK(before!=NULL,1,"native original resize snapshot allocated");if(!before)return 0;
 CHECK(dc_goal_load_module_data(rt,goal_original_thread_resize_exports,GOAL_ORIGINAL_THREAD_RESIZE_EXPORT_COUNT,
  goal_original_thread_resize_data,GOAL_ORIGINAL_THREAD_RESIZE_DATA_COUNT,sync,NULL,&module),1,"unchanged original void stack-size setter and strings link");
 if(!module.functions){free(before);return 0;}
 CHECK(module.functions,1,"one original void setter exported");CHECK(module.data_objects,2,"original unsupported diagnostic strings remain data objects");
 CHECK(dc_goal_get_method(rt,cpu,9),slot9,"unguarded original method is not automatically installed at slot9");
 format_symbol=dc_goal_find(rt,"format");old_format=dc_goal_symbol_value(rt,format_symbol);
 CHECK(dc_goal_set_symbol(rt,format_symbol,0),1,"unsupported diagnostics deliberately unavailable during valid resize calls");
 setter_symbol=dc_goal_find(rt,"(method stack-size-set! thread)");setter_body=dc_goal_symbol_value(rt,setter_symbol);
 CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"resize owner sized allocator selected");
 CHECK(dc_goal_set_method(rt,proc,0,entry(rt,"(method new process)")),1,"resize original process constructor selected");
 CHECK(dc_goal_set_method(rt,tree,0,entry(rt,"(method new process-tree)")),1,"resize original tree constructor selected");
 CHECK(dc_goal_set_method(rt,cpu,0,entry(rt,"(method new cpu-thread)")),1,"resize original CPU constructor selected");
 dc_goal_clear_process_context(rt);allocation=dc_goal_alloc(rt,8224);stack=allocation+16;
 CHECK(allocation!=0,1,"original resize owns guarded8KiB execution stack");if(!allocation){free(before);return module.code_bytes;}
 memset(dc_goal_resolve(rt,allocation,8224),0xa5,8224);memset(dc_goal_resolve(rt,stack,8192),0xd3,8192);
 for(i=0;i<2;++i){
  name=dc_goal_intern(rt,i?"dc-resize-owner-b":"dc-resize-owner-a");owners[i]=(uint32_t)new_proc(name,reservation[i]-112);roots[i]=(uint32_t)new_tree(name);
  CHECK(owners[i] && roots[i],1,"fresh original activation inputs for backup sizing");if(!owners[i] || !roots[i]){free(before);return module.code_bytes;}
  CHECK(dc_goal_activate_process(rt,owners[i],reservation[i],roots[i],stack,8192,name),owners[i],"unchanged original activation supplies a real main CPU thread");
  threads[i]=word(rt,owners[i],40);CHECK(threads[i]!=rt->s7 && threads[i]!=0,1,"original activated main-thread exists");
  if(!threads[i] || threads[i]==rt->s7){free(before);return module.code_bytes;}
  CHECK(word(rt,threads[i],32),256,"original activated CPU starts with256-byte backup");
 }
 dc_goal_set_method(rt,rt->object_type,0,old_object);dc_goal_set_method(rt,proc,0,old_proc);dc_goal_set_method(rt,tree,0,old_tree);dc_goal_set_method(rt,cpu,0,old_cpu);
 memset(dc_goal_resolve(rt,threads[0]+124,256),0xa5,256);
 CHECK(dc_goal_thread_stack_open(&stale,rt,owners[0],reservation[0],threads[0],stack,8192),1,"borrow old default-capacity image view");
 for(i=0;i<sizeof sizes/sizeof sizes[0];++i){
  memcpy(before,dc_goal_resolve(rt,owners[0]-4,reservation[0]),reservation[0]);newcur=threads[0]-4+128+sizes[i];
  patch(before,88,newcur);patch(before,threads[0]+32-(owners[0]-4),sizes[i]);rt->error=DC_GOAL_OK;
  CHECK(dc_goal_resize_main_backup(rt,owners[0],reservation[0],threads[0],stack,8192,sizes[i]),1,"guard calls unchanged original void setter for grow/shrink/no-op");
  CHECK(rt->error,DC_GOAL_OK,"valid original setter avoids format/error branches");
  CHECK(memcmp(before,dc_goal_resolve(rt,owners[0]-4,reservation[0]),reservation[0]),0,"original setter changes only capacity and heap-cur words");
  CHECK(filled(dc_goal_resolve(rt,stack,8192),8192,0xd3),1,"data-only setter never uses execution stack");
 }
 memcpy(backup,dc_goal_resolve(rt,threads[0]+124,32),32);
 CHECK(dc_goal_thread_stack_backup(&stale,stack+8176),0,"changed capacity invalidates old borrowed stack-image view");
 CHECK(stale.valid,0,"stale image view remains closed");CHECK(memcmp(backup,dc_goal_resolve(rt,threads[0]+124,32),32),0,"stale view cannot overwrite resized backup");
 {
  const uint32_t bad[]={1,1920,1936,8208,UINT32_MAX};
  for(i=0;i<sizeof bad/sizeof bad[0];++i){memcpy(before,dc_goal_resolve(rt,owners[1]-4,reservation[1]),reservation[1]);rt->error=DC_GOAL_OK;
   CHECK(dc_goal_resize_main_backup(rt,owners[1],reservation[1],threads[1],stack,8192,bad[i]),0,"unsupported native resize rejected before original body");
   CHECK(rt->error,(i==1 || i==2)?DC_GOAL_OUT_OF_MEMORY:DC_GOAL_BAD_ARGUMENT,"native resize policy error class");
   CHECK(memcmp(before,dc_goal_resolve(rt,owners[1]-4,reservation[1]),reservation[1]),0,"native rejected resize leaves entire owner unchanged");
  }
 }
 CHECK(dc_goal_process_heap_open(&heap,rt,owners[1],reservation[1]),1,"later-allocation resize probe heap opens");
 CHECK(dc_goal_process_alloc(&heap,16)!=0,1,"known later process allocation exists");memcpy(before,dc_goal_resolve(rt,owners[1]-4,reservation[1]),reservation[1]);
 CHECK(dc_goal_resize_main_backup(rt,owners[1],reservation[1],threads[1],stack,8192,256),1,"original same-size no-op accepts later heap allocation");
 CHECK(memcmp(before,dc_goal_resolve(rt,owners[1]-4,reservation[1]),reservation[1]),0,"same-size original call leaves later allocation untouched");
 CHECK(dc_goal_resize_main_backup(rt,owners[1],reservation[1],threads[1],stack,8192,512),0,"changed size after later allocation rejected before diagnostic branch");
 CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"later-allocation failure is deliberate policy error");
 CHECK(memcmp(before,dc_goal_resolve(rt,owners[1]-4,reservation[1]),reservation[1]),0,"later-allocation rejection leaves all owner bytes");
 memcpy(before,dc_goal_resolve(rt,owners[0]-4,reservation[0]),reservation[0]);dc_goal_set_symbol(rt,setter_symbol,rt->s7);
 CHECK(dc_goal_resize_main_backup(rt,owners[0],reservation[0],threads[0],stack,8192,4096),0,"missing registered original setter fails before body");
 CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"missing setter provenance explicit");CHECK(memcmp(before,dc_goal_resolve(rt,owners[0]-4,reservation[0]),reservation[0]),0,"missing original entry cannot mutate owner");
 dc_goal_set_symbol(rt,setter_symbol,setter_body);
 for(i=0;i<6;++i){args[i]=UINT64_C(0xabcdef1234567890)+i*UINT64_C(0x1122334455667789);sum+=args[i]*weights[i];}
 CHECK(dc_goal_resize_main_backup(rt,owners[0],reservation[0],threads[0],stack,8192,1024),1,"prepare smaller capacity before context-lifetime probe");
 CHECK(dc_goal_fiber_init_words(&fiber,rt,owners[0],reservation[0],threads[0],stack,8192,entry(rt,"dc-fiber-words-six"),6,args),1,"context pins old backup capacity");
 /* Deliberately violate the no-live-context resize precondition: old context
  * must fail closed, with no restore/entry. This is not permission to resize it. */
 CHECK(dc_goal_resize_main_backup(rt,owners[0],reservation[0],threads[0],stack,8192,2048),1,"explicit capacity change makes old context stale");value=UINT64_C(0x1122334455667788);
 CHECK(dc_goal_fiber_step(&fiber,0,&value),0,"stale NEW context cannot execute after capacity change");CHECK(fiber.phase,DC_FIBER_FAULT,"stale capacity faults context");
 CHECK(value,UINT64_C(0x1122334455667788),"stale context preserves root output");CHECK(filled(dc_goal_resolve(rt,stack,8192),8192,0xd3),1,"stale context leaves working stack poisoned");CHECK(dc_goal_fiber_clear(&fiber),1,"old context explicitly released");
 CHECK(dc_goal_resize_main_backup(rt,owners[0],reservation[0],threads[0],stack,8192,8192),1,"original setter grows real main backup for native frames");
 memcpy(cpu_before,dc_goal_resolve(rt,threads[0]-4,128),128);
 CHECK(dc_goal_fiber_init_words(&fiber,rt,owners[0],reservation[0],threads[0],stack,8192,entry(rt,"dc-fiber-words-six"),6,args),1,"activated/resized original main CPU accepts explicit startup arguments");
 CHECK(dc_goal_bind_process_context(rt,owners[0],reservation[0]),1,"startup body identity bound explicitly");
 CHECK(dc_runtime_fiber_checked_step(&fiber,0,&value,&intact),DC_FIBER_YIELDED,"activated/resized main CPU runs real GOAL and yields");CHECK(intact,1,"real main-thread startup preserves root CPU state");
 CHECK(value,sum^UINT64_C(0x123456789abcdef0),"real main-thread startup receives all64-bit words");dc_goal_clear_process_context(rt);memset(dc_goal_resolve(rt,stack,8192),0xd3,8192);
 CHECK(dc_goal_bind_process_context(rt,owners[0],reservation[0]),1,"startup continuation identity explicitly rebound");
 CHECK(dc_runtime_fiber_checked_step(&fiber,UINT64_C(0x9876543210abcdef),&value,&intact),DC_FIBER_COMPLETED,"real original main CPU finishes after exact image restoration");
 CHECK(intact,1,"real main-thread completion restores root registers");CHECK(value,sum+UINT64_C(0x9876543210abcdef),"startup body result survives suspension");dc_goal_clear_process_context(rt);
 CHECK(memcmp(cpu_before,dc_goal_resolve(rt,threads[0]-4,128),128),0,"private native continuation does not repurpose original CPU header");
 CHECK(word(rt,owners[0],32),dc_goal_find(rt,"ready"),"explicit C driver leaves original ready status unchanged");CHECK(dc_goal_fiber_clear(&fiber),1,"completed real main-thread context released");
 /* Measure these deterministic resource-free bodies on the real activated CPU.
  * Disposing their first probe is explicit test setup, not a game retry policy.
  * Frame-chain false makes their original remove-exit calls no-ops. */
 args[6]=UINT64_C(0xfedcba9876543210);args[7]=UINT64_C(0x123456789abcdef0);
 for(i=0;i<2;++i){
  unsigned event;uint32_t needed,budget,peak;
  const uint64_t sentinel=UINT64_C(0x1122334455667788),resume=UINT64_C(0x9876543210abcdef),last=UINT64_C(0x76543210fedcba98);
  uint64_t extra=args[6]+17*args[7];
  uint32_t body=entry(rt,i?"dc-fiber-words-eight":"dc-fiber-words-six");
  CHECK(dc_goal_resize_main_backup(rt,owners[0],reservation[0],threads[0],stack,8192,256),1,"profile starts at unchanged original256-byte default");
  CHECK(word(rt,owners[0],88),rt->s7,"profile fixture owns no frame cleanup resources");
  memcpy(cpu_before,dc_goal_resolve(rt,threads[0]-4,128),128);memcpy(backup,dc_goal_resolve(rt,threads[0]+124,32),32);
  CHECK(dc_goal_fiber_init_words(&fiber,rt,owners[0],reservation[0],threads[0],stack,8192,body,i?8:6,args),1,"default-capacity profile explicitly initialized");
  CHECK(fiber.peak_requested_image_bytes,0,"fresh profile has no suspension observations");
  CHECK(dc_goal_bind_process_context(rt,owners[0],reservation[0]),1,"profile process identity explicitly bound");value=sentinel;
  event=(unsigned)dc_runtime_fiber_checked_step(&fiber,0,&value,&intact);needed=fiber.peak_requested_image_bytes;
  CHECK(intact,1,"default-capacity profile returns complete root state");
  CHECK(needed>0 && needed<=8192,1,"native first suspension reports bounded requested interval");
  CHECK(event,needed<=256?DC_FIBER_YIELDED:DC_FIBER_FAIL,"default backup success or OOM agrees with measured request");
  CHECK(rt->error,needed<=256?DC_GOAL_OK:DC_GOAL_OUT_OF_MEMORY,"default capacity has explicit matching error class");
  CHECK(fiber.phase,needed<=256?DC_FIBER_SUSPENDED:DC_FIBER_FAULT,"default profile has matching lifecycle outcome");
  CHECK(value,needed<=256?(sum^UINT64_C(0x123456789abcdef0)):sentinel,"default OOM preserves output while success returns actual yield");
  CHECK(needed<=256 || memcmp(backup,dc_goal_resolve(rt,threads[0]+124,32),32)==0,1,"insufficient default budget performs no partial backup");
  CHECK(memcmp(cpu_before,dc_goal_resolve(rt,threads[0]-4,128),128),0,"default measurement preserves original CPU header");
  dc_goal_clear_process_context(rt);CHECK(dc_goal_fiber_clear(&fiber),1,"pure profile context released before resizing");
  CHECK(fiber.peak_requested_image_bytes,0,"explicit clear resets observation");
  budget=(needed+255u)&~255u;
  CHECK(budget>0 && budget<=8192 && budget>=needed,1,"fixture budget rounds observed request to256 bytes");
  CHECK(dc_goal_resize_main_backup(rt,owners[0],reservation[0],threads[0],stack,8192,budget),1,"unchanged original setter installs measured fixture budget");
  memcpy(cpu_before,dc_goal_resolve(rt,threads[0]-4,128),128);memset(dc_goal_resolve(rt,stack,8192),0xd3,8192);
  CHECK(dc_goal_fiber_init_words(&fiber,rt,owners[0],reservation[0],threads[0],stack,8192,body,i?8:6,args),1,"fresh measured-budget context initialized");
  CHECK(dc_goal_bind_process_context(rt,owners[0],reservation[0]),1,"measured-budget entry explicitly bound");
  CHECK(dc_runtime_fiber_checked_step(&fiber,0,&value,&intact),DC_FIBER_YIELDED,"measured backup fits first real suspension");
  CHECK(intact,1,"measured-budget yield preserves root state");CHECK(value,sum^UINT64_C(0x123456789abcdef0),"measured-budget body receives all first six words");
  CHECK(fiber.image.image_bytes,needed,"fresh replay observes same exact first interval");
  dc_goal_clear_process_context(rt);memset(dc_goal_resolve(rt,stack,8192),0xd3,8192);
  CHECK(dc_goal_bind_process_context(rt,owners[0],reservation[0]),1,"measured-budget continuation explicitly rebound");
  CHECK(dc_runtime_fiber_checked_step(&fiber,resume,&value,&intact),i?DC_FIBER_YIELDED:DC_FIBER_COMPLETED,"measured image resumes after stack destruction");
  CHECK(intact,1,"measured-budget resume preserves root state");
  CHECK(value,i?(extra^UINT64_C(0x1020304050607080)):(sum+resume),"measured-budget resumed words and values remain exact");
  dc_goal_clear_process_context(rt);
  if(i){
   memset(dc_goal_resolve(rt,stack,8192),0xd3,8192);
   CHECK(dc_goal_bind_process_context(rt,owners[0],reservation[0]),1,"eight-word final profile continuation explicitly bound");
   CHECK(dc_runtime_fiber_checked_step(&fiber,last,&value,&intact),DC_FIBER_COMPLETED,"eight-word profile completes after second restoration");
   CHECK(intact,1,"eight-word measured-budget completion preserves root state");
   CHECK(value,sum+resume+extra+last,"measured budget preserves all eight words and both resume values");dc_goal_clear_process_context(rt);
  }
  peak=fiber.peak_requested_image_bytes;dc_runtime_profile_images[i]=peak;
  CHECK(peak,needed,"largest profile suspension interval remains measured first request");
  CHECK(peak,dc_runtime_fiber_word_images[i],"same body has same interval on8KiB and16KiB working stacks");
  CHECK(peak<=budget,1,"completed profile stays within explicitly measured backup");
  CHECK(memcmp(cpu_before,dc_goal_resolve(rt,threads[0]-4,128),128),0,"measured profile leaves original CPU header unchanged");
  CHECK(word(rt,owners[0],32),dc_goal_find(rt,"ready"),"measured profile does not invent scheduler transition");
  CHECK(dc_goal_fiber_clear(&fiber),1,"completed profile metadata released");
 }
 CHECK(sizeof(dc_goal_fiber),320,"native context retains register offsets, main import receipt and enter-state restart state");
 CHECK(dc_goal_resize_main_backup(rt,owners[0],reservation[0],threads[0],stack,8192,8192),1,"restore prior capacity for independent active-resize oracle");
 memcpy(cpu_before,dc_goal_resolve(rt,threads[0]-4,128),128);
 probe_target=(uint32_t)(uintptr_t)active_probe;memcpy(code[0]+8,&probe_target,4);
 probe_target=(uint32_t)(uintptr_t)alias_probe;memcpy(code[1]+8,&probe_target,4);
 CHECK(dc_goal_load_module(rt,probe_exports,2,sync,NULL,&probe_module),1,"native active/alias resize oracles registered explicitly");
 CHECK(probe_module.functions,2,"two distinct native rejection oracles registered");
 used=rt->used;symbols=rt->symbol_count;cur=word(rt,owners[0],84);active_preflight=active_result=99;
 alias_calls=0;memcpy(before,dc_goal_resolve(rt,owners[0]-4,reservation[0]),reservation[0]);dc_goal_set_symbol(rt,setter_symbol,entry(rt,"dc-thread-resize-alias"));
 CHECK(dc_goal_resize_main_backup(rt,owners[0],reservation[0],threads[0],stack,8192,4096),0,"registered foreign entry cannot replace the original setter");
 CHECK(rt->error,DC_GOAL_BAD_FUNCTION,"canonical setter export identity required");CHECK(alias_calls,0,"foreign same-ABI callback never called");
 CHECK(memcmp(before,dc_goal_resolve(rt,owners[0]-4,reservation[0]),reservation[0]),0,"foreign setter alias leaves owner unchanged");
 CHECK(rt->used,used,"foreign setter alias rejection allocates no memory");CHECK(rt->symbol_count,symbols,"foreign setter alias rejection interns no names");
 dc_goal_set_symbol(rt,setter_symbol,setter_body);
 CHECK(dc_goal_fiber_init(&fiber,rt,owners[0],reservation[0],threads[0],stack,8192,entry(rt,"dc-thread-resize-active-probe")),1,"live-resize probe context initialized");value=UINT64_C(0x1122334455667788);
 CHECK(dc_runtime_fiber_checked_step(&fiber,0,&value,&intact),DC_FIBER_FAIL,"active wrapper refuses an otherwise-valid size change");
 CHECK(active_preflight,1,"same request passes read-only geometry before active wrapper rejects");CHECK(active_result,0,"active original-call wrapper rejected");
 CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"active size change fault explicit");CHECK(fiber.phase,DC_FIBER_FAULT,"active error returns to root and closes context");
 CHECK(intact,1,"active-resize failure restores root registers");CHECK(value,UINT64_C(0x1122334455667788),"active-resize failure preserves root output");
 CHECK(memcmp(cpu_before,dc_goal_resolve(rt,threads[0]-4,128),128),0,"active guard prevents capacity/header writes");CHECK(word(rt,owners[0],84),cur,"active guard prevents heap-cur writes");
 CHECK(dc_goal_fiber_clear(&fiber),1,"active probe context released");CHECK(rt->used,used,"active probe allocates no arena memory");CHECK(rt->symbol_count,symbols,"active probe interns no names");
 CHECK(filled(dc_goal_resolve(rt,allocation,16),16,0xa5),1,"real main stack lower fence intact");CHECK(filled(dc_goal_resolve(rt,stack+8192,16),16,0xa5),1,"real main stack upper fence intact");
 CHECK(dc_goal_get_method(rt,cpu,9),slot9,"original setter remains guarded C-entry only");CHECK(dc_goal_set_symbol(rt,format_symbol,old_format),1,"diagnostic cell binding restored");
 rt->error=DC_GOAL_OK;free(before);
#undef CHECK
 return module.code_bytes+probe_module.code_bytes;
}
