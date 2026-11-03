/* Reuse the live observer's HOST fixtures; no original SH-4 execution.
 * Test Jak's missing-initializer refusal plus real recorded cpad path.
 * The old fixture main is compiled but not called (AI-assisted), ISC. */
#define main unused_live_fixture_main
#include "arena_live_observer_check.c"
#undef main
volatile dc_jak_evidence dc_arena_jak_evidence;
int main(void){
  unsigned i;unsigned char pad[132]={0};dc_goal_pad_state state;
  setup();live=0;memset((void *)&dc_arena_jak_evidence,0,sizeof dc_arena_jak_evidence);
  put(layout_sym,0);CHECK(!initialize_layout_default(&rt));CHECK(value(&rt,layout_sym)==0);
  dc_arena_jak_evidence.debug_calls=dc_arena_jak_evidence.debug_restored=1;
  CHECK(!initialize_layout_default(&rt));CHECK(value(&rt,layout_sym)==0); /* no C repair */
  put(layout_sym,rt.s7);CHECK(initialize_layout_default(&rt));
  put(layout_sym,rt.s7+8);CHECK(!initialize_layout_default(&rt));CHECK(value(&rt,layout_sym)==rt.s7+8);
  /* Unchanged service handshake: recording only at hardware boundary. */
  dc_live_input_advance(0);dc_goal_cpad_open(pad,0);
  for(i=0;i<3;++i)dc_goal_cpad_get_data(pad);
  CHECK(pad[0]==6);
  for(i=0;i<182;++i){
    uint16_t buttons;
    dc_live_input_advance(i);CHECK(dc_live_recorded_pad(i,0,&state));dc_goal_cpad_get_data(pad);
    memcpy(&buttons,pad+2,2);CHECK(pad[0]==6 && pad[4]==128 && pad[5]==128 && pad[7]==128);
    if(i==42||i==43){CHECK(state.buttons==(DC_PAD_B|DC_PAD_L2));CHECK(buttons==0x2100 && pad[6]==128);}
    else CHECK(buttons==0 && state.buttons==0);
    CHECK(pad[6]==(unsigned char)(128+state.joyx));
  }
  CHECK(dc_arena_jak_evidence.debug_calls==1 && !dc_arena_jak_evidence.spawn_calls);
  printf("jak observer HOST fixtures: %u assertions; digital hw buttons=0x2100, right X=128\n",checks);
  return 0;
}
