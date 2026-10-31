/* Camera conversion and hostile native-memory inputs (AI-assisted), ISC. */
#include "arena_game.h"
#include "render_draw_window.h"
#include "instance_pass.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static unsigned checks,passed;
#define CHECK(x) do{++checks;if(x)++passed;else fprintf(stderr,"line %u: %s\n",__LINE__,#x);}while(0)
static const dc_game_camera_pose identity={{0,0,0,1},{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
static float bits(uint32_t b){float f;memcpy(&f,&b,4);return f;}
static int near(float a,float b){return fabsf(a-b)<.0001f;}
static void pose(dc_game_camera_pose *p,float yaw,float pitch){
 float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
 *p=identity;
 p->inv[0]=cy;p->inv[2]=sy;p->inv[4]=sp*sy;p->inv[5]=cp;p->inv[6]=-sp*cy;
 p->inv[8]=-cp*sy;p->inv[9]=sp;p->inv[10]=cp*cy;
}
static void conversion(void){
 dc_game_camera c;dc_game_camera_pose p=identity;unsigned i;
 p.trans[0]=4096;p.trans[1]=-8192;p.trans[2]=12288;
 CHECK(dc_arena_game_convert(&p,55,&c));
 CHECK(c.eye[0]==1&&c.eye[1]==-2&&c.eye[2]==3);
 CHECK(c.target[0]==1&&c.target[1]==-2&&c.target[2]==58);
 CHECK(c.yaw==0&&c.pitch==0&&c.distance==55);
 for(i=0;i<4;++i){
  float yaw=(float)i*1.57079632679f,pitch=-.45f,x,y,z;
  pose(&p,yaw,pitch);p.trans[0]=40960;p.trans[1]=81920;p.trans[2]=-122880;
  CHECK(dc_arena_game_convert(&p,55,&c));
  CHECK(near(sinf(c.yaw),sinf(yaw))&&near(cosf(c.yaw),cosf(yaw))&&near(c.pitch,pitch));
  /* A point 8 m forward projects to +2 view units, with the eye at zero.
   * Compute from converted target/angles as the real renderer does. */
  x=c.eye[0]+p.inv[8]*8-c.target[0];y=c.eye[1]+p.inv[9]*8-c.target[1];z=c.eye[2]+p.inv[10]*8-c.target[2];
  CHECK(near((cosf(c.yaw)*x+sinf(c.yaw)*z)*.25f,0));
  CHECK(near((sinf(c.pitch)*sinf(c.yaw)*x+cosf(c.pitch)*y-sinf(c.pitch)*cosf(c.yaw)*z)*.25f,0));
  CHECK(near((-cosf(c.pitch)*sinf(c.yaw)*x+sinf(c.pitch)*y+cosf(c.pitch)*cosf(c.yaw)*z+c.distance)*.25f,2));
  {float point[6]={c.eye[0]+p.inv[8]*8,c.eye[1]+p.inv[9]*8,c.eye[2]+p.inv[10]*8,0,0,0},b[10],v[3];
   dc_render_camera rc={{c.target[0],c.target[1],c.target[2]},c.yaw,c.pitch,c.distance};
   dc_instance_camera ic={c.target[0],c.target[1],c.target[2],c.yaw,c.pitch,c.distance,80};
   dc_view_vertex terrain;dc_render_draw_window w={0};
   w.valid=1;w.vertex_count=1;w.vertices=(unsigned char *)point;w.view=&terrain;
   CHECK(dc_render_draw_window_prepare(&w,&rc));
   dc_instance_view(&ic,b);dc_instance_to_view(b,&ic,point,v);
   CHECK(near(terrain.x,0)&&near(terrain.y,0)&&near(terrain.z,2));
   CHECK(near(v[0],terrain.x)&&near(v[1],terrain.y)&&near(v[2],terrain.z));
  }
 }
 pose(&p,2.2f,.7f);CHECK(dc_arena_game_convert(&p,0,&c));CHECK(near(c.pitch,.7f));
 CHECK(c.target[0]==c.eye[0]&&c.target[1]==c.eye[1]&&c.target[2]==c.eye[2]);
}
static void values(void){
 static const uint32_t bad[]={0x7fc00000,0x7f800000,0xff800000,0x7f7fffff};
 dc_game_camera c,old;dc_game_camera_pose p;unsigned i,k;
 memset(&old,0xa5,sizeof old);
 for(i=0;i<20;++i)for(k=0;k<4;++k){
  p=identity;c=old;if(i<4)p.trans[i]=bits(bad[k]);else p.inv[i-4]=bits(bad[k]);
  CHECK(!dc_arena_game_convert(&p,55,&c));CHECK(!memcmp(&c,&old,sizeof c));
 }
 p=identity;
 CHECK(!dc_arena_game_convert(NULL,0,&c));CHECK(!dc_arena_game_convert(&p,0,NULL));
 CHECK(!dc_arena_game_convert(&p,-1,&c));CHECK(!dc_arena_game_convert(&p,1001,&c));
 CHECK(!dc_arena_game_convert(&p,bits(0x7fc00000),&c));CHECK(!dc_arena_game_convert(&p,bits(0x7f800000),&c));
 p.trans[0]=4096000000.f;CHECK(dc_arena_game_convert(&p,0,&c));
 p.trans[2]=4096000000.f;CHECK(!dc_arena_game_convert(&p,55,&c));
 p=identity;p.trans[0]=4097000000.f;CHECK(!dc_arena_game_convert(&p,0,&c));
 p=identity;p.trans[3]=0;CHECK(!dc_arena_game_convert(&p,0,&c));
 for(i=0;i<16;++i){p=identity;p.inv[i]+=.01f;CHECK(!dc_arena_game_convert(&p,0,&c));}
 p=identity;p.inv[0]=-1;CHECK(!dc_arena_game_convert(&p,0,&c));
 p=identity;p.inv[0]=.8f;p.inv[1]=.6f;p.inv[4]=-.6f;p.inv[5]=.8f;CHECK(!dc_arena_game_convert(&p,0,&c));
 pose(&p,.2f,1.57079632679f);CHECK(!dc_arena_game_convert(&p,0,&c));
}
static unsigned char memory[1024*1024],saved[DC_GAME_CAMERA_BYTES];
static void native_memory(void){
 dc_goal_runtime rt;dc_game_camera_pose p=identity,out,old;uint32_t type,camera,used,tag;unsigned i;
 CHECK(dc_goal_init(&rt,memory,sizeof memory,0x8c200000));
 type=dc_goal_define_type(&rt,"math-camera",rt.basic_type,DC_GAME_CAMERA_BYTES,0,9);
 camera=dc_goal_new_basic(&rt,type,DC_GAME_CAMERA_BYTES);CHECK(type&&camera);
 CHECK(dc_arena_game_write(&rt,camera,type,&p));
 CHECK(dc_arena_game_read(&rt,camera,type,&out));CHECK(!memcmp(&p,&out,sizeof p));
 pose(&p,2.2f,-.45f);p.trans[0]=-1200000;p.trans[1]=23456;p.trans[2]=987654;
 CHECK(dc_arena_game_write(&rt,camera,type,&p));CHECK(dc_arena_game_read(&rt,camera,type,&out));CHECK(!memcmp(&p,&out,sizeof p));
 for(i=0;i<16;++i){float f;memcpy(&f,memory+camera-rt.base+DC_GAME_CAMERA_ROT+4*i,4);CHECK(f==p.inv[(i%4)*4+i/4]);}
 used=rt.used;memset(&old,0xa5,sizeof old);
 {uint32_t bad[]={0,rt.s7,camera+1,camera-4,rt.base-12,rt.base+used+4,0xfffffff4u,0xa5200004u};
  for(i=0;i<sizeof bad/sizeof bad[0];++i){out=old;
   CHECK(!dc_arena_game_read(&rt,bad[i],type,&out));CHECK(!memcmp(&old,&out,sizeof old));}}
 CHECK(!dc_arena_game_read(NULL,camera,type,&out));CHECK(!dc_arena_game_read(&rt,camera,type,NULL));
 CHECK(!dc_arena_game_read(&rt,camera,type+1,&out));CHECK(!dc_arena_game_read(&rt,camera,0xfffffff4u,&out));
 rt.used=camera-4-rt.base+DC_GAME_CAMERA_BYTES-1;CHECK(!dc_arena_game_read(&rt,camera,type,&out));rt.used=used;
 rt.capacity=used-1;CHECK(!dc_arena_game_read(&rt,camera,type,&out));rt.capacity=sizeof memory;
 rt.error=DC_GOAL_BOUNDS;CHECK(!dc_arena_game_read(&rt,camera,type,&out));CHECK(rt.error==DC_GOAL_BOUNDS);rt.error=DC_GOAL_OK;
 memcpy(&tag,memory+camera-4-rt.base,4);memset(memory+camera-4-rt.base,0,4);
 CHECK(!dc_arena_game_read(&rt,camera,type,&out));memcpy(memory+camera-4-rt.base,&tag,4);
 memcpy(&tag,memory+type-4-rt.base,4);memset(memory+type-4-rt.base,0,4);
 CHECK(!dc_arena_game_read(&rt,camera,type,&out));memcpy(memory+type-4-rt.base,&tag,4);
 {uint16_t size=DC_GAME_CAMERA_BYTES-4;memcpy(memory+type+8-rt.base,&size,2);
  CHECK(!dc_arena_game_read(&rt,camera,type,&out));size=DC_GAME_CAMERA_BYTES;memcpy(memory+type+8-rt.base,&size,2);}
 memcpy(saved,memory+camera-4-rt.base,sizeof saved);
 p.inv[9]=bits(0x7fc00000);CHECK(!dc_arena_game_write(&rt,camera,type,&p));
 CHECK(!memcmp(saved,memory+camera-4-rt.base,sizeof saved));
 CHECK(!dc_arena_game_write(&rt,0xfffffff4u,type,&identity));CHECK(rt.used==used);
 {float nan=bits(0x7fc00000);memcpy(memory+camera+DC_GAME_CAMERA_TRANS-rt.base,&nan,4);out=old;
  CHECK(!dc_arena_game_read(&rt,camera,type,&out));CHECK(!memcmp(&out,&old,sizeof old));}
 CHECK(rt.error==DC_GOAL_OK);
}
int main(int argc,char **argv){
 unsigned group=argc>1?(unsigned)atoi(argv[1]):0;
 if(group==0)conversion();else if(group==1)values();else native_memory();
 printf("arena game %u/%u checks\n",passed,checks);return passed!=checks;
}
