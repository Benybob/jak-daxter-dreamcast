/* Original remove-exit with deliberate bounded process identity (AI-assisted), ISC. */
#include <stdint.h>
#include <string.h>
#include "goal_bridge.h"
#include "runtime_cases.h"
#include "build/original_process_entry.h"
#include "build/context_read_fixture.h"
typedef uint64_t (*zero)(void);
typedef uint64_t (*two)(uint64_t,uint64_t);
typedef uint64_t (*three)(uint64_t,uint64_t,uint64_t);
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
static uint64_t checked_construct(three target,uint64_t allocation,uint64_t type,uint64_t exit,uint32_t *intact) {
  register uint32_t a __asm__("r8")=0x12345678,b __asm__("r9")=0x87654321;
  register uint32_t c __asm__("r10")=0xabcdef01,d __asm__("r11")=0x76543210;
  register uint32_t e __asm__("r12")=0xfedcba98,f __asm__("r13")=0x13579bdf;
  volatile uint32_t guards[4]={0x01234567,0x89abcdef,0x76543210,0xfedcba98};
  uint64_t result;
  __asm__ volatile("" : "+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f));
  result=target(allocation,type,exit);
  __asm__ volatile("" : "+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f));
  *intact=a==0x12345678 && b==0x87654321 && c==0xabcdef01 && d==0x76543210 &&
          e==0xfedcba98 && f==0x13579bdf && guards[0]==0x01234567 && guards[1]==0x89abcdef &&
          guards[2]==0x76543210 && guards[3]==0xfedcba98;
  return result;
}
uint32_t dc_runtime_process_entry_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                       unsigned *id,dc_goal_cache_sync sync) {
  dc_goal_module_result original,bridge,loop_module;
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
  CHECK(original.functions,2,"original remove-exit and protect constructor exported");
  CHECK(dc_goal_load_module(rt,goal_context_read_fixture_exports,GOAL_CONTEXT_READ_FIXTURE_EXPORT_COUNT,
                           sync,NULL,&loop_module),1,"explicit context loop CFG oracle links");
  CHECK(loop_module.functions,1,"one explicit argument context loop export");
  if(!loop_module.functions)return original.code_bytes+bridge.code_bytes;
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
  {
    three make=(three)(uintptr_t)entry(rt,"(method new protect-frame)");
    uint32_t spans[3],protect=entry(rt,"protect-frame"),name=dc_goal_find(rt,"protect-frame");
    unsigned char raw[48],owner_bytes[240];uint32_t values[4],mode,k,prior;
    CHECK(make!=NULL,1,"unchanged original protect-frame constructor is linked");
    if(!make)return original.code_bytes+bridge.code_bytes+loop_module.code_bytes;
    for(k=0;k<3;++k) {
      spans[k]=dc_goal_alloc(rt,48);CHECK(spans[k]!=0,1,"owned frame plus two sixteen-byte canaries allocated");
      if(!spans[k])return original.code_bytes+bridge.code_bytes;
      memset(dc_goal_resolve(rt,spans[k],48),0xa5,48);
    }
    for(mode=0;mode<3;++mode) {
      dc_goal_clear_process_context(rt);
      if(mode==1) CHECK(dc_goal_bind_process_heap(rt,owners[0],240),1,"constructor allocation-only context is explicit");
      if(mode==2) {
        CHECK(dc_goal_bind_process_context(rt,owners[0],240),1,"constructor execution context before stale identity");
        set_word(rt,ctx,20,owners[1]);
      }
      memcpy(raw,dc_goal_resolve(rt,spans[0],48),48);
      memcpy(owner_bytes,dc_goal_resolve(rt,owners[0]-4,240),240);used=rt->used;
      CHECK(make(spans[0]+16,protect,(uint32_t)(uintptr_t)pop),0,"missing/allocation-only/stale context exits before original frame stores");
      CHECK(rt->error,DC_GOAL_BAD_ARGUMENT,"constructor context refusal reports explicit error");
      CHECK(memcmp(raw,dc_goal_resolve(rt,spans[0],48),48),0,"rejected constructor preserves complete frame and fences");
      CHECK(memcmp(owner_bytes,dc_goal_resolve(rt,owners[0]-4,240),240),0,"rejected constructor preserves complete process reservation");
      CHECK(rt->used,used,"rejected constructor allocates nothing");
      set_word(rt,ctx,20,rt->s7);
    }
    for(i=0;i<2;++i) {
      CHECK(dc_goal_bind_process_context(rt,owners[i],240),1,"original protect frames use deliberately selected execution owner");
      {
        typedef uint64_t (*one)(uint64_t);one loop=(one)(uintptr_t)entry(rt,"dc-context-read-loop");
        set_word(rt,owners[i],36,17);
        CHECK(loop(0),0,"read-only context guard preserves zero-trip original loop control flow");
        CHECK(loop(3),54,"context definition and remapped back edges retain explicit argument and process PID");
        set_word(rt,owners[i],36,0);
      }
      for(k=0;k<3;++k) {
        memset(dc_goal_resolve(rt,spans[k],48),0xa5,48);memset(raw,0xa5,48);
        prior=word(rt,owners[i],88);values[0]=protect;values[1]=name;values[2]=prior;values[3]=(uint32_t)(uintptr_t)pop;
        memcpy(raw+16,values,16);memcpy(owner_bytes,dc_goal_resolve(rt,owners[i]-4,240),240);
        cur=spans[k]+20;memcpy(owner_bytes+92,&cur,4);used=rt->used;
        CHECK(checked_construct(make,spans[k]+16,protect,(uint32_t)(uintptr_t)pop,&intact),cur,"original three-argument constructor returns complete zero-extended basic reference");
        CHECK(intact,1,"context getter and stacked third argument preserve GCC frame/callee saves");
        CHECK(memcmp(raw,dc_goal_resolve(rt,spans[k],48),48),0,"original constructor writes exact type/name/next/exit with both fences intact");
        CHECK(memcmp(owner_bytes,dc_goal_resolve(rt,owners[i]-4,240),240),0,"only original process stack-frame-top changes");
        CHECK(rt->used,used,"original protect constructor uses supplied storage without allocating");
        CHECK(word(rt,ctx,20),owners[i],"read-only pp capture does not change scheduler identity");
        CHECK(rt->error,DC_GOAL_OK,"original protect constructor succeeds under authenticated entry");
      }
      for(k=3;k>0;--k) CHECK(pop(),k>1?spans[k-2]+20:rt->s7,"original remove-exit pops constructed protect frames in reverse order");
      CHECK(word(rt,owners[1-i],88),rt->s7,"constructor for one owner leaves the other owner's empty frame chain unchanged");
    }
    dc_goal_clear_process_context(rt);
  }
  CHECK(dc_goal_set_method(rt,rt->object_type,0,old_object),1,"sized constructor binding restored");
  CHECK(dc_goal_set_method(rt,process_type,0,old_process),1,"process constructor binding restored");
  rt->error=DC_GOAL_OK;
#undef CHECK
  return original.code_bytes+bridge.code_bytes+loop_module.code_bytes;
}
