/* Behavioral checks independent of the proposed copy implementation (AI-assisted), ISC. */
#include "thread_stack_cases.h"
#include "goal_thread_stack.h"
#include "goal_kernel_types.h"
#include <stdlib.h>
#include <string.h>
static uint32_t word(dc_goal_runtime *rt,uint32_t object,unsigned offset) {
  uint32_t value; memcpy(&value,dc_goal_resolve(rt,object+offset,4),4); return value;
}
static void set_word(dc_goal_runtime *rt,uint32_t object,unsigned offset,uint32_t value) {
  memcpy(dc_goal_resolve(rt,object+offset,4),&value,4);
}
static int byte_value(const void *memory,unsigned bytes,unsigned char expected) {
  const unsigned char *p=memory; unsigned i;
  for(i=0;i<bytes;++i) if(p[i]!=expected) return 0;
  return 1;
}
void dc_thread_stack_cases(dc_runtime_check check,void *context,unsigned *id) {
  const uint32_t capacity=524288;
  void *allocation=malloc(capacity+64);
  unsigned char *memory,*execution,*backup;
  dc_goal_runtime rt;
  dc_kernel_types_result installed;
  dc_goal_process_heap heap;
  dc_goal_thread_stack view;
  dc_goal_type_fields *schema;
  uint32_t process_type,cpu_type,process,raw,thread,stack,top,cur,old,used,original_sp;
  unsigned char original[512],header[128],process_before[112],snapshot[1136],execution_before[544];
  const unsigned corrupt_offsets[]={4,28,32};
  unsigned n,i,j;
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(allocation!=NULL,1,"stack-image shared fixture allocation");
  if(!allocation) return;
  memory=(unsigned char*)(((uintptr_t)allocation+15u)&~(uintptr_t)15u)+16;
  memset(memory-16,0xa5,capacity+32);
  CHECK(dc_goal_init(&rt,memory,capacity,0x01000000u),1,"portable original-layout stack fixture runtime");
  CHECK(dc_goal_install_kernel_types(&rt,&installed),1,"original fixed process/thread schemas installed");
  process_type=dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_PROCESS_TYPE);
  cpu_type=dc_goal_define_type(&rt,"cpu-thread",dc_goal_symbol_value(&rt,rt.s7+DC_FIX_SYM_THREAD_TYPE),128,0,12);
  CHECK(cpu_type!=0,1,"original CPU-thread size/method flags supplied");
  process=dc_goal_new_basic(&rt,process_type,1136);
  CHECK(process!=0,1,"known process reservation with1024-byte heap");
  if(!process || !cpu_type) { free(allocation); return; }
  set_word(&rt,process,68,1024); set_word(&rt,process,76,process+108);
  set_word(&rt,process,80,process+108+1024); set_word(&rt,process,84,process+108);
  set_word(&rt,process,32,dc_goal_intern(&rt,"dead"));
  CHECK(dc_goal_process_heap_open(&heap,&rt,process,1136),1,"owned process heap validates");
  raw=dc_goal_process_alloc(&heap,384); thread=raw+4;
  CHECK(raw!=0,1,"CPU-thread plus256-byte backup occupies actually allocated process memory");
  if(!raw) { free(allocation); return; }
  memset(dc_goal_resolve(&rt,raw,128),0x6d,128);
  set_word(&rt,thread-4,0,cpu_type); set_word(&rt,thread,4,process);
  set_word(&rt,process,44,thread);
  stack=dc_goal_alloc(&rt,544)+16; top=stack+512;
  execution=dc_goal_resolve(&rt,stack,512); backup=dc_goal_resolve(&rt,thread+124,264);
  CHECK(execution!=NULL && backup!=NULL,1,"disjoint execution/backup/guard ranges resolve");
  if(!execution || !backup) { free(allocation); return; }
  memset(dc_goal_resolve(&rt,stack-16,544),0xa5,544);
  set_word(&rt,thread,24,top); set_word(&rt,thread,28,top); set_word(&rt,thread,32,256);
  memcpy(header,dc_goal_resolve(&rt,raw,128),128);
  memcpy(process_before,dc_goal_resolve(&rt,process-4,112),112);
  cur=word(&rt,process,84); used=rt.used; original_sp=word(&rt,thread,24);
  for(i=0;i<512;++i) original[i]=(unsigned char)(i*37u+11u);
  for(n=0;n<=256;++n) {
    memcpy(execution,original,512); memset(backup,0xd3,264);
    CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),1,"fresh explicit stack-image view opens");
    CHECK(dc_goal_thread_stack_backup(&view,top-n),1,"every exact byte depth0..256 backs up without rounding");
    CHECK(view.image_bytes,n,"saved byte length is exact");
    CHECK(view.image_sp,top-n,"saved sidecar range is exact");
    CHECK(view.saved,1,"one pending image after backup");
    CHECK(memcmp(backup,original+512-n,n),0,"backup equals independently indexed execution suffix");
    CHECK(byte_value(backup+n,264-n,0xd3),1,"all bytes after exact backup image retain sentinel");
    CHECK(memcmp(header,dc_goal_resolve(&rt,raw,128),128),0,"backup preserves entire original header/register/FP block");
    memset(execution,0x5a,512);
    CHECK(dc_goal_thread_stack_restore(&view),1,"exact stack image restores without context switch");
    CHECK(byte_value(execution,512-n,0x5a),1,"restore leaves every byte below active suffix untouched");
    CHECK(memcmp(execution+512-n,original+512-n,n),0,"restore recovers exact independently saved suffix");
    CHECK(byte_value(dc_goal_resolve(&rt,stack-16,16),16,0xa5),1,"low execution canary preserved");
    CHECK(byte_value(dc_goal_resolve(&rt,top,16),16,0xa5),1,"high execution canary preserved");
    CHECK(memcmp(header,dc_goal_resolve(&rt,raw,128),128),0,"restore preserves header/register/FP bytes");
    CHECK(view.saved,0,"restore consumes pending image");
    CHECK(rt.error,DC_GOAL_OK,"exact image roundtrip has clear status");
  }
  CHECK(memcmp(process_before,dc_goal_resolve(&rt,process-4,112),112),0,"all exact image operations preserve complete process fields/header");
  CHECK(word(&rt,thread,24),original_sp,"data helper never writes original thread SP field");
  CHECK(word(&rt,process,84),cur,"image operations never move process heap cursor");
  CHECK(rt.used,used,"image operations allocate no memory");
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1135,thread,stack,512),0,"wrong explicit process reservation rejects");
  CHECK(view.valid,0,"failed open cannot retain old view");
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,0,stack,512),0,"missing CPU-thread reference rejects");
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,0),0,"empty execution span rejects");
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,top,0),0,"empty span with matching exclusive top still rejects");
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,0xfffffff0u,32),0,"overflowing execution span rejects before addition");
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,513),0,"execution span must match original stack-top exactly");
  old=word(&rt,thread,28); set_word(&rt,thread,28,process-4+128);
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,process-4,128),0,"overlapping owned execution/process spans reject");
  set_word(&rt,thread,28,old);
  old=word(&rt,thread,32); set_word(&rt,thread,32,257);
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),0,"new view cannot claim backup beyond actually allocated thread extent");
  set_word(&rt,thread,32,old);
  set_word(&rt,thread-4,0,process_type);
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),0,"wrong CPU object header rejects");
  CHECK(rt.error,DC_GOAL_BAD_TYPE,"wrong CPU header exposes type error");
  set_word(&rt,thread-4,0,cpu_type);
  old=word(&rt,process,44); set_word(&rt,process,44,rt.s7);
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),0,"parent top-thread must identify exact CPU thread");
  set_word(&rt,process,44,old);
  for(i=0;i<4;++i) {
    uint32_t sp=i==0?stack-1:(i==1?top+1:(i==2?top-257:stack));
    memcpy(snapshot,dc_goal_resolve(&rt,process-4,1136),1136);
    memcpy(execution_before,dc_goal_resolve(&rt,stack-16,544),544);
    CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),1,"view opens before invalid SP/depth probe");
    CHECK(dc_goal_thread_stack_backup(&view,sp),0,"outside SP or over-capacity image rejects");
    CHECK(rt.error,i<2?DC_GOAL_BAD_ARGUMENT:DC_GOAL_OUT_OF_MEMORY,"SP/depth error category");
    CHECK(view.valid,0,"failed copy invalidates explicit view");
    CHECK(memcmp(snapshot,dc_goal_resolve(&rt,process-4,1136),1136),0,"SP failure changes no process/thread/backup byte");
    CHECK(memcmp(execution_before,dc_goal_resolve(&rt,stack-16,544),544),0,"SP failure changes no execution/guard byte");
  }
  for(i=0;i<3;++i) for(j=0;j<2;++j) {
    CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),1,"view opens before stale field probe");
    if(j) CHECK(dc_goal_thread_stack_backup(&view,top-17),1,"pending exact image before stale restore");
    old=word(&rt,thread,corrupt_offsets[i]); set_word(&rt,thread,corrupt_offsets[i],old+1);
    memcpy(snapshot,dc_goal_resolve(&rt,process-4,1136),1136);
    memcpy(execution_before,dc_goal_resolve(&rt,stack-16,544),544);
    CHECK(j?dc_goal_thread_stack_restore(&view):dc_goal_thread_stack_backup(&view,top-17),0,"owner/top/capacity mutation rejects on backup and restore");
    CHECK(memcmp(snapshot,dc_goal_resolve(&rt,process-4,1136),1136),0,"stale metadata failure preserves all process/backup bytes");
    CHECK(memcmp(execution_before,dc_goal_resolve(&rt,stack-16,544),544),0,"stale metadata failure preserves all execution bytes");
    set_word(&rt,thread,corrupt_offsets[i],old);
    CHECK(dc_goal_thread_stack_backup(&view,top-17),0,"repair alone cannot resurrect invalidated view");
  }
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),1,"view before stale allocation cursor");
  set_word(&rt,process,84,raw+128);
  CHECK(dc_goal_thread_stack_backup(&view,top-17),0,"backup cannot borrow unallocated process bytes");
  set_word(&rt,process,84,cur);
  schema=dc_goal_resolve(&rt,cpu_type,sizeof(*schema));
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),1,"view before CPU schema mutation");
  schema->allocated_size=127;
  CHECK(dc_goal_thread_stack_backup(&view,top-17),0,"mutated CPU schema fails closed");
  CHECK(rt.error,DC_GOAL_BAD_TYPE,"CPU schema failure is explicit type error"); schema->allocated_size=128;
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),1,"view before state guard probes");
  CHECK(dc_goal_thread_stack_restore(&view),0,"restore without pending image rejects");
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),1,"explicit reopening after no-image fault");
  CHECK(dc_goal_thread_stack_backup(&view,top-17),1,"first pending image");
  memcpy(snapshot,dc_goal_resolve(&rt,process-4,1136),1136);
  CHECK(dc_goal_thread_stack_backup(&view,top-17),0,"duplicate backup cannot replace pending image silently");
  CHECK(memcmp(snapshot,dc_goal_resolve(&rt,process-4,1136),1136),0,"duplicate backup leaves pending payload untouched");
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),1,"new explicit image scope");
  CHECK(dc_goal_thread_stack_backup(&view,top-17),1,"new pending image");
  CHECK(dc_goal_thread_stack_restore(&view),1,"one restore succeeds");
  CHECK(dc_goal_thread_stack_restore(&view),0,"second restore cannot reuse consumed image");
  CHECK(dc_goal_thread_stack_open(&view,&rt,process,1136,thread,stack,512),1,"view before forged image range");
  CHECK(dc_goal_thread_stack_backup(&view,top-17),1,"image before forged saved byte count");
  ++view.image_bytes;
  CHECK(dc_goal_thread_stack_restore(&view),0,"inconsistent saved byte count rejects");
  dc_goal_thread_stack_clear(&view);
  CHECK(view.valid,0,"clear releases view validity"); CHECK(view.saved,0,"clear releases pending image");
  CHECK(dc_goal_thread_stack_backup(&view,top),0,"cleared view cannot be used");
  CHECK(byte_value(memory-16,16,0xa5),1,"runtime low guard preserved");
  CHECK(byte_value(memory+capacity,16,0xa5),1,"runtime high guard preserved");
  free(allocation);
#undef CHECK
}
