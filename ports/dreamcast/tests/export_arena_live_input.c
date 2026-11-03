/* Export the production recording for asset staging (AI-assisted), ISC. */
#include "arena_live.h"
#include <stdio.h>
int main(void){
 unsigned i;dc_goal_pad_state s;
 puts("completed,stage,joyx,buttons");
 for(i=0;i<DC_LIVE_UPDATES;++i){
  if(!dc_live_recorded_pad(i,0,&s))return 1;
  printf("%u,%u,%d,%u\n",i,dc_live_input_stage(i),s.joyx,(unsigned)s.buttons);
 }
 return 0;
}
