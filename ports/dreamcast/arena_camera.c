/* Observe original camera processes and original update-camera (AI-assisted), ISC.
 * One diagnostic trans.w sentinel proves a store even with neutral input.
 * No pose script, duplicate spawn, synthetic pad or renderer readback here. */
#ifdef DC_ARENA_CAMERA
#include "arena_camera.h"
#include "arena_game.h" /* Layout constants only; arena_game.o is not linked. */
#include "goal_fiber.h"
#include <stdio.h>
#include <string.h>

volatile dc_arena_camera_metrics_type dc_arena_camera_metrics={.version=1,.producer=1};
typedef char camera_metrics_size[sizeof(dc_arena_camera_metrics_type)==172?1:-1];
static dc_goal_runtime *runtime;
static uint32_t original_update,update_symbol,shim;
static uint32_t master_type,slave_type,combiner_type,math_type;
static uint32_t master_sym,combiner_sym,math_sym,bank_sym,pads_sym,display_sym,dproc_sym;
static uint32_t kernel_sym,analog_sym,buttons_sym,mode_sym,free_state,suspended,external_sym;
static unsigned char previous[80];
static int have_previous;
/* reason: 1 entry/install, 2 schema, 3 update fault, 4 no write,
 * 5 process path, 6 no serviced controller, 7 frame/root fault. */
static int require(int ok,uint32_t reason,const char *why){
  if(!ok && !dc_arena_camera_metrics.reason)dc_arena_camera_metrics.reason=reason;
  return dc_arena_camera_check(ok,1,why);
}
#define REQUIRE(ok,reason,why) do { if(!require((ok),(reason),(why)))return 0; } while(0)
/* Main arena only, allocated bytes, checked before adding field offsets.
 * Observing a fault must not clear it through a runtime lookup. */
static unsigned char *span(dc_goal_runtime *rt,uint32_t p,uint32_t n){
  uint32_t off;
  if(!rt || !rt->memory || rt->used>rt->capacity || rt->base>UINT32_MAX-rt->capacity || p<rt->base)return NULL;
  off=p-rt->base;
  return off<=rt->used && n<=rt->used-off?rt->memory+off:NULL;
}
static uint32_t word(dc_goal_runtime *rt,uint32_t p){
  uint32_t v=0;const void *b=span(rt,p,4);if(b)memcpy(&v,b,4);return v;
}
static uint32_t value(dc_goal_runtime *rt,uint32_t s){return s?word(rt,s):0;}
static uint32_t symbol(dc_goal_runtime *rt,const char *s){return dc_goal_find(rt,s);}
static uint32_t named(dc_goal_runtime *rt,const char *s){return dc_goal_symbol_value(rt,symbol(rt,s));}
static unsigned char *object(dc_goal_runtime *rt,uint32_t p,uint32_t type,uint32_t bytes){
  unsigned char *b;
  if(p<4 || (p&15u)!=4 || !type || !(b=span(rt,p-4,bytes)) || word(rt,p-4)!=type)return NULL;
  return b+4;
}
static unsigned char *math_camera(dc_goal_runtime *rt){
  uint32_t p=value(rt,math_sym);
  if(!object(rt,math_type,rt->type_type,20) || word(rt,math_type+8)%65536u!=DC_GAME_CAMERA_BYTES)return NULL;
  return object(rt,p,math_type,DC_GAME_CAMERA_BYTES);
}
static int finite_word(uint32_t bits){return (bits&0x7fffffffu)<0x7f800000u;}
static int snapshot(dc_goal_runtime *rt,volatile dc_arena_camera_process *p,uint32_t process,uint32_t type){
  if(!object(rt,process,type,112))return 0;
  p->process=process;p->status=word(rt,process+32);p->state=word(rt,process+52);p->mask=word(rt,process+4);
  return 1;
}
static uint64_t observe_update(void){
  dc_goal_runtime *rt=runtime;dc_goal_fiber *f=dc_goal_fiber_current();
  unsigned char *b;uint32_t w,expect;uint64_t result;
  if(!rt || rt->error!=DC_GOAL_OK || !f || f->runtime!=rt || !f->process ||
     f->process!=value(rt,dproc_sym) || !(b=math_camera(rt))){
    require(0,2,"camera update lacks live display fiber or math-camera schema");
    if(rt)rt->error=DC_GOAL_BAD_OBJECT;
    return 0;
  }
  ++dc_arena_camera_metrics.update_calls;
  dc_arena_camera_metrics.camera=value(rt,math_sym);
  /* Probe only the ordinary combiner branch. Never count fallback pad code as
   * evidence for the master/slave/combiner route. No writes to their poses. */
  if(!dc_arena_camera_metrics.diagnostic_seeds){
    uint32_t comb=value(rt,combiner_sym);
    if(!object(rt,comb,combiner_type,112) || value(rt,external_sym)!=rt->s7){
      require(0,5,"camera combiner absent or external camera mode enabled");
      rt->error=DC_GOAL_BAD_OBJECT;return 0;
    }
    w=0xc640e400u; /* -12345.0, diagnostic only, never a render pose */
    memcpy(b+DC_GAME_CAMERA_TRANS+12,&w,4);
    ++dc_arena_camera_metrics.diagnostic_seeds;
  }
  result=((uint64_t (*)(void))(uintptr_t)original_update)();
  if(rt->error!=DC_GOAL_OK){
    dc_arena_camera_metrics.runtime_error=rt->error;dc_arena_camera_metrics.error_site=dc_goal_error_site;
    require(0,3,"original update-camera returned a runtime fault");return result;
  }
  ++dc_arena_camera_metrics.update_returns;
  b=math_camera(rt);
  if(!b || value(rt,math_sym)!=dc_arena_camera_metrics.camera){
    require(0,2,"original update-camera changed camera identity/schema");rt->error=DC_GOAL_BAD_OBJECT;return result;
  }
  memcpy(&w,b+DC_GAME_CAMERA_TRANS+12,4);expect=0x3f800000u;
  /* Only the first call has the sentinel witness. Do not infer stores from
   * unchanged later bytes or count our diagnostic write as original output. */
  if(dc_arena_camera_metrics.update_calls==1)
    dc_arena_camera_metrics.original_wrote_camera=(w==expect);
  if(!require(w==expect && dc_arena_camera_metrics.original_wrote_camera,4,
              "original update-camera did not produce trans.w=1 after diagnostic")){
    rt->error=DC_GOAL_BAD_OBJECT;return result;
  }
  /* Diagnostic pose changes are separate from the one-time write witness. */
  {unsigned char pose[80];unsigned i;
   memcpy(pose,b+DC_GAME_CAMERA_TRANS,16);memcpy(pose+16,b+DC_GAME_CAMERA_INV,64);
   for(i=0;i<80;i+=4){memcpy(&w,pose+i,4);if(!finite_word(w)){
     require(0,2,"original camera output contains nonfinite lanes");rt->error=DC_GOAL_BAD_OBJECT;return result;}}
   if(have_previous && memcmp(previous,pose,80))++dc_arena_camera_metrics.pose_changes;
   memcpy(previous,pose,80);have_previous=1;}
  return result;
}
int dc_arena_camera_begin(dc_goal_runtime *rt,dc_goal_cache_sync sync){
  dc_goal_code_entry_info info;dc_goal_module_result loaded;
  unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  uint32_t target=(uint32_t)(uintptr_t)observe_update;
  dc_goal_code_export fn={"dc-arena-camera-update",code,12,NULL,0};
  REQUIRE(rt && rt->error==DC_GOAL_OK && !dc_goal_fiber_current(),7,"camera probe installs on fault-free root stack");
  runtime=rt;
  update_symbol=symbol(rt,"update-camera");original_update=value(rt,update_symbol);
  REQUIRE(dc_goal_code_entry(rt,original_update,&info) && info.symbol==update_symbol,1,
          "camera update-camera has an exact loader code export");
  REQUIRE(dc_goal_code_entry(rt,named(rt,"cam-free-floating-move"),&info),1,
          "camera original free-floating code export exists");
  master_type=named(rt,"camera-master");slave_type=named(rt,"camera-slave");
  combiner_type=named(rt,"camera-combiner");math_type=named(rt,"math-camera");
  master_sym=symbol(rt,"*camera*");combiner_sym=symbol(rt,"*camera-combiner*");math_sym=symbol(rt,"*math-camera*");
  bank_sym=symbol(rt,"*CAMERA-bank*");pads_sym=symbol(rt,"*cpad-list*");display_sym=symbol(rt,"*display*");
  dproc_sym=symbol(rt,"*dproc*");
  kernel_sym=symbol(rt,"*kernel-context*");analog_sym=symbol(rt,"*camera-read-analog*");
  buttons_sym=symbol(rt,"*camera-read-buttons*");mode_sym=symbol(rt,"*master-mode*");
  external_sym=symbol(rt,"*external-cam-mode*");free_state=named(rt,"cam-free-floating");suspended=symbol(rt,"suspended");
  REQUIRE(master_type && slave_type && combiner_type && math_type && master_sym && combiner_sym && math_sym &&
          bank_sym && pads_sym && display_sym && dproc_sym && kernel_sym && analog_sym && buttons_sym && mode_sym &&
          external_sym && free_state && suspended && math_camera(rt),2,"camera probe symbols and math-camera layout exist");
  memcpy(code+8,&target,4);
  REQUIRE(dc_goal_load_module(rt,&fn,1,sync,NULL,&loaded),1,"camera observer trampoline links");
  shim=named(rt,"dc-arena-camera-update");
  REQUIRE(shim && dc_goal_set_symbol(rt,update_symbol,shim),1,"camera observer wraps original update-camera");
  return 1;
}
void dc_arena_camera_resume(dc_goal_runtime *rt,uint32_t process,int completed){
  volatile dc_arena_camera_process *p;uint32_t type;
  if(rt!=runtime || process<4)return;
  type=word(rt,process-4);
  if(type==master_type)p=&dc_arena_camera_metrics.master;
  else if(type==slave_type)p=&dc_arena_camera_metrics.slave;
  else if(type==combiner_type)p=&dc_arena_camera_metrics.combiner;
  else return;
  if(!snapshot(rt,p,process,type))return;
  if(!completed)++p->attempts;
  else {++p->resumed;if(type==slave_type && p->state==free_state && p->status==suspended)++p->free_frames;}
}
void dc_arena_camera_pad(dc_goal_runtime *rt,uint32_t pad){
  unsigned char *b;
  if(rt!=runtime || !(b=span(rt,pad,132)))return;
  ++dc_arena_camera_metrics.pad_services;
  if(!(b[0]&128) && word(rt,pad+32)==word(rt,value(rt,bank_sym)+4))
    ++dc_arena_camera_metrics.valid_services;
}
int dc_arena_camera_frame(dc_goal_runtime *rt){
  uint32_t bank=value(rt,bank_sym),pads=value(rt,pads_sym),display=value(rt,display_sym),pad,cpad;
  dc_arena_camera_metrics.runtime_error=rt->error;dc_arena_camera_metrics.error_site=dc_goal_error_site;
  REQUIRE(rt==runtime && rt->error==DC_GOAL_OK && !dc_goal_fiber_current(),7,
          "camera sample runs after dispatcher on restored root stack");
  ++dc_arena_camera_metrics.dispatches;
  if(!span(rt,bank,8) || !span(rt,pads,20) || !span(rt,display,920)){
    REQUIRE(0,2,"camera pad/bank/display diagnostic span missing");
  }
  pad=word(rt,bank+4);dc_arena_camera_metrics.selected_pad=pad;
  REQUIRE(pad<word(rt,pads) && pad<4,2,"camera selected controller is in cpad-list");
  cpad=word(rt,pads+4+4*pad);
  REQUIRE(span(rt,cpad,132)!=NULL,2,"camera selected cpad-info diagnostic span exists");
  dc_arena_camera_metrics.pad_valid=*span(rt,cpad,1);
  dc_arena_camera_metrics.prevent_mask=word(rt,value(rt,kernel_sym));
  dc_arena_camera_metrics.read_analog=value(rt,analog_sym);
  dc_arena_camera_metrics.read_buttons=value(rt,buttons_sym);
  dc_arena_camera_metrics.master_mode=value(rt,mode_sym);
  dc_arena_camera_metrics.time_ratio_bits=word(rt,display+916);
  /* Capture skipped/waiting processes as well as ones reaching resume-hook. */
  {uint32_t master=value(rt,master_sym);
   snapshot(rt,&dc_arena_camera_metrics.master,master,master_type);
   snapshot(rt,&dc_arena_camera_metrics.combiner,value(rt,combiner_sym),combiner_type);
   if(object(rt,master,master_type,128)){
     uint32_t slave=word(rt,word(rt,master+116));
     dc_arena_camera_metrics.registered_slave=slave;
     snapshot(rt,&dc_arena_camera_metrics.slave,slave,slave_type);
   }}
  return 1;
}
int dc_arena_camera_finish(dc_goal_runtime *rt){
  uint32_t master=value(rt,master_sym),comb=value(rt,combiner_sym),handle;
  REQUIRE(object(rt,master,master_type,128) && object(rt,comb,combiner_type,112),5,
          "camera original startup produced master and combiner");
  handle=word(rt,master+116);dc_arena_camera_metrics.registered_slave=word(rt,handle);
  REQUIRE(word(rt,master+112)==1 && dc_arena_camera_metrics.registered_slave==dc_arena_camera_metrics.slave.process &&
          object(rt,dc_arena_camera_metrics.registered_slave,slave_type,112),5,
          "camera master registered exactly one original slave");
  REQUIRE(dc_arena_camera_metrics.master.process==master && dc_arena_camera_metrics.combiner.process==comb &&
          dc_arena_camera_metrics.master.resumed && dc_arena_camera_metrics.slave.resumed && dc_arena_camera_metrics.combiner.resumed,5,
          "camera master slave and combiner actually resumed");
  REQUIRE(dc_arena_camera_metrics.slave.free_frames && dc_arena_camera_metrics.slave.state==free_state,5,
          "camera original slave entered and suspended in cam-free-floating");
  REQUIRE(dc_arena_camera_metrics.pad_services && dc_arena_camera_metrics.valid_services &&
          !(dc_arena_camera_metrics.pad_valid&128),6,"camera needs a serviced valid Maple controller");
  REQUIRE(dc_arena_camera_metrics.read_analog==rt->s7+DC_FIX_SYM_TRUE &&
          dc_arena_camera_metrics.read_buttons==rt->s7+DC_FIX_SYM_TRUE &&
          dc_arena_camera_metrics.master_mode==symbol(rt,"game"),5,"camera read flags enabled in game mode");
  REQUIRE(finite_word(dc_arena_camera_metrics.time_ratio_bits) && dc_arena_camera_metrics.time_ratio_bits>0 &&
          dc_arena_camera_metrics.time_ratio_bits<0x80000000u,5,"camera display time ratio is finite and positive");
  REQUIRE(dc_arena_camera_metrics.update_calls==61 && dc_arena_camera_metrics.update_returns==61,3,
          "camera original update-camera completed all 61 display dispatches");
  REQUIRE(dc_arena_camera_metrics.diagnostic_seeds==1 && dc_arena_camera_metrics.original_wrote_camera==1,4,
          "camera original output replaced the single diagnostic sentinel");
  REQUIRE(dc_arena_camera_metrics.dispatches==64,7,"camera observed all 64 original dispatcher returns");
  REQUIRE(dc_goal_set_symbol(rt,update_symbol,original_update),1,"camera observer restores original update-camera binding");
  return 1;
}
void dc_arena_camera_report(char *out,size_t bytes,uint32_t used,uint32_t capacity){
  snprintf(out,bytes,"BOOT original-camera wrote=%lu updates=%lu arena=%lu/%lu",
    (unsigned long)dc_arena_camera_metrics.original_wrote_camera,(unsigned long)dc_arena_camera_metrics.update_returns,
    (unsigned long)used,(unsigned long)capacity);
}
#undef REQUIRE
#endif
