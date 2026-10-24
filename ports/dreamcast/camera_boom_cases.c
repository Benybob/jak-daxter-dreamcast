/* Independent analytic sphere-boom fixtures (AI-assisted), ISC. */
#include "camera_boom.h"
#include "camera_rig.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned total,bad;
unsigned dc_camera_failed_line;
unsigned dc_camera_patch_shortened;
float dc_camera_patch_min;
#define CHECK(x) do{++total;if(!(x)){++bad;if(!dc_camera_failed_line)dc_camera_failed_line=__LINE__;printf("camera failure line %u\n",(unsigned)__LINE__);}}while(0)
static dc_vec3 point(unsigned axis,float d,float a,float b){
 dc_vec3 p={a,b,d};if(axis==0)p=(dc_vec3){d,a,b};else if(axis==1)p=(dc_vec3){a,d,b};return p;
}
static void plane(dc_world_triangle *tri,unsigned axis,float d,uint32_t pat){
 dc_vec3 a=point(axis,d,-32,-32),b=point(axis,d,32,-32),c=point(axis,d,32,32),e=point(axis,d,-32,32);
 tri[0]=(dc_world_triangle){a,b,c,pat};tri[1]=(dc_world_triangle){a,c,e,pat};
}
static void wall_case(unsigned axis,float coord,dc_vec3 direction,float desired,float expected,uint32_t pat){
 dc_collision_world w={0};dc_world_triangle tri[2];float output=-12;dc_vec3 target={0,1,0},eye;unsigned k;
 plane(tri,axis,coord,pat);CHECK(dc_collision_build(&w,tri,2)==NULL);
 CHECK(dc_camera_boom_query(&w,target,direction,desired,&output));
 CHECK(fabsf(output-expected)<.0003f);CHECK(output>=0&&output<=desired);
 /* Independently sample the reconstructed ray for nonpenetration. This does
  * not replace the analytic first-hit distance checks against plane equations. */
 for(k=0;k<=8;++k){float t=output*k/8;
  eye=(dc_vec3){target.x+direction.x*t,target.y+direction.y*t-.12f,target.z+direction.z*t};
  CHECK(dc_collision_clear(&w,eye,.12f,.24f));
 }
 dc_collision_free(&w);
}
unsigned dc_camera_boom_cases(unsigned *count){
 dc_collision_world w={0};dc_world_triangle triangles[2];float out,desired;unsigned i,j;
 const dc_vec3 dirs[7]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{.6f,0,.8f}};
 const float distances[6]={.3f,7.9f,8,10.4f,11,12};
 total=bad=dc_camera_failed_line=0;plane(triangles,0,32,0);CHECK(dc_collision_build(&w,triangles,2)==NULL);
 for(i=0;i<7;++i)for(j=0;j<6;++j){
  desired=distances[j];out=-1;
  CHECK(dc_camera_boom_query(&w,(dc_vec3){0,1,0},dirs[i],desired,&out));CHECK(out==desired);
 }
 for(i=0;i<10;++i){
  dc_vec3 target={0,1,0},dir={1,0,0};float d=10.4f;out=123.5f;w.candidate_count=77;
  if(i==0)d=NAN;else if(i==1)d=INFINITY;else if(i==2)d=.299f;else if(i==3)d=12.001f;
  else if(i==4)dir=(dc_vec3){0};else if(i==5)dir.x=2;else if(i==6)dir.z=NAN;
  else if(i==7)target.x=4097;else if(i==8)target.y=-4096;else target.z=INFINITY;
  CHECK(!dc_camera_boom_query(&w,target,dir,d,&out));CHECK(out==123.5f&&w.candidate_count==77);
 }
 out=123.5f;CHECK(!dc_camera_boom_query(NULL,(dc_vec3){0},dirs[0],10,&out)&&out==123.5f);
 CHECK(!dc_camera_boom_query(&w,(dc_vec3){0},dirs[0],10,NULL));
 CHECK(dc_camera_boom_query(&w,(dc_vec3){0,1,0},(dc_vec3){1.0005f,0,0},10.4f,&out)&&out==10.4f);
 dc_collision_free(&w);
 wall_case(0,1,dirs[0],10.4f,.877f,0); /* wall in first cast */
 wall_case(0,9,dirs[0],12,8.877f,8);  /* wall in second cast, wall PAT mode */
 wall_case(1,0,dirs[3],2,.877f,0);    /* floor, center/feet conversion */
 wall_case(1,2,dirs[2],2,.877f,16);   /* ceiling, obstacle mode */
 wall_case(2,-1,dirs[5],10.4f,.877f,0);
 wall_case(0,1,dirs[6],10.4f,(1-.121f)/.6f-.002f,0);
 wall_case(0,1,dirs[0],10.4f,10.4f,1); /* noentity does not obstruct */
 plane(triangles,0,.05f,0);CHECK(dc_collision_build(&w,triangles,2)==NULL);out=123.5f;
 CHECK(!dc_camera_boom_query(&w,(dc_vec3){0,1,0},dirs[0],10.4f,&out)&&out==123.5f);
 dc_collision_free(&w);
 plane(triangles,0,1,0);CHECK(dc_collision_build(&w,triangles,2)==NULL);
 CHECK(dc_camera_boom_query(&w,(dc_vec3){.88f,1,0},dirs[0],10.4f,&out)&&out==0);
 dc_collision_free(&w);
 /* Overflow in the initial clear and separately in the second cast. Failure
  * cannot publish an apparently safe 7.9m prefix. No native-stack large array. */
 for(j=0;j<2;++j){
  dc_world_triangle *dense=malloc(1025*sizeof(*dense));CHECK(dense!=NULL);if(!dense)continue;
  plane(triangles,j?0:1,j?8.5f:0,0);
  for(i=0;i<1025;++i)dense[i]=triangles[0];
  CHECK(dc_collision_build(&w,dense,1025)==NULL);free(dense);out=123.5f;
  CHECK(!dc_camera_boom_query(&w,(dc_vec3){0,j?1:.121f,0},dirs[0],10.4f,&out));
  CHECK(out==123.5f&&w.failure==1&&w.count==1025&&w.candidate_count==1024);
  dc_collision_free(&w);
 }
 *count=total;return bad;
}

unsigned dc_camera_rig_cases(unsigned *count){
 dc_collision_world w={0};dc_world_triangle tri[2];dc_camera_rig rig={0},saved;
 unsigned k,prior=total,old_bad=bad;float previous;
 plane(tri,0,32,0);CHECK(dc_collision_build(&w,tri,2)==NULL);
 for(k=0;k<64;++k){
  float yaw=k*(6.28318530718f/64),cy=cosf(yaw),sy=sinf(yaw),cp=cosf(-.5f),sp=sinf(-.5f),x,y,z;
  dc_vec3 p={-4,2,-4},v;
  CHECK(dc_camera_rig_update(&rig,&w,(dc_vec3){64,64,64},p,1.8f,yaw,1.0f/60));
  CHECK(rig.target.x==-4&&fabsf(rig.target.y-2.55f)<.00001f&&rig.target.z==-4&&rig.distance==10.4f);
  /* Independent inverse-view equation: eye must transform to camera origin.
   * This detects the incorrect sign/extra scale in the proposal's prose. */
  v=(dc_vec3){(rig.eye.x-rig.target.x)/4,(rig.eye.y-rig.target.y)/4,(rig.eye.z-rig.target.z)/4};
  x=cy*v.x+sy*v.z;z=-sy*v.x+cy*v.z;y=cp*v.y-sp*z;z=sp*v.y+cp*z+rig.distance/4;
  CHECK(fabsf(x)<.00001f&&fabsf(y)<.00001f&&fabsf(z)<.00001f);
  CHECK(rig.eye.y>rig.target.y);
 }
 for(k=0;k<6;++k){dc_vec3 p={0};float floor=0,yaw=0,dt=.01f;saved=rig;
  if(k==0)dt=-.001f;else if(k==1)dt=.051f;else if(k==2)yaw=NAN;
  else if(k==3)floor=INFINITY;else if(k==4)p.y=NAN;else rig.distance=NAN;
  if(k==5)saved=rig;
  CHECK(!dc_camera_rig_update(&rig,&w,(dc_vec3){64,64,64},p,floor,yaw,dt));CHECK(!memcmp(&rig,&saved,sizeof rig));
  if(k==5)rig.distance=10.4f;
 }
 dc_collision_free(&w);
 plane(tri,2,-1,0);CHECK(dc_collision_build(&w,tri,2)==NULL);
 CHECK(dc_camera_rig_update(&rig,&w,(dc_vec3){64,64,64},(dc_vec3){0},0,0,.05f));
 CHECK(rig.distance>.9f&&rig.distance<1.1f&&rig.shortened==1);previous=rig.distance;
 dc_collision_free(&w);plane(tri,2,-32,0);CHECK(dc_collision_build(&w,tri,2)==NULL);
 CHECK(dc_camera_rig_update(&rig,&w,(dc_vec3){64,64,64},(dc_vec3){0},0,0,.05f));
 CHECK(rig.distance>previous&&rig.distance<=previous+.30001f);previous=rig.distance;
 CHECK(dc_camera_rig_update(&rig,&w,(dc_vec3){64,64,64},(dc_vec3){0},0,0,0)&&rig.distance==previous);
 dc_collision_free(&w);plane(tri,0,-.5f,0);CHECK(dc_collision_build(&w,tri,2)==NULL);
 CHECK(dc_camera_rig_update(&rig,&w,(dc_vec3){64,64,64},(dc_vec3){0},0,-1.57079632679f,.05f));
 CHECK(rig.distance<.5f&&rig.distance<previous&&rig.shortened==2);
 dc_collision_free(&w);plane(tri,1,.75f,0);CHECK(dc_collision_build(&w,tri,2)==NULL);
 memset(&rig,0,sizeof rig);
 CHECK(dc_camera_rig_update(&rig,&w,(dc_vec3){64,64,64},(dc_vec3){0,1,0},0,0,.05f));
 CHECK(rig.fallbacks==1&&rig.target.y==1.75f&&rig.distance==10.4f);saved=rig;
 CHECK(!dc_camera_rig_update(&rig,&w,(dc_vec3){64,64,64},(dc_vec3){0},0,0,.05f));CHECK(!memcmp(&rig,&saved,sizeof rig));
 dc_collision_free(&w);plane(tri,0,32,0);CHECK(dc_collision_build(&w,tri,2)==NULL);
 memset(&rig,0,sizeof rig);
 CHECK(dc_camera_rig_update(&rig,&w,(dc_vec3){18,12,18},(dc_vec3){0,8,0},8,0,.05f));
 CHECK(rig.coverage_limited==1&&rig.shortened==0&&rig.distance<10.4f&&rig.eye.y<=11.87701f);
 saved=rig;
 CHECK(!dc_camera_rig_update(&rig,&w,(dc_vec3){0},(dc_vec3){0},0,0,.05f));CHECK(!memcmp(&rig,&saved,sizeof rig));
 dc_collision_free(&w);*count=total-prior;return bad-old_bad;
}

unsigned dc_camera_patch_cases(FILE *f,unsigned *count){
 dc_collision_world w={0};dc_vec3 origin;unsigned k,j,prior=total;float cp=cosf(-.5f),sp=sinf(-.5f);
 unsigned old_bad=bad;float distance;
 dc_camera_patch_shortened=0;dc_camera_patch_min=10.4f;
 CHECK(dc_collision_load(f,&w,&origin)==NULL);
 for(k=0;k<64;++k){
  float yaw=(float)k*(6.28318530718f/64),sy=sinf(yaw),cy=cosf(yaw);
  dc_vec3 target={-4,.644593f,-4},dir={cp*sy,-sp,-cp*cy};
  distance=-1;CHECK(dc_camera_boom_query(&w,target,dir,10.4f,&distance));
  CHECK(isfinite(distance)&&distance>=0&&distance<=10.4f);
  if(distance<10.35f)++dc_camera_patch_shortened;
  if(distance<dc_camera_patch_min)dc_camera_patch_min=distance;
  for(j=0;j<=8;++j){float d=distance*j/8;dc_vec3 feet={target.x+dir.x*d,target.y+dir.y*d-.12f,target.z+dir.z*d};
   CHECK(dc_collision_clear(&w,feet,.12f,.24f));
  }
 }
 dc_collision_free(&w);*count=total-prior;return bad-old_bad;
}
