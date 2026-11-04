/* Run original start/init-target/dispatcher/read-pad (AI-assisted), ISC.
 * No stores to target, control, state, position or follow matrices. The only
 * changed GOAL binding is an authenticated read-pad call observer, restored at
 * every exit. The private beach package is not passed off as an original level. */
#ifdef DC_ARENA_JAK_PLAYER
#include "arena_jak_player.h"
#include "arena_jak.h"
#include "arena_jak_exports.h"
#include "goal_art.h"
#include "goal_main_start.h"
#include "goal_bridge.h"
#include "process_spans.h"
#include "build/jak-art-disc/jak_art_sizes.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef __DREAMCAST__
#include <arch/cache.h>
#endif
volatile dc_jak_player_evidence dc_arena_jak_player_evidence={.version=1};
typedef char player_receipt_bytes[sizeof(dc_jak_player_evidence)==7804?1:-1];
static dc_goal_runtime *runtime;
static dc_goal_fiber target_fiber;
static uint32_t original_read,read_symbol,slot;
static uint32_t word(dc_goal_runtime *rt,uint32_t p){
 uint32_t v=0;const void *b=dc_goal_resolve(rt,p,4);if(b)memcpy(&v,b,4);return v;
}
static uint32_t named(dc_goal_runtime *rt,const char *name){
 enum dc_goal_error error=rt->error;uint32_t s=dc_goal_find(rt,name),v=s?word(rt,s):0;
 if(error!=DC_GOAL_OK)rt->error=error;
 return v;
}
volatile struct {
 uint32_t bytes,target_length,resolved,typed,w28,top,w36,w40,w44,cp,w48,span_found,span_bytes,error,site,w32;
 char w36_name[32],w32_name[32];
} dc_arena_jak_thread_probe;
static int typed(dc_goal_runtime *rt,uint32_t p,const char *type,uint32_t bytes){
 uint32_t t=named(rt,type);
 return p>=4 && (p&15u)==4 && t && t!=rt->s7 && dc_goal_resolve(rt,p-4,bytes) && word(rt,p-4)==t;
}
/* Same authentication as an export: symbol value, code registry, and that
 * record's owner is this symbol. dc_goal_find clears a prior fault. */
static uint32_t code_entry(dc_goal_runtime *rt,const char *name){
 dc_goal_code_entry_info info;enum dc_goal_error saved=rt->error;
 uint32_t symbol=dc_goal_find(rt,name),value=symbol?dc_goal_symbol_value(rt,symbol):0;
 if(saved!=DC_GOAL_OK){rt->error=saved;return 0;}
 if(rt->error!=DC_GOAL_OK || !value || !dc_goal_code_entry(rt,value,&info)){
  if(rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_FUNCTION;
  return 0;
 }
 if(info.symbol!=symbol){rt->error=DC_GOAL_BAD_FUNCTION;return 0;}
 return value;
}
/* process next-state is port +72 (retail 76). A state is the 36-byte type
 * whose parent is protect-frame. Original defstate binds the name symbol to
 * that object. */
static int original_state(dc_goal_runtime *rt,uint32_t state){
 uint32_t name;int ok=typed(rt,state,"state",36);
 if(ok){name=word(rt,state);
  ok=name && dc_goal_type_of(rt,name)==rt->symbol_type &&
   dc_goal_symbol_value(rt,name)==state && rt->error==DC_GOAL_OK;}
 if(!ok && rt->error==DC_GOAL_OK)rt->error=DC_GOAL_BAD_OBJECT;
 return ok;
}
static int check(dc_goal_runtime *rt,int ok,uint32_t why,const char *text){
 if(!ok){dc_arena_jak_player_evidence.failure=why;
  dc_arena_jak_player_evidence.error=rt?rt->error:DC_GOAL_BAD_ARGUMENT;dc_arena_jak_player_evidence.site=dc_goal_error_site;
  dc_arena_jak_evidence.blocker=why;dc_arena_jak_evidence.runtime_error=dc_arena_jak_player_evidence.error;
  dc_arena_jak_evidence.error_site=dc_goal_error_site;
  snprintf((char *)dc_arena_jak_evidence.missing,sizeof dc_arena_jak_evidence.missing,"%s",text);
 }
 return dc_arena_live_check(ok,1,text);
}
#define CHECK(ok,why,text) do{if(!check(rt,(ok),(why),(text)))goto done;}while(0)
int dc_arena_jak_player_active(void){return dc_arena_jak_player_evidence.active!=0;}
int dc_arena_jak_player_pad_state(int port,dc_goal_pad_state *out){
 dc_goal_pad_state s={0};if(!out)return 0;
 if(port!=0){*out=s;return 0;}
 /* Two lateral legs, neutral before/between/after. Every read in a dispatch
  * returns the same sample. Advancing is done only on the root. */
 if(slot>=32 && slot<64)s.joyx=96;
 if(slot>=96 && slot<128)s.joyx=-96;
 *out=s;return 1;
}
void dc_arena_jak_player_resume(dc_goal_runtime *rt,uint32_t process,int completed){
 if(rt!=runtime || process!=dc_arena_jak_player_evidence.target)return;
 if(completed){++dc_arena_jak_player_evidence.resumes;++dc_arena_jak_evidence.target_resumes;}
 else ++dc_arena_jak_player_evidence.attempts;
}
void dc_arena_jak_player_pad(dc_goal_runtime *rt,uint32_t pad){
 dc_goal_pad_state s;uint8_t expect[132]={0};const uint8_t *p;
 if(rt!=runtime || !(p=dc_goal_resolve(rt,pad,132)))return;
 if(word(rt,pad+32)!=0)return;
 ++dc_arena_jak_player_evidence.pad_services;
 dc_arena_jak_player_pad_state(0,&s);dc_goal_pad_fill(expect,&s);
 if(p[0]==6 && !memcmp(p+1,expect+1,19))++dc_arena_jak_player_evidence.pad_valid;
 else rt->error=DC_GOAL_BAD_OBJECT;
}
static uint64_t observe_read(uint64_t vector){
 dc_goal_runtime *rt=runtime;uint64_t result;
 uint32_t control,process;float magnitude;dc_goal_pad_state sample;
 /* read-pad also runs in original post hooks on the registered kernel stack.
  * The runtime's authenticated execution identity covers both main and post;
  * requiring a main fiber here would incorrectly reject the real post path. */
 process=dc_goal_mips2c_current_process();
 if(!rt || rt->error!=DC_GOAL_OK || process!=dc_arena_jak_player_evidence.target){
  if(rt)rt->error=DC_GOAL_BAD_OBJECT;
  return 0;
 }
 ++dc_arena_jak_player_evidence.read_calls;
 result=((uint64_t (*)(uint64_t))(uintptr_t)original_read)(vector);
 if(rt->error!=DC_GOAL_OK)return result;
 ++dc_arena_jak_player_evidence.read_returns;
 control=word(rt,process+108);
 if(!typed(rt,control,"control-info",928))return result;
 memcpy(&magnitude,dc_goal_resolve(rt,control+908,4),4);
 dc_arena_jak_player_pad_state(0,&sample);
 if(isfinite(magnitude) && magnitude>=0 && magnitude<=1.001f){
  if(sample.joyx && magnitude>0)++dc_arena_jak_player_evidence.moving_reads;
  if(!sample.joyx && magnitude==0)++dc_arena_jak_player_evidence.neutral_reads;
 }
 return result;
}
static void sync_code(uint32_t allocation,uint32_t bytes,void *unused){
 (void)unused;
#ifdef __DREAMCAST__
 dcache_flush_range(allocation,bytes);icache_flush_range(allocation,bytes);
#else
 (void)allocation;(void)bytes;
#endif
}
int dc_arena_jak_player_run(dc_goal_runtime *rt,uint32_t cp){
 typedef uint64_t (*two_fn)(uint64_t,uint64_t);
 typedef uint64_t (*zero_fn)(void);
 uint32_t listener,bytes,target,thread,dram,base,top,start,dispatch,control,cpad,enter;
 uint32_t prev_state=0,prev_resumes,prev_pads;float previous[3]={0,0,0};
 int ok=0,registered=0,installed=0,bound=0;
 unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
 uint32_t address=(uint32_t)(uintptr_t)observe_read;
 dc_goal_code_export fn={"dc-arena-jak-read-pad",code,12,NULL,0};dc_goal_module_result loaded;
 runtime=rt;dc_arena_jak_player_evidence.phase=1;
 dc_goal_art_probe(rt,8);
 CHECK(rt && rt->error==DC_GOAL_OK && !dc_goal_fiber_current() && dc_goal_process_spans_registered(rt)==1,
       12,"jak original spawn retains live process ownership and scope ledgers");
 /* Query every export separately, retaining the first failure. The separate
  * receipt keeps raw addresses even when provenance refuses them. */
 ok=dc_arena_jak_exports(rt,1);
 start=dc_arena_jak_exports_evidence.stages[1].exports[0].value;
 dispatch=dc_arena_jak_exports_evidence.stages[1].exports[1].value;
 original_read=dc_arena_jak_exports_evidence.stages[1].exports[2].value;
 dc_arena_jak_player_evidence.init_entry=dc_arena_jak_exports_evidence.stages[1].exports[3].value;
 dc_arena_jak_player_evidence.read_entry=original_read;
 CHECK(ok && start && dispatch && original_read && dc_arena_jak_player_evidence.init_entry,
       13,"jak start/init-target/dispatcher/read-pad authenticate exact original exports");
 ok=0;
 listener=named(rt,"*listener-process*");bytes=word(rt,listener+80)-(listener-4u);
 CHECK(typed(rt,listener,"process",112) && bytes>=112 && dc_goal_resolve(rt,listener-4,bytes) &&
       dc_goal_note_process_span(rt,listener,bytes,NULL) && dc_goal_bind_process_context(rt,listener,bytes),
       14,"jak original start executes with the original listener caller");
 bound=1;
 target=(uint32_t)((two_fn)(uintptr_t)start)(dc_goal_find(rt,"play"),cp);
 ++dc_arena_jak_evidence.spawn_calls;
 {enum dc_goal_error e=rt->error;dc_goal_clear_process_context(rt);rt->error=e;}bound=0;
 dc_arena_jak_evidence.target_after=target;dc_arena_jak_player_evidence.target=target;
 CHECK(rt->error==DC_GOAL_OK && target==named(rt,"*target*") && typed(rt,target,"target",192),
       15,"jak ORIGINAL start creates and publishes an original typed target");
 thread=word(rt,target+40);bytes=word(rt,target+68)+112u;
 dram=named(rt,"*dram-stack*");base=(dram+12u+15u)&~15u;top=(dram+0x8000u)&~15u;
 dc_arena_jak_player_evidence.thread=thread;
 {/* Diagnostic: each condition of the armed-thread check, in order. */
  dc_goal_process_span existing;
  dc_arena_jak_thread_probe.bytes=bytes;dc_arena_jak_thread_probe.target_length=word(rt,target+68);
  dc_arena_jak_thread_probe.resolved=dc_goal_resolve(rt,target-4,bytes)!=NULL;
  dc_arena_jak_thread_probe.typed=typed(rt,thread,"cpu-thread",124);
  dc_arena_jak_thread_probe.w28=word(rt,thread+28);dc_arena_jak_thread_probe.top=top;
  dc_arena_jak_thread_probe.w36=word(rt,thread+36);dc_arena_jak_thread_probe.w40=word(rt,thread+40);
  dc_arena_jak_thread_probe.w44=word(rt,thread+44);dc_arena_jak_thread_probe.cp=cp;
  dc_arena_jak_thread_probe.w48=word(rt,thread+48);
  uint32_t site=dc_goal_error_site;
  dc_arena_jak_thread_probe.span_found=dc_goal_process_span_lookup(rt,target,&existing);
  dc_arena_jak_thread_probe.span_bytes=dc_arena_jak_thread_probe.span_found?existing.bytes:0;
  {dc_goal_code_entry_info info;const char *n;size_t k;
   dc_arena_jak_thread_probe.w32=word(rt,thread+32);
   if(dc_goal_code_entry(rt,dc_arena_jak_thread_probe.w36,&info) && (n=dc_goal_symbol_name(rt,info.symbol)))
     for(k=0;n[k] && k<31;++k)dc_arena_jak_thread_probe.w36_name[k]=n[k];
   if(dc_goal_code_entry(rt,dc_arena_jak_thread_probe.w32,&info) && (n=dc_goal_symbol_name(rt,info.symbol)))
     for(k=0;n[k] && k<31;++k)dc_arena_jak_thread_probe.w32_name[k]=n[k];}
  dc_arena_jak_thread_probe.error=rt->error;dc_arena_jak_thread_probe.site=dc_goal_error_site;
  rt->error=DC_GOAL_OK;dc_goal_error_site=site;}
 /* set-to-run stores uint64 rregs at port +36. rreg 0 is enter-state (+36, high
  * half +40 is zero). rreg 1 is go's continue point (+44, high half +48 is zero).
  * +28 is the unchanged stack-top. init-target already returned. */
 enter=code_entry(rt,"enter-state");
 CHECK(enter && bytes>=192 && dc_goal_resolve(rt,target-4,bytes) && typed(rt,thread,"cpu-thread",124) &&
       word(rt,thread+36)==enter && word(rt,thread+40)==0 &&
       word(rt,thread+44)==cp && word(rt,thread+48)==0 && word(rt,thread+28)==top &&
       original_state(rt,word(rt,target+72)) &&
       dc_goal_note_process_span(rt,target,bytes,NULL),
       16,"jak original armed thread is enter-state, the continue point and an original state");
 CHECK(dc_goal_fiber_init_main(&target_fiber,rt,target,bytes,thread,base,top-base) &&
       target_fiber.entry==enter &&
       dc_goal_register_thread_fiber(rt,&target_fiber),17,"jak imports original target main at enter-state on its original DRAM working stack");
 registered=1;
 read_symbol=dc_goal_find(rt,"read-pad");memcpy(code+8,&address,4);
 CHECK(dc_goal_load_module(rt,&fn,1,sync_code,NULL,&loaded),18,"jak read-pad call observer links");
 dc_arena_jak_player_evidence.read_shim=named(rt,fn.name);
 CHECK(dc_goal_set_symbol(rt,read_symbol,dc_arena_jak_player_evidence.read_shim),
       19,"jak read-pad observer installed around authenticated original body");
 installed=1;dc_arena_jak_player_evidence.active=1;dc_arena_jak_player_evidence.phase=2;
#ifdef DC_JAK_HEAVY_PROBES
 {void dc_arena_jak_player_pc_start(dc_goal_runtime *);dc_arena_jak_player_pc_start(rt);}
#endif
#ifdef DC_JAK_NO_COMPACT
 /* Diagnostic only: original actors-update compacts *nk-dead-pool* when
  * *compact-actors* is true. */
 {uint32_t s=dc_goal_find(rt,"*compact-actors*");if(s)dc_goal_set_symbol(rt,s,rt->s7);}
#endif
 for(slot=0;slot<DC_JAK_PLAYER_FRAMES;++slot){
  dc_jak_player_frame row={0};dc_goal_pad_state sample;
  prev_resumes=dc_arena_jak_player_evidence.resumes;prev_pads=dc_arena_jak_player_evidence.pad_valid;
  ((zero_fn)(uintptr_t)dispatch)();
  CHECK(rt->error==DC_GOAL_OK && !dc_goal_fiber_current(),20,"jak original dispatcher returns without fault during pad replay");
  CHECK(dc_arena_jak_player_evidence.resumes>prev_resumes &&
        dc_arena_jak_player_evidence.pad_valid>prev_pads && target_fiber.phase==DC_FIBER_SUSPENDED &&
        word(rt,target+32)==dc_goal_find(rt,"suspended"),21,"jak target actually resumed/suspended and original service-cpads consumed recording");
  control=word(rt,target+108);cpad=word(rt,named(rt,"*cpad-list*")+4);
  CHECK(typed(rt,control,"control-info",928) && word(rt,control+664)==cpad,
        22,"jak ORIGINAL init-target created control-info linked to cpad zero");
  dc_arena_jak_player_evidence.control=control;dc_arena_jak_player_evidence.cpad=cpad;
  row.sequence=slot+1;row.state=word(rt,target+52);row.status=word(rt,target+32);
  row.resumes=dc_arena_jak_player_evidence.resumes;row.read_calls=dc_arena_jak_player_evidence.read_returns;
  row.pad_services=dc_arena_jak_player_evidence.pad_valid;
  dc_arena_jak_player_pad_state(0,&sample);row.joyx=sample.joyx;row.buttons=sample.buttons;
  memcpy(row.trans,dc_goal_resolve(rt,control+12,12),12);
  memcpy(&row.magnitude,dc_goal_resolve(rt,control+908,4),4);
  CHECK(row.state && row.state!=rt->s7 && row.state!=named(rt,"process-drawable-art-error") &&
        isfinite(row.trans[0]) && isfinite(row.trans[1]) && isfinite(row.trans[2]) && isfinite(row.magnitude),
        23,"jak replay records real finite control output outside original art-error state");
  if(prev_state && row.state!=prev_state)++dc_arena_jak_player_evidence.state_changes;
  if(slot){float dx=row.trans[0]-previous[0],dz=row.trans[2]-previous[2];
   if(sample.joyx && dx*dx+dz*dz>16.f)++dc_arena_jak_player_evidence.moving_changes;
   if(slot>=144 && dx*dx+dz*dz<=16.f)++dc_arena_jak_player_evidence.neutral_stable;
  }
  prev_state=row.state;memcpy(previous,row.trans,12);
  dc_arena_jak_evidence.target_state=row.state;
  dc_arena_jak_player_evidence.trace[slot]=row;++dc_arena_jak_player_evidence.frames;
 }
 CHECK(dc_arena_jak_player_evidence.read_returns==dc_arena_jak_player_evidence.read_calls &&
       dc_arena_jak_player_evidence.moving_reads>=32 && dc_arena_jak_player_evidence.neutral_reads>=32,
       24,"jak ORIGINAL read-pad executes for the target and distinguishes neutral from moving hardware");
 CHECK(dc_arena_jak_player_evidence.moving_changes>=16 && dc_arena_jak_player_evidence.neutral_stable>=12,
       25,"jak original control position responds to pad and settles with neutral input");
 dc_arena_jak_evidence.pad_position_updates=dc_arena_jak_player_evidence.moving_changes;
 dc_arena_jak_player_evidence.phase=3;ok=1;
done:
 dc_arena_jak_player_evidence.active=0;
 if(rt && rt->temporary<=rt->capacity && rt->used<=rt->capacity-rt->temporary){
  uint32_t available=rt->capacity-rt->temporary-rt->used;
  dc_arena_jak_player_evidence.sidekick_available=available;
  dc_arena_jak_player_evidence.sidekick_shortfall=dc_arena_jak_player_evidence.sidekick_loaded?0:
    (DC_JAK_SIDEKICK_BYTES>available?DC_JAK_SIDEKICK_BYTES-available:0);
 }
 /* Cleanup must preserve the first original fault/site. Suspended target owns
  * live native metadata until root teardown; never clear an executing fiber. */
 if(rt){enum dc_goal_error error=rt->error;
  if(bound)dc_goal_clear_process_context(rt);
  if(installed){if(dc_goal_set_symbol(rt,read_symbol,original_read))dc_arena_jak_player_evidence.restored=1;}
  if(registered)dc_goal_unregister_thread_fiber(rt,&target_fiber);
  rt->error=error;
 }
 if(ok)ok=check(rt,dc_arena_jak_player_evidence.restored,26,"jak original read-pad binding restored after replay");
 return ok;
}
void dc_arena_jak_player_report(char *out,size_t bytes){
 snprintf(out,bytes,"JAK frames=%lu read=%lu move=%lu sidekick-short=%lu",
  (unsigned long)dc_arena_jak_player_evidence.frames,(unsigned long)dc_arena_jak_player_evidence.read_returns,
  (unsigned long)dc_arena_jak_player_evidence.moving_changes,(unsigned long)dc_arena_jak_player_evidence.sidekick_shortfall);
}
#undef CHECK
#endif
