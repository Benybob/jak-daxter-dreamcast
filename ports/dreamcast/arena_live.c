/* Original camera drives rendering; guarded separate mode (AI-assisted), ISC. */
#ifdef DC_ARENA_LIVE
#include "arena_live.h"
#include "goal_bridge.h"
#include "arena_game.h" /* Layout constants only; arena_game.o is not linked. */
#include "goal_fiber.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#ifdef DC_ARENA_JAK
#include "arena_jak.h"
#endif

volatile dc_live_evidence dc_arena_live_evidence={.version=3,.producer=2};
typedef char camera_metrics_size[sizeof(dc_live_evidence)==39476?1:-1];
static dc_goal_runtime *runtime;
static uint32_t original_update,update_symbol,shim;
static uint32_t master_type,slave_type,combiner_type,math_type;
static uint32_t master_sym,combiner_sym,math_sym,bank_sym,pads_sym,display_sym,dproc_sym;
static uint32_t kernel_sym,analog_sym,buttons_sym,mode_sym,free_state,suspended,external_sym;
static int in_update;
static uint32_t init_block,init_transform,event_entry,service_entry,game_mode,progress_sym,look_sym;
static uint32_t levels_sym,levels_type,level_type,inactive;
static uint32_t layout_sym;
/* reason: 1 entry/install, 2 schema, 3 update fault, 4 no write,
 * 5 process path, 6 recorded-pad service, 7 frame/root fault,
 * 8 freshness/initialization/motion/draw contract. */
static int require(int ok,uint32_t reason,const char *why){
  if(!ok && !dc_arena_live_evidence.reason)dc_arena_live_evidence.reason=reason;
  return dc_arena_live_check(ok,1,why);
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
/* The saved exact loader export and its callees must replace all 36 poisoned
 * output lanes in this dynamic call. This is a store witness, not instruction
 * tracing and not proof of correct motion: affine/motion/render checks follow. */
static int ordinary_mode(dc_goal_runtime *rt){
 uint32_t display=value(rt,display_sym),ratio,levels=value(rt,levels_sym);unsigned i;
 if(!span(rt,display,920))return 0;
 /* update-camera reads the previous trans before overwriting it for every
  * non-inactive level. Refuse that unsupported path BEFORE diagnostic poison.
  * Inline BASIC fields add their own tag: tagged level0 = tagged group+96,
  * not +92. Retail size/offset assertions are in all-types.gc. */
 if(!object(rt,levels,levels_type,0x1ef4)||word(rt,levels)!=2)return 0;
 for(i=0;i<2;++i){uint32_t level=levels+96+i*2608;
  if(!object(rt,level,level_type,2608)||word(rt,level+16)!=inactive)return 0;
 }
 ratio=word(rt,display+916);
 return value(rt,update_symbol)==shim && finite_word(ratio) && ratio>=0x3f800000u && ratio<=0x4099999au &&
        value(rt,layout_sym)==rt->s7 &&
        value(rt,mode_sym)==game_mode && value(rt,external_sym)==rt->s7 &&
        value(rt,progress_sym)==rt->s7 && value(rt,look_sym)==0 &&
        value(rt,analog_sym)==rt->s7+DC_FIX_SYM_TRUE && value(rt,buttons_sym)==rt->s7+DC_FIX_SYM_TRUE &&
        object(rt,value(rt,combiner_sym),combiner_type,112)!=NULL;
}
static uint64_t observe_update(void){
 dc_goal_runtime *rt=runtime;dc_goal_fiber *f=dc_goal_fiber_current();
 dc_live_pose pose;dc_game_camera converted;dc_live_trace row;uint64_t result;uint32_t seq,camera;
 if(!rt || rt->error!=DC_GOAL_OK || !f || f->runtime!=rt || !f->process ||
    f->process!=value(rt,dproc_sym) || !math_camera(rt) || in_update){
  require(0,2,"live original update requires nonrecursive display fiber and camera schema");
  if(rt)rt->error=DC_GOAL_BAD_OBJECT;
  return 0;
 }
 seq=++dc_arena_live_evidence.update_calls;camera=value(rt,math_sym);
 if(!require(seq<=DC_LIVE_UPDATES && dc_arena_live_evidence.dispatches==seq+3 &&
             dc_arena_live_evidence.init_events==1 && ordinary_mode(rt),5,
             "live original update has exact schedule and ordinary game camera branch"))goto fault;
 if(!require(dc_arena_live_evidence.valid_services>dc_arena_live_evidence.selected_services_before_update &&
             dc_arena_live_evidence.pad_stage==dc_live_input_stage(seq-1),6,
             "live update has fresh selected recorded cpad service for this sequence"))goto fault;
 dc_arena_live_evidence.selected_services_before_update=dc_arena_live_evidence.valid_services;
 dc_arena_live_evidence.camera=camera;
 if(!dc_live_poison(rt,camera,math_type,seq)){require(0,2,"live diagnostic poison destination schema");goto fault;}
 ++dc_arena_live_evidence.poison_batches;dc_arena_live_evidence.poisoned_lanes+=36;
 in_update=1;result=((uint64_t (*)(void))(uintptr_t)original_update)();in_update=0;
 if(rt->error!=DC_GOAL_OK){
  dc_arena_live_evidence.runtime_error=rt->error;dc_arena_live_evidence.error_site=dc_goal_error_site;
  require(0,3,"live original update-camera returned a runtime fault");return result;
 }
 ++dc_arena_live_evidence.update_returns;
 if(!require(value(rt,math_sym)==camera && ordinary_mode(rt) &&
             dc_live_read(rt,camera,math_type,&pose) && dc_live_convert(&pose,55,&converted),4,
             "live original call replaces EVERY poisoned lane with a valid affine camera"))goto fault;
 ++dc_arena_live_evidence.witnessed_updates;
 memset(&row,0,sizeof row);row.sequence=seq;row.dispatch=seq+4;
 row.stage=dc_arena_live_evidence.pad_stage;row.pad_services=dc_arena_live_evidence.valid_services;
 row.master_resumed=dc_arena_live_evidence.master.resumed;row.slave_resumed=dc_arena_live_evidence.slave.resumed;
 row.combiner_resumed=dc_arena_live_evidence.combiner.resumed;row.pose=pose;
 {uint32_t bits=word(rt,value(rt,display_sym)+916);memcpy(&row.time_adjust_ratio,&bits,4);}
 if(seq>1){
  dc_live_pose previous=dc_arena_live_evidence.trace[seq-2].pose;
  row.delta=dc_live_delta(&previous,&pose);
  if(row.delta&2)++dc_arena_live_evidence.position_changes;
  if(row.delta&4)++dc_arena_live_evidence.orientation_changes;
  if(row.delta!=1)++dc_arena_live_evidence.pose_changes;
 }
 dc_arena_live_evidence.trace[seq-1]=row;
 dc_live_input_advance(seq);return result;
fault:
 rt->error=DC_GOAL_BAD_OBJECT;return 0;
}
static int initialize_layout_default(dc_goal_runtime *rt){
#ifdef DC_ARENA_JAK
  /* This target must obtain data from the ORIGINAL debug initializer. No C
   * fallback: removing that startup must fail before recording motion. */
  layout_sym=symbol(rt,"*cam-layout*");
  REQUIRE(dc_arena_jak_evidence.debug_calls==1 && dc_arena_jak_evidence.debug_restored,5,
          "jak camera requires original cam-debug-h data initializer");
  REQUIRE(layout_sym && value(rt,layout_sym)==rt->s7,5,
          "jak camera layout false came from original define-perm");
#else
  /* cam-debug-h.gc:73/150 puts this default in a skipped debug initializer.
   * Numeric zero is truthy in GOAL and blocks digital free-camera rotation.
   * Supply only the absent default; never disable an active layout mode. */
  layout_sym=symbol(rt,"*cam-layout*");
  REQUIRE(layout_sym && (value(rt,layout_sym)==0 || value(rt,layout_sym)==rt->s7),5,
          "live camera layout is inactive or has its skipped debug default");
  REQUIRE(dc_goal_set_symbol(rt,layout_sym,rt->s7),5,"live initializes skipped camera layout false default once");
#endif
  return 1;
}
int dc_arena_live_begin(dc_goal_runtime *rt,dc_goal_cache_sync sync){
  dc_goal_code_entry_info info;dc_goal_module_result loaded;
  unsigned char code[12]={1,0xd0,0x2b,0x40,9,0,9,0,0,0,0,0};
  uint32_t target=(uint32_t)(uintptr_t)observe_update;
  dc_goal_code_export fn={"dc-arena-live-update",code,12,NULL,0};
  REQUIRE(rt && rt->error==DC_GOAL_OK && !dc_goal_fiber_current(),7,"camera probe installs on fault-free root stack");
  runtime=rt;dc_live_input_advance(0);
  update_symbol=symbol(rt,"update-camera");original_update=value(rt,update_symbol);
  REQUIRE(dc_goal_code_entry(rt,original_update,&info) && info.symbol==update_symbol,1,
          "camera update-camera has an exact loader code export");
  REQUIRE(dc_goal_code_entry(rt,named(rt,"cam-free-floating-move"),&info) &&
          info.symbol==symbol(rt,"cam-free-floating-move"),1,
          "camera original free-floating code export exists");
  master_type=named(rt,"camera-master");slave_type=named(rt,"camera-slave");
  combiner_type=named(rt,"camera-combiner");math_type=named(rt,"math-camera");
  master_sym=symbol(rt,"*camera*");combiner_sym=symbol(rt,"*camera-combiner*");math_sym=symbol(rt,"*math-camera*");
  bank_sym=symbol(rt,"*CAMERA-bank*");pads_sym=symbol(rt,"*cpad-list*");display_sym=symbol(rt,"*display*");
  dproc_sym=symbol(rt,"*dproc*");
  kernel_sym=symbol(rt,"*kernel-context*");analog_sym=symbol(rt,"*camera-read-analog*");
  buttons_sym=symbol(rt,"*camera-read-buttons*");mode_sym=symbol(rt,"*master-mode*");
  game_mode=symbol(rt,"game");progress_sym=symbol(rt,"*progress-process*");look_sym=symbol(rt,"*camera-look-through-other*");
  levels_sym=symbol(rt,"*level*");levels_type=named(rt,"level-group");level_type=named(rt,"level");inactive=symbol(rt,"inactive");
  external_sym=symbol(rt,"*external-cam-mode*");free_state=named(rt,"cam-free-floating");suspended=symbol(rt,"suspended");
  REQUIRE(master_type && slave_type && combiner_type && math_type && master_sym && combiner_sym && math_sym &&
          bank_sym && pads_sym && display_sym && dproc_sym && kernel_sym && analog_sym && buttons_sym && mode_sym &&
          external_sym && game_mode && progress_sym && look_sym && levels_sym && levels_type && level_type && inactive &&
          free_state && suspended && math_camera(rt),2,"camera probe symbols and math-camera layout exist");
  if(!initialize_layout_default(rt))return 0;
  memcpy(code+8,&target,4);
  REQUIRE(dc_goal_load_module(rt,&fn,1,sync,NULL,&loaded),1,"camera observer trampoline links");
  shim=named(rt,"dc-arena-live-update");
  REQUIRE(shim && dc_goal_set_symbol(rt,update_symbol,shim),1,"camera observer wraps original update-camera");
  dc_arena_live_evidence.original_update=original_update;dc_arena_live_evidence.shim=shim;
  event_entry=named(rt,"send-event-function");
  REQUIRE(dc_goal_code_entry(rt,event_entry,&info) && info.symbol==symbol(rt,"send-event-function"),1,
          "live initialization event uses exact original send-event-function export");
  service_entry=named(rt,"service-cpads");
  REQUIRE(dc_goal_code_entry(rt,service_entry,&info) && info.symbol==symbol(rt,"service-cpads"),1,
          "live pad warmup uses exact original service-cpads export");
  init_block=dc_goal_alloc(rt,80);init_transform=dc_goal_alloc(rt,32);
  REQUIRE(init_block && init_transform,2,"live one-time event argument storage allocated before drawing");
  return 1;
}
int dc_arena_live_initialize(dc_goal_runtime *rt){
 uint32_t listener,bytes,master,message;float transform[8];
 unsigned char *block;uint64_t argument;
 REQUIRE(rt==runtime && rt->error==DC_GOAL_OK && !dc_goal_fiber_current() &&
         dc_arena_live_evidence.dispatches==3 && !dc_arena_live_evidence.init_events,7,
         "live one-time event runs on root after three initialization dispatches");
 listener=named(rt,"*listener-process*");master=value(rt,master_sym);message=symbol(rt,"teleport-to-transformq");
 REQUIRE(object(rt,master,master_type,128) && word(rt,master+112)==1 &&
         dc_arena_live_evidence.master.resumed && dc_arena_live_evidence.slave.free_frames,5,
         "live event requires original master and one initialized free-floating slave");
 REQUIRE(object(rt,listener,named(rt,"process"),112) && word(rt,listener+80)>listener,2,
         "live event caller is original boot listener with bounded heap");
 bytes=word(rt,listener+80)-(listener-4u);
 REQUIRE(message && dc_arena_live_initial_transform(transform) && span(rt,init_block,80) && span(rt,init_transform,32),2,
         "live one-time event transform and argument spans validate before calling original code");
 REQUIRE(span(rt,listener-4,bytes) && dc_goal_note_process_span(rt,listener,bytes,NULL) &&
         dc_goal_bind_process_context(rt,listener,bytes),7,"live event binds original listener context");
 block=span(rt,init_block,80);
 memcpy(span(rt,init_transform,32),transform,32);memset(block,0,80);
 memcpy(block+4,&listener,4);{uint32_t n=1;
  memcpy(block+8,&n,4);memcpy(block+12,&message,4);}
 argument=init_transform;memcpy(block+16,&argument,8);
 ++dc_arena_live_evidence.init_events;
 ((uint64_t (*)(uint64_t,uint64_t))(uintptr_t)event_entry)(master,init_block);
 /* Complete the unchanged DualShock handshake through ORIGINAL services.
  * Root initialization only; all display frames subsequently service normally. */
 {unsigned i;for(i=0;i<3 && rt->error==DC_GOAL_OK;++i)((void (*)(void))(uintptr_t)service_entry)();}
 {enum dc_goal_error error=rt->error;dc_goal_clear_process_context(rt);rt->error=error;}
 REQUIRE(rt->error==DC_GOAL_OK && !dc_goal_fiber_current() && dc_arena_live_evidence.valid_services,5,
         "live original teleport and pad handshake return without runtime fault");
 return 1;
}
void dc_arena_live_resume(dc_goal_runtime *rt,uint32_t process,int completed){
  volatile dc_arena_camera_process *p;uint32_t type;
  if(rt!=runtime || process<4)return;
  type=word(rt,process-4);
  if(type==master_type)p=&dc_arena_live_evidence.master;
  else if(type==slave_type)p=&dc_arena_live_evidence.slave;
  else if(type==combiner_type)p=&dc_arena_live_evidence.combiner;
  else return;
  if(!snapshot(rt,p,process,type))return;
  if(!completed)++p->attempts;
  else {++p->resumed;if(type==slave_type && p->state==free_state && p->status==suspended)++p->free_frames;}
}
void dc_arena_live_pad(dc_goal_runtime *rt,uint32_t pad){
  unsigned char *b;
  if(rt!=runtime || !(b=span(rt,pad,132)))return;
  ++dc_arena_live_evidence.pad_services;
  if(!(b[0]&128) && word(rt,pad+32)==word(rt,value(rt,bank_sym)+4)){
    uint8_t expected[132]={0};dc_goal_pad_state state;
    uint32_t completed=dc_arena_live_evidence.update_returns;
    if(word(rt,pad+32)!=0 || !dc_live_recorded_pad(completed,0,&state))goto bad;
    dc_goal_pad_fill(expected,&state);
    /* Compare actual post-service status, button bits, all stick bytes and
     * analog pressure lanes. The stage label alone is never evidence of input. */
    if(b[0]!=6 || memcmp(b+1,expected+1,19))goto bad;
    ++dc_arena_live_evidence.valid_services;
    dc_arena_live_evidence.pad_stage=dc_live_input_stage(completed);
  }
  return;
bad:
  require(0,6,"live selected cpad bytes differ from the recorded hardware sample");rt->error=DC_GOAL_BAD_OBJECT;
}
int dc_arena_live_frame(dc_goal_runtime *rt){
  uint32_t bank=value(rt,bank_sym),pads=value(rt,pads_sym),display=value(rt,display_sym),pad,cpad;
  dc_arena_live_evidence.runtime_error=rt->error;dc_arena_live_evidence.error_site=dc_goal_error_site;
  REQUIRE(rt==runtime && rt->error==DC_GOAL_OK && !dc_goal_fiber_current(),7,
          "camera sample runs after dispatcher on restored root stack");
  ++dc_arena_live_evidence.dispatches;
  if(!span(rt,bank,8) || !span(rt,pads,20) || !span(rt,display,920)){
    REQUIRE(0,2,"camera pad/bank/display diagnostic span missing");
  }
  pad=word(rt,bank+4);dc_arena_live_evidence.selected_pad=pad;
  REQUIRE(pad<word(rt,pads) && pad<4,2,"camera selected controller is in cpad-list");
  cpad=word(rt,pads+4+4*pad);
  REQUIRE(span(rt,cpad,132)!=NULL,2,"camera selected cpad-info diagnostic span exists");
  dc_arena_live_evidence.pad_valid=*span(rt,cpad,1);
  dc_arena_live_evidence.prevent_mask=word(rt,value(rt,kernel_sym));
  dc_arena_live_evidence.read_analog=value(rt,analog_sym);
  dc_arena_live_evidence.read_buttons=value(rt,buttons_sym);
  dc_arena_live_evidence.master_mode=value(rt,mode_sym);
  dc_arena_live_evidence.time_ratio_bits=word(rt,display+916);
  /* Capture skipped/waiting processes as well as ones reaching resume-hook. */
  {uint32_t master=value(rt,master_sym);
   snapshot(rt,&dc_arena_live_evidence.master,master,master_type);
   snapshot(rt,&dc_arena_live_evidence.combiner,value(rt,combiner_sym),combiner_type);
   if(object(rt,master,master_type,128)){
     uint32_t slave=word(rt,word(rt,master+116));
     dc_arena_live_evidence.registered_slave=slave;
     snapshot(rt,&dc_arena_live_evidence.slave,slave,slave_type);
   }}
  return 1;
}
int dc_arena_live_sample(dc_live_pose *out,dc_game_camera *camera){
 dc_goal_runtime *rt=runtime;uint32_t seq=dc_arena_live_evidence.update_returns;
 dc_live_pose pose;dc_game_camera c;unsigned delta;float transform[8];
 if(!rt||!out||!camera||rt->error!=DC_GOAL_OK||dc_goal_fiber_current())goto bad;
 if(dc_arena_live_evidence.dispatches<=4){
  if(seq || dc_arena_live_evidence.rendered ||
     dc_arena_live_evidence.warmup+1!=dc_arena_live_evidence.dispatches)goto bad;
  ++dc_arena_live_evidence.warmup;return 0;
 }
 if(seq<1||seq>DC_LIVE_UPDATES||seq!=dc_arena_live_evidence.rendered+1||
    dc_arena_live_evidence.dispatches!=seq+4||dc_arena_live_evidence.witnessed_updates!=seq||
    dc_arena_live_evidence.poison_batches!=seq||value(rt,math_sym)!=dc_arena_live_evidence.camera||!ordinary_mode(rt)||
    !dc_live_read(rt,value(rt,math_sym),math_type,&pose)||
    memcmp(&pose,(const void *)&dc_arena_live_evidence.trace[seq-1].pose,sizeof pose)||
    !dc_live_convert(&pose,55,&c))goto bad;
 if(seq==1){
  if(!dc_arena_live_initial_transform(transform)||!dc_live_initial_matches(&pose,transform))goto bad;
  dc_arena_live_evidence.initial=pose;dc_arena_live_evidence.init_verified=1;
 }
 /* Strafe/neutral interiors plus the exact two turn responses. The decoded
  * original strafe starts at output 17 for completed slot 16: no extra lag.
  * Slots 40/41 are deliberately neutral, not hidden turn settling frames. */
 delta=dc_arena_live_evidence.trace[seq-1].delta;
 if((seq>=4&&seq<=14)||(seq>=32&&seq<=38)||(seq>=48&&seq<=60)||(seq>=92&&seq<=98)||(seq>=128&&seq<=134)||(seq>=176&&seq<=180)){
  if(delta!=1)goto bad;
  ++dc_arena_live_evidence.neutral_pairs;
 }
 if((seq>=20&&seq<=26)||(seq>=68&&seq<=86)||(seq>=104&&seq<=122)||(seq>=140&&seq<=158)||(seq>=164&&seq<=170)){if(delta!=2)goto bad;++dc_arena_live_evidence.moving_pairs;}
 if(seq==41||seq==42){if(delta!=1)goto bad;}
 if(seq>=43&&seq<=44){
  const volatile dc_live_pose *prior=&dc_arena_live_evidence.trace[seq-2].pose;
  float cross=prior->inv[8]*pose.inv[10]-prior->inv[10]*pose.inv[8];
  float dot=prior->inv[8]*pose.inv[8]+prior->inv[10]*pose.inv[10];
  float angle=fabsf(atan2f(cross,dot));
  if(delta!=4||!dc_live_turn_matches(angle,dc_arena_live_evidence.trace[seq-1].time_adjust_ratio))goto bad;
  ++dc_arena_live_evidence.turning_pairs;
 }
 /* A stalled original process must not be hidden by repeated valid output. */
 if(seq>1){
  const volatile dc_live_trace *a=&dc_arena_live_evidence.trace[seq-2],*b=&dc_arena_live_evidence.trace[seq-1];
  if(b->master_resumed<=a->master_resumed||b->slave_resumed<=a->slave_resumed||
     b->combiner_resumed<=a->combiner_resumed)goto bad;
 }
 ++dc_arena_live_evidence.affine_reads;*out=pose;*camera=c;return 1;
bad:
 require(0,8,"live readback rejects stale output, reset pose, stalled processes or wrong input response");return -1;
}
int dc_arena_live_drawn(const dc_game_camera *c,float half,float draw_distance){
 uint32_t seq=dc_arena_live_evidence.update_returns;volatile dc_live_trace *row;dc_live_pose pose;
 REQUIRE(c && seq>=1 && seq<=DC_LIVE_UPDATES && seq==dc_arena_live_evidence.rendered+1 &&
         dc_arena_live_evidence.affine_reads==seq && dc_live_bounded(half,18) && half>0 &&
         dc_live_bounded(draw_distance,128) && draw_distance>0,8,"live render consumes exactly one fresh original camera");
 row=&dc_arena_live_evidence.trace[seq-1];
 pose=row->pose;
 REQUIRE(dc_live_camera_matches(&pose,c),8,"live submitted renderer camera differs from original readback");
 memcpy((void *)row->target,c->target,12);row->yaw=c->yaw;row->pitch=c->pitch;
 row->distance=c->distance;row->half=half;row->draw_distance=draw_distance;row->drawn=1;
 ++dc_arena_live_evidence.rendered;return 1;
}
int dc_arena_live_finish(dc_goal_runtime *rt){
  uint32_t master=value(rt,master_sym),comb=value(rt,combiner_sym),handle;
  unsigned i,j;float path_m=0;
  REQUIRE(object(rt,master,master_type,128) && object(rt,comb,combiner_type,112),5,
          "camera original startup produced master and combiner");
  handle=word(rt,master+116);dc_arena_live_evidence.registered_slave=word(rt,handle);
  REQUIRE(word(rt,master+112)==1 && dc_arena_live_evidence.registered_slave==dc_arena_live_evidence.slave.process &&
          object(rt,dc_arena_live_evidence.registered_slave,slave_type,112),5,
          "camera master registered exactly one original slave");
  REQUIRE(dc_arena_live_evidence.master.process==master && dc_arena_live_evidence.combiner.process==comb &&
          dc_arena_live_evidence.master.resumed && dc_arena_live_evidence.slave.resumed && dc_arena_live_evidence.combiner.resumed,5,
          "camera master slave and combiner actually resumed");
  REQUIRE(dc_arena_live_evidence.slave.free_frames && dc_arena_live_evidence.slave.state==free_state,5,
          "camera original slave entered and suspended in cam-free-floating");
  REQUIRE(dc_arena_live_evidence.selected_pad==0 && dc_arena_live_evidence.pad_services && dc_arena_live_evidence.valid_services &&
          !(dc_arena_live_evidence.pad_valid&128),6,"live requires a serviced valid recorded selected controller");
  REQUIRE(dc_arena_live_evidence.read_analog==rt->s7+DC_FIX_SYM_TRUE &&
          dc_arena_live_evidence.read_buttons==rt->s7+DC_FIX_SYM_TRUE &&
          dc_arena_live_evidence.master_mode==symbol(rt,"game"),5,"camera read flags enabled in game mode");
  REQUIRE(dc_arena_live_evidence.time_ratio_bits>=0x3f800000u &&
          dc_arena_live_evidence.time_ratio_bits<=0x4099999au,5,"camera display time adjust stays in supported NTSC/PAL 1 to 4.8 domain");
  REQUIRE(dc_arena_live_evidence.update_calls==DC_LIVE_UPDATES && dc_arena_live_evidence.update_returns==DC_LIVE_UPDATES,3,
          "camera original update-camera completed all 181 display dispatches");
  REQUIRE(dc_arena_live_evidence.poison_batches==DC_LIVE_UPDATES && dc_arena_live_evidence.witnessed_updates==DC_LIVE_UPDATES && dc_arena_live_evidence.poisoned_lanes==36*DC_LIVE_UPDATES,4,
          "live original outputs replaced all 6516 diagnostic lanes across 181 calls");
  REQUIRE(dc_arena_live_evidence.dispatches==DC_LIVE_DISPATCHES,7,"camera observed all 185 original dispatcher returns including display initialization");
  REQUIRE(dc_arena_live_evidence.rendered==DC_LIVE_UPDATES && dc_arena_live_evidence.affine_reads==DC_LIVE_UPDATES &&
          dc_arena_live_evidence.warmup==4 && dc_arena_live_evidence.init_events==1 && dc_arena_live_evidence.init_verified==1 &&
          dc_arena_live_evidence.neutral_pairs==50 && dc_arena_live_evidence.moving_pairs==71 &&
          dc_arena_live_evidence.turning_pairs==2 && dc_arena_live_evidence.position_changes>=71 &&
          dc_arena_live_evidence.orientation_changes>=2,8,"live requires every draw plus original input response and neutral stability");
  for(i=1;i<DC_LIVE_UPDATES;++i){
   float d2=0;
   for(j=0;j<3;++j){float d=(dc_arena_live_evidence.trace[i].pose.trans[j]-
       dc_arena_live_evidence.trace[i-1].pose.trans[j])/4096.f;d2+=d*d;}
   path_m+=sqrtf(d2);
  }
  REQUIRE(dc_live_bounded(path_m,20)&&path_m>=4.5f,8,"live raw original eye travels 4.5 to 20 meters along recording");
  REQUIRE(dc_goal_set_symbol(rt,update_symbol,original_update),1,"camera observer restores original update-camera binding");
  dc_arena_live_evidence.restored=1;
  return 1;
}
void dc_arena_live_report(char *out,size_t bytes,uint32_t used,uint32_t capacity){
  snprintf(out,bytes,"LIVE seq=%lu stores=%lu draws=%lu neutral=%lu move=%lu turn=%lu",
    (unsigned long)dc_arena_live_evidence.update_returns,(unsigned long)dc_arena_live_evidence.witnessed_updates,
    (unsigned long)dc_arena_live_evidence.rendered,(unsigned long)dc_arena_live_evidence.neutral_pairs,
    (unsigned long)dc_arena_live_evidence.moving_pairs,(unsigned long)dc_arena_live_evidence.turning_pairs);
  (void)used;(void)capacity;
}
#undef REQUIRE
#endif
