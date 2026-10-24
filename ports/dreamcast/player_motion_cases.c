/* Deterministic movement/collision oracle (AI-assisted), ISC. */
#include "player_motion.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned total,failed;
#define CHECK(x) do{++total;if(!(x)){++failed;printf("motion failure line %u\n",(unsigned)__LINE__);}}while(0)
unsigned dc_player_motion_cases(unsigned *count){
 dc_player p,q,before;unsigned i;float top=0,walk,run;
 total=failed=0;dc_player_init(&p);
 for(i=0;i<120;++i)CHECK(dc_player_tick(&p,0,0,0));
 CHECK(p.y==0 && p.grounded && p.mode==DC_PLAYER_IDLE && p.x==0 && p.z==0);
 before=p;CHECK(!dc_player_tick(&p,NAN,0,1));CHECK(memcmp(&p,&before,sizeof p)==0);
 CHECK(!dc_player_tick(NULL,0,0,0));
 dc_player_init(&p);dc_player_init(&q);
 for(i=0;i<60;++i){dc_player_tick(&p,0,0.5f,0);dc_player_tick(&q,0,1,0);}
 walk=p.z;run=q.z;CHECK(walk>1 && walk<2);CHECK(run>4 && run<6);
 CHECK(p.mode==DC_PLAYER_WALK && q.mode==DC_PLAYER_RUN);
 dc_player_init(&p);dc_player_init(&q);p.x=q.x=-8;p.z=q.z=-8;
 for(i=0;i<30;++i){dc_player_tick(&p,1,1,0);dc_player_tick(&q,0,1,0);}
 CHECK(fabsf(sqrtf(p.vx*p.vx+p.vz*p.vz)-q.vz)<0.001f);
 dc_player_init(&p);
 for(i=0;i<180;++i){dc_player_tick(&p,0,0,1);if(p.y>top)top=p.y;CHECK(p.y>=0);}
 CHECK(top>1.2f && top<1.4f);CHECK(p.jumps==1 && p.landings==1 && p.grounded);
 dc_player_tick(&p,0,0,0);dc_player_tick(&p,0,0,1);CHECK(p.jumps==2 && !p.grounded);
 dc_player_init(&p);
 for(i=0;i<120;++i)dc_player_tick(&p,1,0,0);
 CHECK(p.x<=1.721f && p.x>=1.70f && p.y==0);
 for(i=0;i<30;++i)dc_player_tick(&p,1,0,1);
 CHECK(p.x>2 && p.y>0.55f);
 for(i=0;i<90;++i)dc_player_tick(&p,0,0,0);
 CHECK(p.grounded && fabsf(p.y-0.55f)<0.001f && p.landings==1);
 for(i=0;i<120;++i)dc_player_tick(&p,0,1,0);
 CHECK(p.y==0 && p.grounded && p.landings==2);
 dc_player_init(&p);p.x=-8;
 for(i=0;i<600;++i)dc_player_tick(&p,0,-1,0);
 CHECK(p.z>=-9.721f && p.z<=-9.70f && p.vz==0);
 dc_player_init(&p);dc_player_tick(&p,0.1f,0.1f,0);CHECK(p.x==0 && p.z==0);
 dc_player_tick(&p,INFINITY,0,0);CHECK(p.ticks==1);
 if(count)*count=total;
 return failed;
}
