/* Integrated synthetic capsule controller tests (AI-assisted), ISC. */
#include "player_world_motion.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned total,failed;
#define CHECK(x) do{++total;if(!(x)){++failed;printf("world player failure line %u\n",(unsigned)__LINE__);}}while(0)
unsigned dc_world_player_cases(unsigned *count){
 dc_collision_world w={0};dc_world_triangle t[4];dc_player p,prior;dc_collision_result r,before;unsigned i;float top=0;
 total=failed=0;
 t[0]=(dc_world_triangle){{-10,0,-10},{-10,0,10},{10,0,10},448};
 t[1]=(dc_world_triangle){{-10,0,-10},{10,0,10},{10,0,-10},448};
 t[2]=(dc_world_triangle){{1,0,-3},{1,3,-3},{1,3,3},8};
 t[3]=(dc_world_triangle){{1,0,-3},{1,3,3},{1,0,3},8};
 CHECK(dc_collision_build(&w,t,4)==NULL);
 memset(&p,0x5a,sizeof p);prior=p;
 CHECK(!dc_player_world_place(&p,&w,(dc_vec3){0,-.1f,0}));CHECK(!memcmp(&p,&prior,sizeof p));
 CHECK(dc_player_world_place(&p,&w,(dc_vec3){0,2,0}));CHECK(p.grounded && p.y<.002f && p.x==0);
 for(i=0;i<60;++i)CHECK(dc_player_world_tick(&p,&w,0,.5f,0,&r));
 CHECK(p.mode==DC_PLAYER_WALK && p.z>1.4f && p.z<1.7f);
 for(i=0;i<120;++i){CHECK(dc_player_world_tick(&p,&w,0,0,1,&r));if(p.y>top)top=p.y;}
 CHECK(p.jumps==1 && p.landings==1 && p.grounded && top>1.2f && top<1.4f);
 prior=p;memset(&r,0x6a,sizeof r);before=r;
 CHECK(!dc_player_world_tick(&p,&w,NAN,0,1,&r));CHECK(!memcmp(&p,&prior,sizeof p));CHECK(!memcmp(&r,&before,sizeof r));
 for(i=0;i<90;++i)CHECK(dc_player_world_tick(&p,&w,1,0,0,&r));
 CHECK(p.x<.721f && p.x>.7f);
 CHECK(dc_collision_clear(&w,(dc_vec3){0,0,0},.28f,1.7f));
 CHECK(!dc_collision_clear(&w,(dc_vec3){.9f,0,0},.28f,1.7f));
 dc_collision_free(&w);
 if(count)*count=total;
 return failed;
}
