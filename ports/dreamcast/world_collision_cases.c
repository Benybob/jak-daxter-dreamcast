/* Analytic capsule and PAT regressions, no original assets (AI-assisted), ISC. */
#include "world_collision.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned total,failed;
#define CHECK(x) do{++total;if(!(x)){++failed;printf("collision failure line %u\n",(unsigned)__LINE__);}}while(0)
static void quad(dc_world_triangle *t,dc_vec3 a,dc_vec3 b,dc_vec3 c,dc_vec3 d,uint32_t pat){
 t[0]=(dc_world_triangle){a,b,c,pat};t[1]=(dc_world_triangle){a,c,d,pat};
}
static void floor_quad(dc_world_triangle *t,float slope,uint32_t pat){
 quad(t,(dc_vec3){-4,-4*slope,-4},(dc_vec3){-4,-4*slope,4},(dc_vec3){4,4*slope,4},(dc_vec3){4,4*slope,-4},pat);
}
unsigned dc_world_collision_cases(unsigned *count){
 dc_collision_world w={0},prior;dc_world_triangle t[6],bad;dc_collision_result r,before;unsigned i;dc_vec3 p;float speed;
 total=failed=0;floor_quad(t,0,448);
 quad(t+2,(dc_vec3){1,0,-4},(dc_vec3){1,3,-4},(dc_vec3){1,3,4},(dc_vec3){1,0,4},8);
 quad(t+4,(dc_vec3){-4,2.5f,-4},(dc_vec3){4,2.5f,-4},(dc_vec3){4,2.5f,4},(dc_vec3){-4,2.5f,4},0);
 CHECK(dc_collision_build(&w,t,6)==NULL);
 CHECK(dc_collision_move(&w,(dc_vec3){0,.3f,0},(dc_vec3){0,-1,0},.28f,1.7f,&r));
 CHECK(fabsf(r.position.y)<.002f && r.grounded && r.pat==448);
 p=r.position;
 CHECK(dc_collision_move(&w,p,(dc_vec3){2,0,0},.28f,1.7f,&r));
 CHECK(r.position.x>.718f && r.position.x<.721f && r.contacts>0);
 CHECK(dc_collision_move(&w,p,(dc_vec3){2,0,1},.28f,1.7f,&r));
 CHECK(r.position.x<.721f && fabsf(r.position.z-1)<.002f);
 CHECK(dc_collision_move(&w,p,(dc_vec3){0,2,0},.28f,1.7f,&r));
 CHECK(r.ceiling && r.position.y>.798f && r.position.y<.802f);
 CHECK(dc_collision_move(&w,p,(dc_vec3){8,0,0},.28f,1.7f,&r));
 CHECK(r.position.x<.721f);
 CHECK(dc_collision_move(&w,p,(dc_vec3){-.5f,0,0},.28f,1.7f,&r));
 CHECK(fabsf(r.position.x+.5f)<.001f && r.contacts==0);
 memset(&r,0x5a,sizeof r);before=r;
 CHECK(!dc_collision_move(&w,p,(dc_vec3){NAN,0,0},.28f,1.7f,&r));CHECK(!memcmp(&r,&before,sizeof r));
 CHECK(!dc_collision_move(&w,p,(dc_vec3){9,0,0},.28f,1.7f,&r));CHECK(!memcmp(&r,&before,sizeof r));
 CHECK(!dc_collision_move(&w,p,(dc_vec3){0,-1,0},.28f,.2f,&r));CHECK(!memcmp(&r,&before,sizeof r));
 dc_collision_free(&w);CHECK(!w.triangles && !w.count);
 /* A thin mid-body obstacle exercises the full capsule axis, not just feet. */
 quad(t,(dc_vec3){1,.7f,-1},(dc_vec3){1,1,-1},(dc_vec3){1,1,1},(dc_vec3){1,.7f,1},16);
 CHECK(dc_collision_build(&w,t,2)==NULL);
 CHECK(dc_collision_move(&w,(dc_vec3){0,0,0},(dc_vec3){2,0,0},.28f,1.7f,&r));CHECK(r.position.x<.721f);
 dc_collision_free(&w);
 floor_quad(t,0,449);CHECK(dc_collision_build(&w,t,2)==NULL);
 CHECK(dc_collision_move(&w,(dc_vec3){0,1,0},(dc_vec3){0,-2,0},.28f,1.7f,&r));
 CHECK(r.position.y==-1 && !r.contacts && !r.grounded);dc_collision_free(&w);
 floor_quad(t,0,8);CHECK(dc_collision_build(&w,t,2)==NULL);
 CHECK(dc_collision_move(&w,(dc_vec3){0,1,0},(dc_vec3){0,-2,0},.28f,1.7f,&r));
 CHECK(fabsf(r.position.y)<.002f && !r.grounded);dc_collision_free(&w);
 floor_quad(t,.5f,448);CHECK(dc_collision_build(&w,t,2)==NULL);
 CHECK(dc_collision_move(&w,(dc_vec3){0,1,0},(dc_vec3){0,-2,0},.28f,1.7f,&r));
 CHECK(r.grounded && fabsf(r.position.y-(.5f*r.position.x+.28f*(sqrtf(1.25f)-1)))<.003f);p=r.position;
 CHECK(dc_collision_move(&w,p,(dc_vec3){1,0,0},.28f,1.7f,&r));
 CHECK(r.grounded && r.position.x>p.x+.7f && r.position.y>p.y+.35f);
 CHECK(fabsf(r.position.y-(.5f*r.position.x+.28f*(sqrtf(1.25f)-1)))<.003f);
 CHECK(dc_collision_cast(&w,(dc_vec3){0,1,0},(dc_vec3){0,-2,0},.28f,1.7f,&r));
 CHECK(r.grounded && r.position.x==0 && r.position.z==0 && fabsf(r.position.y-(.28f*(sqrtf(1.25f)-1)))<.002f);
 dc_collision_free(&w);floor_quad(t,3,0);CHECK(dc_collision_build(&w,t,2)==NULL);
 CHECK(dc_collision_move(&w,(dc_vec3){0,2,0},(dc_vec3){0,-3,0},.28f,1.7f,&r));CHECK(r.grounded);
 dc_collision_free(&w);floor_quad(t,6,0);CHECK(dc_collision_build(&w,t,2)==NULL);
 CHECK(dc_collision_move(&w,(dc_vec3){0,2,0},(dc_vec3){0,-3,0},.28f,1.7f,&r));CHECK(!r.grounded);
 dc_collision_free(&w);floor_quad(t,.5f,16);CHECK(dc_collision_build(&w,t,2)==NULL);
 CHECK(dc_collision_move(&w,(dc_vec3){0,1,0},(dc_vec3){0,-2,0},.28f,1.7f,&r));CHECK(r.grounded && r.ground_pat==16);
 dc_collision_free(&w);floor_quad(t,1,16);CHECK(dc_collision_build(&w,t,2)==NULL);
 CHECK(dc_collision_move(&w,(dc_vec3){0,1,0},(dc_vec3){0,-2,0},.28f,1.7f,&r));CHECK(!r.grounded);
 dc_collision_free(&w);floor_quad(t,0,448|(1u<<14));CHECK(dc_collision_build(&w,t,2)==NULL);
 CHECK(dc_collision_move(&w,(dc_vec3){0,1,0},(dc_vec3){0,-2,0},.28f,.56f,&r));
 CHECK(r.grounded && ((r.pat>>14)&63)==1 && (r.event_bits[0]&2));dc_collision_free(&w);
 /* Crossing a welded diagonal while tangent must not invent a seam wall. */
 floor_quad(t,.5f,448);CHECK(dc_collision_build(&w,t,2)==NULL);
 p=(dc_vec3){-1,-.5f+.28f*(sqrtf(1.25f)-1)+.0012f,0};
 for(i=0;i<100;++i){CHECK(dc_collision_move(&w,p,(dc_vec3){.02f,.01f,0},.28f,1.7f,&r));p=r.position;CHECK(fabsf(p.z)<.003f);}
 CHECK(fabsf(p.x-1)<.005f && fabsf(p.y-(.5f+.28f*(sqrtf(1.25f)-1)+.0012f))<.005f);dc_collision_free(&w);
 floor_quad(t,0,0);CHECK(dc_collision_build(&w,t,2)==NULL);
 p=(dc_vec3){0,2,0};speed=0;
 for(i=0;i<120;++i){speed-=.3f;CHECK(dc_collision_move(&w,p,(dc_vec3){0,speed/60,0},.28f,1.7f,&r));
  p=r.position;if(r.grounded)speed=0;CHECK(p.y>=0 && isfinite(p.y));}
 CHECK(p.y<.002f);dc_collision_free(&w);
 prior=w;bad=t[0];bad.b=bad.a;CHECK(dc_collision_build(&w,&bad,1)!=NULL);CHECK(!memcmp(&w,&prior,sizeof w));
 bad=t[0];bad.pat=24;CHECK(dc_collision_build(&w,&bad,1)!=NULL);CHECK(!memcmp(&w,&prior,sizeof w));
 bad=t[0];bad.a.x=NAN;CHECK(dc_collision_build(&w,&bad,1)!=NULL);CHECK(!memcmp(&w,&prior,sizeof w));
 {
  dc_world_triangle *many=malloc(1025*sizeof(*many));CHECK(many!=NULL);
  if(many){for(i=0;i<1025;++i)many[i]=t[0];CHECK(dc_collision_build(&w,many,1025)==NULL);
   memset(&r,0x5a,sizeof r);before=r;CHECK(!dc_collision_move(&w,(dc_vec3){0,1,0},(dc_vec3){0,-2,0},.28f,1.7f,&r));
   CHECK(!memcmp(&r,&before,sizeof r));dc_collision_free(&w);free(many);}
 }
 if(count)*count=total;
 return failed;
}
