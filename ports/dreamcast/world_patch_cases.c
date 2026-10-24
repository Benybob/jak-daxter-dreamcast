/* Optional private original training collision oracle (AI-assisted), ISC.
 * No game-derived vertices are in this source. The reference patch is exported
 * from the user's FR3 at the documented origin/radius; it stays local. */
#include "player_world_motion.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned total,failed;
uint32_t dc_world_patch_max_tests;
#define CHECK(x) do{++total;if(!(x)){++failed;printf("original world failure line %u\n",(unsigned)__LINE__);}}while(0)
unsigned dc_world_patch_cases(FILE *file,unsigned *count){
 dc_collision_world w={0};dc_vec3 origin;dc_player p,prior;dc_collision_result r;unsigned i,route;
 const char *error;total=failed=dc_world_patch_max_tests=0;
 error=dc_collision_load(file,&w,&origin);CHECK(error==NULL);
 if(error){printf("%s\n",error);if(count)*count=total;return failed;}
 CHECK(w.count==576);CHECK(fabsf(origin.x+1159.3880615f)<.001f && fabsf(origin.z-1001.145813f)<.001f);
 memset(&p,0x5a,sizeof p);prior=p;
 CHECK(!dc_player_world_place(&p,&w,(dc_vec3){0,2,0}));CHECK(!memcmp(&p,&prior,sizeof p));
 for(route=0;route<2;++route){
  dc_vec3 start=route?(dc_vec3){-4,3,-4}:(dc_vec3){0,3,0};int placed=dc_player_world_place(&p,&w,start);
  CHECK(placed);if(!placed)continue;
  CHECK(route?(p.y>-.2f && p.y<0):(p.y>2 && p.y<2.1f));
  for(i=0;i<(route?180u:240u);++i){
   float iz=route?(i<60?.5f:0):(i<40?.5f:(i<80?1:0));
   int jump=route?i>=60:i>=80;
   int ok=dc_player_world_tick(&p,&w,0,iz,jump,&r);CHECK(ok);if(!ok)break;
   CHECK(isfinite(p.y) && p.y>-1 && p.y<4);
   CHECK(!r.event_bits[1] && !(r.event_bits[0]&~UINT32_C(1)));
   if(r.distance_tests>dc_world_patch_max_tests)dc_world_patch_max_tests=r.distance_tests;
  }
  CHECK(p.jumps==1 && p.landings==1 && p.grounded);
  CHECK(route?p.z>-2.5f:(p.z>.1f && p.z<.6f));
 }
 CHECK(dc_world_patch_max_tests<4096);
 dc_collision_free(&w);if(count)*count=total;return failed;
}
