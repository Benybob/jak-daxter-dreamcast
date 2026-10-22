/* Original remove-exit with deliberate bounded process identity (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "build/original_process_entry.h"
typedef uint64_t (*zero)(void);
typedef uint64_t (*two)(uint64_t,uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t word(dc_goal_runtime *rt,uint32_t object,unsigned offset) {
  uint32_t v; memcpy(&v,dc_goal_resolve(rt,object+offset,4),4); return v;
}
static void set_word(dc_goal_runtime *rt,uint32_t object,unsigned offset,uint32_t value) {
  memcpy(dc_goal_resolve(rt,object+offset,4),&value,4);
}
/* Independent GCC caller keeps known callee-saved values live across either
 * native exit. R14 and PR restoration also matter to the surrounding C frame. */
static uint64_t checked_call(zero target,uint32_t *intact) {
  register uint32_t a __asm__("r8")=0x12345678,b __asm__("r9")=0x87654321;
  register uint32_t c __asm__("r10")=0xabcdef01,d __asm__("r11")=0x76543210;
  register uint32_t e __asm__("r12")=0xfedcba98,f __asm__("r13")=0x13579bdf;
  volatile uint32_t guards[4]={0x01234567,0x89abcdef,0x76543210,0xfedcba98};
  uint64_t result;
  __asm__ volatile("" : "+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f));
  result=target();
  __asm__ volatile("" : "+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f));
  *intact=a==0x12345678 && b==0x87654321 && c==0xabcdef01 && d==0x76543210 &&
          e==0xfedcba98 && f==0x13579bdf && guards[0]==0x01234567 && guards[1]==0x89abcdef &&
          guards[2]==0x76543210 && guards[3]==0xfedcba98;
  return result;
}
uint32_t dc_runtime_process_entry_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                       unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result original,bridge;
  uint32_t old_object,old_process,process_type=entry(rt,"process"),frame_type=entry(rt,"stack-frame");
  uint32_t owners[2],frames[2],ctx=entry(rt,"*kernel-context*"),symbol=dc_goal_find(rt,"*kernel-context*");
  uint32_t i,j,cur,intact,used=rt->used,symbols=rt->symbol_count;
  unsigned char expected[240],frame_bytes[2][12];
  uint64_t result;
  zero pop,basic_getter;
  two new_process=(two)(uintptr_t)entry(rt,"dc-new-process");
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(dc_goal_load_module(rt,goal_original_process_entry_exports,GOAL_ORIGINAL_PROCESS_ENTRY_EXPORT_COUNT,
                           sync,NULL,&original),0,"implicit entry requires installed native getter import");
  CHECK(rt->error,DC_GOAL_BAD_MODULE,"missing process getter import explicit error");
  CHECK(rt->used,used,"missing getter module load rolls back arena");
  CHECK(rt->symbol_count,symbols,"missing getter module load rolls back symbols");
  CHECK(dc_goal_install_process_bridge(rt,sync,&bridge),1,"guarded process getter installed explicitly");
  if(!bridge.functions) return 0;
  CHECK(dc_goal_load_module(rt,goal_original_process_entry_exports,GOAL_ORIGINAL_PROCESS_ENTRY_EXPORT_COUNT,
                           sync,NULL,&original),1,"unchanged original remove-exit links with guarded import");
  if(!original.functions) return bridge.code_bytes;
  CHECK(original.functions,1,"only original remove-exit behavior exported");
  pop=(zero)(uintptr_t)entry(rt,"remove-exit");
  basic_getter=(zero)(uintptr_t)entry(rt,"dc-c-current-process");
  old_object=dc_goal_get_method(rt,rt->object_type,0); old_process=dc_goal_get_method(rt,process_type,0);
  CHECK(dc_goal_set_method(rt,rt->object_type,0,entry(rt,"dc-c-object-new")),1,"existing sized constructor bridge selected");
  CHECK(dc_goal_set_method(rt,process_type,0,entry(rt,"(method new process)")),1,"unchanged original owner constructor selected");
  dc_goal_clear_process_context(rt);
  for(i=0;i<2;++i) {
    owners[i]=(uint32_t)new_process(dc_goal_intern(rt,"dc-entry-owner"),128);
    CHECK(owners[i]!=0,1,"original process constructor supplies known reservation");
    frames[i]=dc_goal_new_basic(rt,frame_type,12);
    CHECK(frames[i]!=0,1,"original twelve-byte stack-frame schema allocated");
    if(!owners[i] || !frames[i]) return original.code_bytes+bridge.code_bytes;
    set_word(rt,frames[i],0,dc_goal_intern(rt,"dc-entry-frame"));
    set_word(rt,frames[i],4,i?frames[0]:rt->s7);
    memcpy(frame_bytes[i],dc_goal_resolve(rt,frames[i]-4,12),12);
  }
  set_word(rt,owners[0],88,frames[1]);
  memcpy(expected,dc_goal_resolve(rt,owners[0]-4,240),240);
  CHECK(checked_call(pop,&intact),0,"missing execution identity safely exits original behavior");
  CHECK(intact,1,"missing-context native exit preserves GCC frame and callee saves");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"missing execution identity explicit error");
  CHECK(memcmp(expected,dc_goal_resolve(rt,owners[0]-4,240),240),0,"missing context touches no original process bytes");
  CHECK(dc_goal_bind_process_heap(rt,owners[0],240),1,"allocation target binds independently");
  CHECK(pop(),0,"allocation-only binding never becomes execution identity");
  CHECK(word(rt,ctx,20),rt->s7,"allocation-only binding leaves original current-process false");
  CHECK(memcmp(expected,dc_goal_resolve(rt,owners[0]-4,240),240),0,"allocation-only rejected entry preserves process bytes");
  for(i=0;i<2;++i) {
    CHECK(dc_goal_bind_process_context(rt,owners[i],240),1,"deliberate original process execution binding");
    CHECK(word(rt,ctx,20),owners[i],"original kernel current-process matches execution identity");
    CHECK(basic_getter(),owners[i],"boxed getter returns exact zero-extended self address");
    for(j=0;j<3;++j) {
      set_word(rt,owners[i],88,j?frames[j-1]:rt->s7);
      memcpy(expected,dc_goal_resolve(rt,owners[i]-4,240),240);
      cur=j==2?frames[0]:rt->s7;
      memcpy(expected+4+88,&cur,4);
      result=checked_call(pop,&intact);
      CHECK(result,cur,"original remove-exit pops exactly one of zero/one/two frames");
      CHECK(intact,1,"successful implicit entry preserves GCC frame and callee saves");
      CHECK(rt->error,DC_GOAL_OK,"successful guarded entry clears error");
      CHECK(memcmp(expected,dc_goal_resolve(rt,owners[i]-4,240),240),0,"only original stack-frame-top changes");
      CHECK(word(rt,ctx,20),owners[i],"remove-exit preserves explicit execution identity");
    }
    CHECK(pop(),rt->s7,"remaining original frame pops on next call");
    CHECK(pop(),rt->s7,"empty stack remains false");
  }
  for(i=0;i<2;++i) CHECK(memcmp(frame_bytes[i],dc_goal_resolve(rt,frames[i]-4,12),12),0,"remove-exit never rewrites linked stack-frame objects");
  CHECK(word(rt,owners[0],88),rt->s7,"rebinding second owner leaves former owner independent");
  CHECK(dc_goal_bind_process_heap(rt,owners[0],240),1,"allocation change explicitly invalidates execution identity");
  CHECK(word(rt,ctx,20),rt->s7,"allocation change clears owned original current-process");
  CHECK(pop(),0,"former execution context cannot survive allocation rebinding");
  CHECK(dc_goal_bind_process_context(rt,owners[0],240),1,"explicit context can be rebound after invalidation");
  CHECK(dc_goal_bind_process_context(rt,owners[1],239),0,"invalid context reservation rejects and clears old identity");
  CHECK(word(rt,ctx,20),rt->s7,"failed rebinding clears owned current-process");
  CHECK(pop(),0,"failed context rebind cannot use former owner");
  CHECK(dc_goal_bind_process_context(rt,owners[0],240),1,"valid context before stale heap test");
  cur=word(rt,owners[0],84); set_word(rt,owners[0],84,cur+1);
  CHECK(pop(),0,"entry revalidates mutated process heap metadata before body");
  CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"stale process metadata explicit error");
  CHECK(word(rt,ctx,20),rt->s7,"stale reservation invalidates original context binding");
  set_word(rt,owners[0],84,cur);
  CHECK(pop(),0,"repairing fields cannot resurrect an invalidated execution context");
  CHECK(dc_goal_bind_process_context(rt,owners[0],240),1,"valid context before global rebinding test");
  CHECK(dc_goal_set_symbol(rt,symbol,rt->s7),1,"external original context global changed");
  CHECK(pop(),0,"stale kernel context global rejects before body");
  CHECK(word(rt,ctx,20),rt->s7,"stale global releases former owned context field");
  CHECK(dc_goal_set_symbol(rt,symbol,ctx),1,"original context global restored explicitly");
  CHECK(dc_goal_bind_process_context(rt,owners[0],240),1,"valid context before current-process mismatch");
  set_word(rt,ctx,20,owners[1]);
  CHECK(pop(),0,"external current-process mismatch rejects before body");
  CHECK(word(rt,ctx,20),owners[1],"failure does not overwrite a foreign current-process value");
  CHECK(dc_goal_bind_process_context(rt,owners[0],240),0,"nonfalse foreign context requires explicit restoration");
  set_word(rt,ctx,20,rt->s7);
  CHECK(dc_goal_bind_process_context(rt,owners[1],240),1,"restored context can be rebound deliberately");
  dc_goal_clear_process_context(rt);
  CHECK(word(rt,ctx,20),rt->s7,"scope clear releases original current-process");
  CHECK(checked_call(pop,&intact),0,"cleared execution scope exits safely");
  CHECK(intact,1,"cleared-context path restores native stack and callee saves");
  CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"sized constructor binding restored");
  CHECK(dc_goal_set_method(rt,process_type,0,old_process),1,"process constructor binding restored");
  rt->error=DC_GOAL_OK;
#undef CHECK
  return original.code_bytes+bridge.code_bytes;
}
