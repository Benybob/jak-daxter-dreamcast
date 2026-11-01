/* Host doubles test probe refusal, never execute SH-4/original GOAL (AI-assisted), ISC. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../arena_camera.c"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"failed: %s:%d %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)
static unsigned char memory[524288];
static dc_goal_runtime rt;
static dc_goal_fiber fiber;
static int live,mode,calls,failures;
static const char *failure;
dc_goal_fiber *dc_goal_fiber_current(void){return live?&fiber:NULL;}
int dc_arena_camera_check(uint64_t got,uint64_t expected,const char *why){
  if(got!=expected){++failures;failure=why;return 0;}return 1;
}
static void put(uint32_t p,uint32_t v){void *b=dc_goal_resolve(&rt,p,4);CHECK(b);memcpy(b,&v,4);}
static uint64_t fake_update(void){
  unsigned char *b=math_camera(&rt);++calls;
  if(mode==1)put(value(&rt,math_sym)+DC_GAME_CAMERA_TRANS+12,0x3f800000u);
  if(mode==2)rt.error=DC_GOAL_BAD_FUNCTION;
  if(mode==3){put(value(&rt,math_sym)+DC_GAME_CAMERA_TRANS+12,0x3f800000u);memset(b+DC_GAME_CAMERA_INV,0xff,4);}
  return rt.s7;
}
static void setup(void){
  uint32_t comb;
  CHECK((uintptr_t)fake_update<=UINT32_MAX); /* Python chooses a low host image base. */
  CHECK(dc_goal_init(&rt,memory,sizeof memory,0x20000000u));
  memset((void *)&dc_arena_camera_metrics,0,sizeof dc_arena_camera_metrics);
  dc_arena_camera_metrics.version=1;dc_arena_camera_metrics.producer=1;
  memset(&fiber,0,sizeof fiber);fiber.runtime=&rt;runtime=&rt;live=1;
  fiber.process=0x20000104u;dproc_sym=dc_goal_intern(&rt,"*dproc*");
  CHECK(dc_goal_set_symbol(&rt,dproc_sym,fiber.process));
  mode=calls=failures=0;failure=NULL;have_previous=0;
  math_type=dc_goal_define_type(&rt,"math-camera",rt.basic_type,DC_GAME_CAMERA_BYTES,0,9);
  combiner_type=dc_goal_define_type(&rt,"camera-combiner",rt.basic_type,128,0,9);
  CHECK(math_type && combiner_type);
  math_sym=dc_goal_intern(&rt,"*math-camera*");combiner_sym=dc_goal_intern(&rt,"*camera-combiner*");
  external_sym=dc_goal_intern(&rt,"*external-cam-mode*");
  CHECK(dc_goal_set_symbol(&rt,math_sym,dc_goal_new_basic(&rt,math_type,DC_GAME_CAMERA_BYTES)));
  comb=dc_goal_new_basic(&rt,combiner_type,128);CHECK(comb);
  CHECK(dc_goal_set_symbol(&rt,combiner_sym,comb));CHECK(dc_goal_set_symbol(&rt,external_sym,rt.s7));
  original_update=(uint32_t)(uintptr_t)fake_update;
}
static uint32_t diagnostic_objects(void){
  uint32_t type=dc_goal_define_type(&rt,"diagnostic",rt.basic_type,1024,0,9);
  uint32_t bank=dc_goal_new_basic(&rt,type,1024),pads=dc_goal_new_basic(&rt,type,1024);
  uint32_t display=dc_goal_new_basic(&rt,type,1024),pad=dc_goal_new_basic(&rt,type,1024);
  CHECK(bank && pads && display && pad);
  bank_sym=dc_goal_intern(&rt,"*CAMERA-bank*");pads_sym=dc_goal_intern(&rt,"*cpad-list*");
  display_sym=dc_goal_intern(&rt,"*display*");
  CHECK(dc_goal_set_symbol(&rt,bank_sym,bank));CHECK(dc_goal_set_symbol(&rt,pads_sym,pads));
  CHECK(dc_goal_set_symbol(&rt,display_sym,display));
  put(bank+4,2);put(pads,4);put(pads+12,pad);put(pad+32,2);put(pad,128);
  put(display+916,0x3f800000u);
  return pad;
}
/* Finish-contract fixture only: these counters are host doubles, not evidence
 * that original camera processes ran. Preserve the exact 61-update requirement
 * while adding the original display-loop's initialization dispatch. */
static void finish_setup(void){
  uint32_t master,slave,handle;
  setup();live=0;
  master_type=dc_goal_define_type(&rt,"camera-master",rt.basic_type,128,0,9);
  slave_type=dc_goal_define_type(&rt,"camera-slave",rt.basic_type,128,0,9);
  master=dc_goal_new_basic(&rt,master_type,128);slave=dc_goal_new_basic(&rt,slave_type,128);
  handle=dc_goal_alloc(&rt,16);CHECK(master && slave && handle);
  master_sym=dc_goal_intern(&rt,"*camera*");CHECK(dc_goal_set_symbol(&rt,master_sym,master));
  put(master+112,1);put(master+116,handle);put(handle,slave);
  free_state=dc_goal_intern(&rt,"cam-free-floating");
  update_symbol=dc_goal_intern(&rt,"update-camera");CHECK(dc_goal_set_symbol(&rt,update_symbol,0xdeadbeefu));
  dc_arena_camera_metrics.master.process=master;dc_arena_camera_metrics.master.resumed=1;
  dc_arena_camera_metrics.slave.process=slave;dc_arena_camera_metrics.slave.resumed=1;
  dc_arena_camera_metrics.slave.state=free_state;dc_arena_camera_metrics.slave.free_frames=1;
  dc_arena_camera_metrics.combiner.process=value(&rt,combiner_sym);dc_arena_camera_metrics.combiner.resumed=1;
  dc_arena_camera_metrics.pad_services=dc_arena_camera_metrics.valid_services=1;
  dc_arena_camera_metrics.read_analog=dc_arena_camera_metrics.read_buttons=rt.s7+DC_FIX_SYM_TRUE;
  dc_arena_camera_metrics.master_mode=dc_goal_intern(&rt,"game");
  dc_arena_camera_metrics.time_ratio_bits=0x3f800000u;
  dc_arena_camera_metrics.update_calls=dc_arena_camera_metrics.update_returns=61;
  dc_arena_camera_metrics.diagnostic_seeds=dc_arena_camera_metrics.original_wrote_camera=1;
  dc_arena_camera_metrics.dispatches=65;
}
static void finish_counts(void){
  static const uint32_t bad[][3]={{60,60,65},{60,61,65},{61,60,65},{62,62,65},{61,61,64},{61,61,66}};
  unsigned i;
  for(i=0;i<sizeof bad/sizeof bad[0];++i){
    finish_setup();
    dc_arena_camera_metrics.update_calls=bad[i][0];dc_arena_camera_metrics.update_returns=bad[i][1];
    dc_arena_camera_metrics.dispatches=bad[i][2];
    CHECK(!dc_arena_camera_finish(&rt) && failures==1);
    CHECK(dc_arena_camera_metrics.reason==(i<4?3u:7u));
    CHECK(strstr(failure,i<4?"all 61":"all 65"));
    CHECK(value(&rt,update_symbol)==0xdeadbeefu);
  }
  finish_setup();CHECK(dc_arena_camera_finish(&rt) && !failures);
  CHECK(value(&rt,update_symbol)==original_update);
}
int main(void){
  uint32_t camera,pad;
  setup();mode=1;observe_update();
  CHECK(calls==1 && !failures && dc_arena_camera_metrics.original_wrote_camera==1);
  CHECK(dc_arena_camera_metrics.diagnostic_seeds==1 && dc_arena_camera_metrics.update_returns==1);
  observe_update();CHECK(dc_arena_camera_metrics.diagnostic_seeds==1 && dc_arena_camera_metrics.pose_changes==0);
  setup();observe_update();
  CHECK(calls==1 && failures==1 && dc_arena_camera_metrics.reason==4);
  CHECK(dc_arena_camera_metrics.original_wrote_camera==0 && strstr(failure,"did not produce"));
  setup();mode=2;observe_update();
  CHECK(calls==1 && dc_arena_camera_metrics.reason==3 && dc_arena_camera_metrics.update_returns==0);
  CHECK(dc_arena_camera_metrics.runtime_error==DC_GOAL_BAD_FUNCTION);
  setup();mode=3;observe_update();CHECK(calls==1 && dc_arena_camera_metrics.reason==2 && strstr(failure,"nonfinite"));
  setup();live=0;observe_update();CHECK(calls==0 && dc_arena_camera_metrics.diagnostic_seeds==0);
  setup();fiber.process+=16;observe_update();CHECK(calls==0 && dc_arena_camera_metrics.diagnostic_seeds==0);
  setup();camera=value(&rt,math_sym);put(camera-4,rt.basic_type);observe_update();
  CHECK(calls==0 && dc_arena_camera_metrics.reason==2);
  setup();rt.used=camera-rt.base;observe_update();CHECK(calls==0 && dc_arena_camera_metrics.reason==2);
  setup();CHECK(dc_goal_set_symbol(&rt,combiner_sym,rt.s7));observe_update();
  CHECK(calls==0 && dc_arena_camera_metrics.reason==5 && dc_arena_camera_metrics.diagnostic_seeds==0);
  setup();CHECK(dc_goal_set_symbol(&rt,external_sym,rt.s7+8));observe_update();
  CHECK(calls==0 && dc_arena_camera_metrics.reason==5);
  setup();live=0;CHECK(!dc_arena_camera_begin(&rt,NULL));
  CHECK(dc_arena_camera_metrics.reason==1 && calls==0 && strstr(failure,"exact loader"));
  setup();pad=diagnostic_objects();live=0;
  CHECK(dc_arena_camera_frame(&rt));
  CHECK(dc_arena_camera_metrics.selected_pad==2 && dc_arena_camera_metrics.pad_valid==128);
  CHECK(dc_arena_camera_metrics.time_ratio_bits==0x3f800000u);
  dc_arena_camera_pad(&rt,pad);CHECK(dc_arena_camera_metrics.valid_services==0);
  put(pad,6);dc_arena_camera_pad(&rt,pad);CHECK(dc_arena_camera_metrics.valid_services==1);
  put(pad+32,1);dc_arena_camera_pad(&rt,pad);CHECK(dc_arena_camera_metrics.valid_services==1);
  CHECK(dc_arena_camera_metrics.pad_services==3);
  put(value(&rt,bank_sym)+4,4);CHECK(!dc_arena_camera_frame(&rt));
  CHECK(dc_arena_camera_metrics.reason==2);
  finish_counts();
  puts("camera host doubles: write witness, stationary output, no-store/fault/schema/owner/missing-entry refusals");
  puts("camera finish host doubles: exactly 61 updates and 65 dispatches; short/excess/mismatched counts refused");
  return 0;
}
