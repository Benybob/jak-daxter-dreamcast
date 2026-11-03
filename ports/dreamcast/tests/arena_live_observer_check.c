/* Host doubles exercise guards; never execute SH-4 or original GOAL (AI-assisted), ISC. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../arena_live.c"
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char memory[524288];
static dc_goal_runtime rt;
static dc_goal_fiber fiber;
static int live,mode,failures,calls,omit;
static int event_calls,service_calls,event_fault;
static float motion_x,motion_yaw;
static const float initial_target[3]={-281.192993f,31.275928f,-280.458771f};
static const char *failure;
dc_goal_fiber *dc_goal_fiber_current(void){return live?&fiber:NULL;}
int dc_arena_live_check(uint64_t a,uint64_t b,const char *why){if(a!=b){++failures;failure=why;return 0;}return 1;}
int dc_goal_note_process_span(dc_goal_runtime *r,uint32_t p,uint32_t n,struct dc_goal_process_span *o){(void)r;(void)p;(void)n;(void)o;return 1;}
int dc_goal_bind_process_context(dc_goal_runtime *r,uint32_t p,uint32_t n){(void)r;(void)p;(void)n;return 1;}
void dc_goal_clear_process_context(dc_goal_runtime *r){(void)r;}
int dc_arena_live_initial_transform(float out[8]){CHECK(dc_live_initial_transform(2.2f,-.45f,initial_target,55,out));return 1;}
static void put(uint32_t p,uint32_t v){unsigned char *b=span(&rt,p,4);CHECK(b);memcpy(b,&v,4);}
static uint32_t symval(const char *s,uint32_t v){uint32_t p=dc_goal_intern(&rt,s);CHECK(p&&dc_goal_set_symbol(&rt,p,v));return p;}
static dc_live_pose output_pose(void){
 dc_live_pose p={{0},{0},{0}};float q[8],yaw=2.2f+motion_yaw,pitch=-.45f;unsigned i,j;
 float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
 const float m[16]={cy,0,sy,0,sp*sy,cp,-sp*cy,0,-cp*sy,sp,cp*cy,0,0,0,0,1};
 dc_arena_live_initial_transform(q);memcpy(p.inv,m,64);memcpy(p.trans,q,16);p.trans[0]+=motion_x;
 for(i=0;i<3;++i){p.inv[12+i]=p.trans[i];for(j=0;j<3;++j){p.rot[4*i+j]=p.inv[4*j+i];p.rot[12+i]-=p.inv[4*i+j]*p.trans[j];}}
 p.rot[15]=1;return p;
}
static void write_pose(dc_live_pose *p){
 unsigned char *b=math_camera(&rt);unsigned i;
 for(i=0;i<36;++i){unsigned offset=i<4?DC_GAME_CAMERA_TRANS+4*i:i<20?DC_GAME_CAMERA_INV+4*(i-4):DC_GAME_CAMERA_ROT+4*(i-20);
  const float *f=i<4?p->trans+i:i<20?p->inv+i-4:p->rot+i-20;
  if((int)i!=omit)memcpy(b+offset,f,4);
 }
}
static uint64_t fake_update(void){
 dc_live_pose p;dc_goal_pad_state sample;unsigned completed=dc_arena_live_evidence.update_calls-1;++calls;
 if(mode==1)return rt.s7; /* skip ALL stores, including on a later call */
 if(mode==2){rt.error=DC_GOAL_BAD_FUNCTION;return 0;}
 if(mode==3){observe_update();return 0;}
 dc_live_recorded_pad(completed,0,&sample);
 if(sample.joyx)motion_x+=(sample.joyx>0?409.6f:-409.6f);
 if(sample.buttons&DC_PAD_RX_POS){
  uint32_t bits=word(&rt,value(&rt,display_sym)+916);float ratio;memcpy(&ratio,&bits,4);
  motion_yaw+=.0104719755f*ratio;
 }
 p=output_pose();
 if(mode==4){memset(&p,0,sizeof p);p.trans[3]=p.inv[0]=p.inv[5]=p.inv[10]=p.inv[15]=p.rot[0]=p.rot[5]=p.rot[10]=p.rot[15]=1;}
 write_pose(&p);
 if(mode==5)put(value(&rt,math_sym)-4,rt.basic_type);
 if(mode==6)put(mode_sym,rt.s7);
 return rt.s7;
}
static void setup(void){
 uint32_t master,slave,comb,handle;
 CHECK((uintptr_t)fake_update<=UINT32_MAX);
 CHECK(dc_goal_init(&rt,memory,sizeof memory,0x20000000));
 memset((void *)&dc_arena_live_evidence,0,sizeof dc_arena_live_evidence);
 dc_arena_live_evidence.version=3;dc_arena_live_evidence.producer=2;runtime=&rt;
 mode=failures=calls=in_update=0;omit=-1;failure=NULL;motion_x=motion_yaw=0;
 memset(&fiber,0,sizeof fiber);fiber.runtime=&rt;live=1;fiber.process=0x20000104;
 dproc_sym=symval("*dproc*",fiber.process);
 math_type=dc_goal_define_type(&rt,"math-camera",rt.basic_type,1060,0,9);
 master_type=dc_goal_define_type(&rt,"camera-master",rt.basic_type,128,0,9);
 slave_type=dc_goal_define_type(&rt,"camera-slave",rt.basic_type,128,0,9);
 combiner_type=dc_goal_define_type(&rt,"camera-combiner",rt.basic_type,128,0,9);
 math_sym=symval("*math-camera*",dc_goal_new_basic(&rt,math_type,1060));
 master=dc_goal_new_basic(&rt,master_type,128);slave=dc_goal_new_basic(&rt,slave_type,128);
 comb=dc_goal_new_basic(&rt,combiner_type,128);handle=dc_goal_alloc(&rt,16);CHECK(master&&slave&&comb&&handle);
 master_sym=symval("*camera*",master);combiner_sym=symval("*camera-combiner*",comb);
 put(master+112,1);put(master+116,handle);put(handle,slave);
 free_state=dc_goal_intern(&rt,"cam-free-floating");suspended=dc_goal_intern(&rt,"suspended");
 dc_arena_live_evidence.master.process=master;dc_arena_live_evidence.master.resumed=3;
 dc_arena_live_evidence.slave.process=slave;dc_arena_live_evidence.slave.resumed=3;
 dc_arena_live_evidence.slave.state=free_state;dc_arena_live_evidence.slave.free_frames=3;
 dc_arena_live_evidence.combiner.process=comb;dc_arena_live_evidence.combiner.resumed=3;
 layout_sym=symval("*cam-layout*",rt.s7);
 game_mode=dc_goal_intern(&rt,"game");mode_sym=symval("*master-mode*",game_mode);
 {uint32_t type=dc_goal_define_type(&rt,"test-display",rt.basic_type,1024,0,9),display=dc_goal_new_basic(&rt,type,1024);
  display_sym=symval("*display*",display);put(display+916,0x3f800000);}
 {uint32_t levels;unsigned i;
  level_type=dc_goal_define_type(&rt,"level",rt.basic_type,2608,0,9);
  levels_type=dc_goal_define_type(&rt,"level-group",rt.basic_type,0x1ef4,0,9);
  levels=dc_goal_new_basic(&rt,levels_type,0x1ef4);levels_sym=symval("*level*",levels);
  inactive=dc_goal_intern(&rt,"inactive");put(levels,2);
  for(i=0;i<2;++i){put(levels+96+i*2608-4,level_type);put(levels+96+i*2608+16,inactive);}}
 external_sym=symval("*external-cam-mode*",rt.s7);progress_sym=symval("*progress-process*",rt.s7);
 look_sym=symval("*camera-look-through-other*",0);
 analog_sym=symval("*camera-read-analog*",rt.s7+DC_FIX_SYM_TRUE);buttons_sym=symval("*camera-read-buttons*",rt.s7+DC_FIX_SYM_TRUE);
 dc_arena_live_evidence.read_analog=dc_arena_live_evidence.read_buttons=rt.s7+DC_FIX_SYM_TRUE;
 dc_arena_live_evidence.master_mode=game_mode;dc_arena_live_evidence.time_ratio_bits=0x3f800000;
 dc_arena_live_evidence.dispatches=4;dc_arena_live_evidence.warmup=4;dc_arena_live_evidence.init_events=1;
 update_symbol=symval("update-camera",0xdeadbeef);original_update=(uint32_t)(uintptr_t)fake_update;
 shim=0xdeadbeef;dc_arena_live_evidence.original_update=original_update;dc_arena_live_evidence.shim=shim;
}
static void next_call(void){
 uint32_t seq=dc_arena_live_evidence.update_calls+1;
 live=1;dc_arena_live_evidence.dispatches=seq+3;
 ++dc_arena_live_evidence.valid_services;dc_arena_live_evidence.pad_services+=4;
 dc_arena_live_evidence.pad_stage=dc_live_input_stage(seq-1);
 ++dc_arena_live_evidence.master.resumed;++dc_arena_live_evidence.slave.resumed;++dc_arena_live_evidence.combiner.resumed;
 observe_update();live=0;dc_arena_live_evidence.dispatches=seq+4;
}
static void refusals(void){
 unsigned i;dc_live_pose p;dc_game_camera c;
 for(i=0;i<36;++i){setup();omit=(int)i;next_call();CHECK(failures==1&&dc_arena_live_evidence.reason==4&&dc_arena_live_evidence.witnessed_updates==0);}
 setup();next_call();CHECK(!failures&&dc_arena_live_evidence.witnessed_updates==1);
 CHECK(dc_arena_live_sample(&p,&c)==1);CHECK(dc_arena_live_drawn(&c,14,80));
 mode=1;next_call();CHECK(failures==1&&dc_arena_live_evidence.reason==4&&dc_arena_live_evidence.witnessed_updates==1);
 setup();mode=2;next_call();CHECK(failures==1&&dc_arena_live_evidence.reason==3&&dc_arena_live_evidence.update_returns==0);
 setup();mode=3;next_call();CHECK(failures>=1&&dc_arena_live_evidence.reason==2&&dc_arena_live_evidence.update_returns==0);
 setup();mode=4;next_call();CHECK(!failures);CHECK(dc_arena_live_sample(&p,&c)==-1&&dc_arena_live_evidence.reason==8);
 setup();mode=5;next_call();CHECK(failures==1&&dc_arena_live_evidence.reason==4);
 setup();mode=6;next_call();CHECK(failures==1&&dc_arena_live_evidence.reason==4);
 setup();live=0;observe_update();CHECK(failures==1&&calls==0);
 setup();fiber.process+=16;observe_update();CHECK(failures==1&&calls==0);
 setup();put(external_sym,rt.s7+8);next_call();CHECK(failures==1&&calls==0);
 setup();put(progress_sym,rt.s7+8);next_call();CHECK(failures==1&&calls==0);
 setup();put(value(&rt,display_sym)+916,0x40a00000u);next_call();CHECK(failures==1&&calls==0&&dc_arena_live_evidence.poison_batches==0);
 setup();put(layout_sym,0);next_call();CHECK(failures==1&&calls==0);
 setup();put(layout_sym,rt.s7+8);next_call();CHECK(failures==1&&calls==0);
 setup();put(look_sym,1);next_call();CHECK(failures==1&&calls==0);
 setup();put(value(&rt,levels_sym)+112,rt.s7);next_call();CHECK(failures==1&&calls==0&&dc_arena_live_evidence.poison_batches==0);
 setup();dc_arena_live_evidence.valid_services=0;observe_update();CHECK(failures==1&&calls==0&&dc_arena_live_evidence.reason==6);
 setup();next_call();CHECK(!failures);put(value(&rt,math_sym)+DC_GAME_CAMERA_TRANS,0);
 CHECK(dc_arena_live_sample(&p,&c)==-1);
 setup();next_call();CHECK(dc_arena_live_sample(&p,&c)==1);CHECK(dc_arena_live_drawn(&c,14,80));
 CHECK(dc_arena_live_sample(&p,&c)==-1); /* duplicate */
 setup();dc_arena_live_evidence.dispatches=5;live=0;CHECK(dc_arena_live_sample(&p,&c)==-1); /* missing update */
 setup();dc_arena_live_evidence.dispatches=0;dc_arena_live_evidence.warmup=0;live=0;
 for(i=1;i<=4;++i){dc_arena_live_evidence.dispatches=i;CHECK(dc_arena_live_sample(&p,&c)==0);}
 CHECK(dc_arena_live_sample(&p,&c)==-1); /* duplicate warmup */
 setup();live=0;CHECK(!dc_arena_live_begin(&rt,NULL)&&dc_arena_live_evidence.reason==1); /* not a loader export */
}
static uint64_t fake_event(uint64_t master,uint64_t block){
 uint64_t argument;float expected[8];++event_calls;
 CHECK(master==value(&rt,master_sym));CHECK(word(&rt,(uint32_t)block+8)==1);
 CHECK(word(&rt,(uint32_t)block+12)==symbol(&rt,"teleport-to-transformq"));
 memcpy(&argument,span(&rt,(uint32_t)block+16,8),8);CHECK(argument==init_transform);
 CHECK(dc_arena_live_initial_transform(expected));CHECK(!memcmp(span(&rt,init_transform,32),expected,32));
 if(event_fault)rt.error=DC_GOAL_BAD_ARGUMENT;
 return rt.s7;
}
static void fake_service(void){++service_calls;++dc_arena_live_evidence.valid_services;}
static void init_setup(void){
 uint32_t type,listener;
 setup();live=0;dc_arena_live_evidence.dispatches=3;dc_arena_live_evidence.init_events=0;
 type=dc_goal_define_type(&rt,"process",rt.basic_type,128,0,9);listener=dc_goal_new_basic(&rt,type,128);
 symval("*listener-process*",listener);put(listener+80,listener-4+128);
 CHECK(dc_goal_intern(&rt,"teleport-to-transformq"));init_block=dc_goal_alloc(&rt,80);init_transform=dc_goal_alloc(&rt,32);
 event_entry=(uint32_t)(uintptr_t)fake_event;service_entry=(uint32_t)(uintptr_t)fake_service;
 event_calls=service_calls=event_fault=0;
}
static void layout_default(void){
 setup();put(layout_sym,0);CHECK(initialize_layout_default(&rt));CHECK(value(&rt,layout_sym)==rt.s7);
 CHECK(initialize_layout_default(&rt));CHECK(value(&rt,layout_sym)==rt.s7);
 setup();put(layout_sym,rt.s7+8);CHECK(!initialize_layout_default(&rt));CHECK(value(&rt,layout_sym)==rt.s7+8);
}
static void turn_refusals(void){
 unsigned seq;dc_live_pose p;dc_game_camera c;
 /* Baseline PAL response consumes a different ratio from the later header.
  * A zero, early or delayed response must still fail the SAME exact window. */
 for(int mutant=0;mutant<4;++mutant){
  setup();
  for(seq=1;seq<=44;++seq){
   put(value(&rt,display_sym)+916,seq==43?0x4099999au:0x3f800000u);
   if(seq==43&&mutant==1)mode=1;
   next_call();
   if(mutant==1&&seq==43){CHECK(failures==1&&dc_arena_live_evidence.reason==4);break;}
   if((mutant==2&&seq==43)||(mutant==3&&seq==41)){
    /* Valid freshly stored affine output with the wrong input response. */
    dc_live_pose wrong=seq==43?dc_arena_live_evidence.trace[seq-2].pose:output_pose();
    if(seq==41){motion_yaw+=.01f;wrong=output_pose();}
    write_pose(&wrong);dc_arena_live_evidence.trace[seq-1].pose=wrong;
    dc_arena_live_evidence.trace[seq-1].delta=dc_live_delta((const dc_live_pose *)&dc_arena_live_evidence.trace[seq-2].pose,&wrong);
   }
   if((mutant==2&&seq==43)||(mutant==3&&seq==41)){CHECK(dc_arena_live_sample(&p,&c)==-1);break;}
   CHECK(!failures&&dc_arena_live_sample(&p,&c)==1);
   CHECK(dc_arena_live_drawn(&c,14,80));
   if(seq==43)CHECK(fabsf(dc_arena_live_evidence.trace[42].time_adjust_ratio-4.8f)<.000001f);
  }
 }
}
static void initialization(void){
 init_setup();CHECK(dc_arena_live_initialize(&rt)&&!failures&&event_calls==1&&service_calls==3);
 CHECK(!dc_arena_live_initialize(&rt)&&event_calls==1); /* cannot reinitialize camera */
 init_setup();event_fault=1;CHECK(!dc_arena_live_initialize(&rt));CHECK(rt.error==DC_GOAL_BAD_ARGUMENT&&service_calls==0);
 init_setup();dc_arena_live_evidence.slave.free_frames=0;CHECK(!dc_arena_live_initialize(&rt)&&!event_calls);
 init_setup();init_transform=rt.base+rt.used;CHECK(!dc_arena_live_initialize(&rt)&&!event_calls);
 init_setup();live=1;CHECK(!dc_arena_live_initialize(&rt)&&!event_calls);
}
static void pad_validation(void){
 uint32_t bank,pad;unsigned i;dc_goal_pad_state state;unsigned char *bytes;
 setup();bank=dc_goal_alloc(&rt,16);bank_sym=symval("*CAMERA-bank*",bank);put(bank+4,0);
 pad=dc_goal_alloc(&rt,144);bytes=span(&rt,pad,132);CHECK(bytes);memset(bytes,0,132);
 for(i=0;i<=181;++i){
  dc_arena_live_evidence.update_returns=i;CHECK(dc_live_recorded_pad(i,0,&state));
  dc_goal_pad_fill(bytes,&state);bytes[0]=6;dc_arena_live_pad(&rt,pad);
  CHECK(!failures&&dc_arena_live_evidence.valid_services==i+1);
 }
 for(i=1;i<20;++i){
  uint32_t count=dc_arena_live_evidence.valid_services;
  CHECK(dc_live_recorded_pad(181,0,&state));dc_goal_pad_fill(bytes,&state);bytes[0]=6;bytes[i]^=1;
  rt.error=DC_GOAL_OK;failures=0;dc_arena_live_pad(&rt,pad);CHECK(failures==1&&dc_arena_live_evidence.valid_services==count);
 }
}
static void complete_fixture(const char *path){
 unsigned seq;dc_live_pose p;dc_game_camera c;FILE *f;
 setup();
 for(seq=1;seq<=181;++seq){put(value(&rt,display_sym)+916,seq==43?0x4099999au:0x3f800000u);next_call();CHECK(!failures);CHECK(dc_arena_live_sample(&p,&c)==1);CHECK(dc_arena_live_drawn(&c,14,80));}
 CHECK(dc_arena_live_evidence.neutral_pairs==50&&dc_arena_live_evidence.moving_pairs==71&&dc_arena_live_evidence.turning_pairs==2);
 CHECK(dc_arena_live_evidence.poisoned_lanes==6516&&dc_arena_live_evidence.witnessed_updates==181);
 /* Strict finish count mutants: valid host doubles are still never device evidence. */
 {static const uint32_t bad[][3]={{180,180,185},{181,180,185},{180,181,185},{182,182,185},{181,181,184},{181,181,186}};unsigned i;
  for(i=0;i<sizeof bad/sizeof bad[0];++i){
   dc_arena_live_evidence.update_calls=bad[i][0];dc_arena_live_evidence.update_returns=bad[i][1];dc_arena_live_evidence.dispatches=bad[i][2];
   CHECK(!dc_arena_live_finish(&rt));CHECK(value(&rt,update_symbol)==shim);failures=0;dc_arena_live_evidence.reason=0;
  }}
 dc_arena_live_evidence.update_calls=dc_arena_live_evidence.update_returns=181;dc_arena_live_evidence.dispatches=185;
 CHECK(dc_arena_live_finish(&rt)&&!failures&&dc_arena_live_evidence.restored==1);
 CHECK(value(&rt,update_symbol)==original_update);
 f=fopen(path,"wb");CHECK(f);CHECK(fwrite((const void *)&dc_arena_live_evidence,1,sizeof dc_arena_live_evidence,f)==sizeof dc_arena_live_evidence);CHECK(!fclose(f));
}
int main(int argc,char **argv){CHECK(argc==2);refusals();layout_default();turn_refusals();initialization();pad_validation();complete_fixture(argv[1]);printf("live observer HOST DOUBLES: %u assertions; 181 synthetic witness/draw records\n",checks);return 0;}
