/* Original affine camera validation; no finite pose producer (AI-assisted), ISC. */
#include "arena_live_math.h"
#include <math.h>
#include <string.h>
int dc_live_bounded(float f,float limit){
 uint32_t b;memcpy(&b,&f,4);return (b&0x7fffffffu)<0x7f800000u && f>=-limit && f<=limit;
}
static int close_lane(float a,float b,float absolute){return fabsf(a-b)<=absolute+fabsf(b)*.000002f;}
int dc_live_convert(const dc_live_pose *p,float distance,dc_game_camera *out){
 dc_game_camera c;float h,m[12],cy,sy,cp,sp;unsigned i,j;
 if(!p||!out||!dc_live_bounded(distance,1000)||distance<0)return 0;
 for(i=0;i<4;++i)if(!dc_live_bounded(p->trans[i],4096000000.f))return 0;
 for(i=0;i<16;++i)if(!dc_live_bounded(p->inv[i],i>=12&&i<15?4096000000.f:1.001f)||
                                !dc_live_bounded(p->rot[i],i>=12&&i<15?12288000000.f:1.001f))return 0;
 if(p->trans[3]!=1||p->inv[15]!=1||p->rot[15]!=1)return 0;
 for(i=0;i<3;++i)if(p->inv[4*i+3]!=0||p->rot[4*i+3]!=0||p->inv[12+i]!=p->trans[i])return 0;
 h=sqrtf(p->inv[8]*p->inv[8]+p->inv[10]*p->inv[10]);if(h<.001f)return 0;
 c.yaw=atan2f(-p->inv[8],p->inv[10]);c.pitch=atan2f(p->inv[9],h);
 cy=cosf(c.yaw);sy=sinf(c.yaw);cp=cosf(c.pitch);sp=sinf(c.pitch);
 {const float r[12]={cy,0,sy,0,sp*sy,cp,-sp*cy,0,-cp*sy,sp,cp*cy,0};memcpy(m,r,sizeof m);}
 /* Reconstruct a proper roll-free basis; reject roll, poles, reflection,
  * scale and shear rather than silently projecting them onto yaw/pitch. */
 for(i=0;i<12;++i)if(fabsf(p->inv[i]-m[i])>.0001f)return 0;
 for(i=0;i<3;++i){
  float t=0;
  for(j=0;j<3;++j){
   if(fabsf(p->rot[4*i+j]-p->inv[4*j+i])>.0001f)return 0;
   t-=p->inv[4*i+j]*p->trans[j];
  }
  if(!close_lane(p->rot[12+i],t,.25f))return 0;
 }
 c.distance=distance;
 for(i=0;i<3;++i){
  c.eye[i]=p->trans[i]/4096.f;c.target[i]=c.eye[i]+p->inv[8+i]*distance;
  if(!dc_live_bounded(c.target[i],1000000.f))return 0;
 }
 *out=c;return 1;
}
static unsigned char *span(dc_goal_runtime *rt,uint32_t p,uint32_t n){
 uint32_t off;
 if(!rt||!rt->memory||rt->error!=DC_GOAL_OK||rt->used>rt->capacity||
    rt->base>UINT32_MAX-rt->capacity||p<rt->base)return NULL;
 off=p-rt->base;return off<=rt->used&&n<=rt->used-off?rt->memory+off:NULL;
}
static unsigned char *camera_bytes(dc_goal_runtime *rt,uint32_t camera,uint32_t type){
 unsigned char *p,*t;uint32_t tag,size;
 if(!rt||camera<4||type<4||(camera&15)!=4||(type&15)!=4)return NULL;
 p=span(rt,camera-4,DC_GAME_CAMERA_BYTES);t=span(rt,type-4,20);if(!p||!t)return NULL;
 memcpy(&tag,t,4);memcpy(&size,t+12,4);
 if(tag!=rt->type_type||(size&65535u)!=DC_GAME_CAMERA_BYTES)return NULL;
 memcpy(&tag,p,4);return tag==type?p+4:NULL;
}
int dc_live_read(dc_goal_runtime *rt,uint32_t camera,uint32_t type,dc_live_pose *out){
 dc_live_pose p;dc_game_camera c;unsigned char *b=camera_bytes(rt,camera,type);
 if(!out||!b)return 0;
 memcpy(p.trans,b+DC_GAME_CAMERA_TRANS,16);memcpy(p.inv,b+DC_GAME_CAMERA_INV,64);memcpy(p.rot,b+DC_GAME_CAMERA_ROT,64);
 if(!dc_live_convert(&p,0,&c))return 0;
 *out=p;return 1;
}
int dc_live_poison(dc_goal_runtime *rt,uint32_t camera,uint32_t type,uint32_t sequence){
 unsigned char *b=camera_bytes(rt,camera,type);unsigned i;
 if(!b||sequence<1||sequence>181)return 0;
 for(i=0;i<36;++i){
  uint32_t q=0x7fc00000u|(sequence<<8)|i;
  unsigned offset=i<4?DC_GAME_CAMERA_TRANS+4*i:i<20?DC_GAME_CAMERA_INV+4*(i-4):DC_GAME_CAMERA_ROT+4*(i-20);
  memcpy(b+offset,&q,4);
 }
 return 1;
}
static int point_matches(const dc_live_pose *p,const float world[3],const float actual[3]){
 unsigned a,j;float delta[3];
 for(j=0;j<3;++j){if(!dc_live_bounded(world[j],1000000.f))return 0;delta[j]=world[j]-p->trans[j]/4096.f;}
 for(a=0;a<3;++a){
  float expected=0;for(j=0;j<3;++j)expected+=p->inv[4*a+j]*delta[j];expected*=.25f;
  if(!dc_live_bounded(actual[a],2000000.f)||!close_lane(actual[a],expected,.002f))return 0;
 }
 return 1;
}
int dc_live_terrain_matches(const dc_live_pose *p,const dc_render_draw_window *w){
 dc_game_camera c;uint32_t i;int off_axis=0;
 if(!w||!w->valid||!w->prepared||!w->vertices||!w->view||w->vertex_count<3||!dc_live_convert(p,0,&c))return 0;
 for(i=0;i<w->vertex_count;++i){
  float world[3],actual[3]={w->view[i].x,w->view[i].y,w->view[i].z};
  memcpy(world,w->vertices+(size_t)i*DC_RENDER_DRAW_VERTEX_BYTES,12);
  if(!point_matches(p,world,actual))return 0;
  if(fabsf(actual[0])>1||fabsf(actual[1])>1)off_axis=1;
 }
 return off_axis;
}
int dc_live_instance_matches(const dc_live_pose *p,const dc_instance_camera *ic){
 dc_game_camera c;float b[10];unsigned i;
 static const float offsets[4][3]={{0,0,0},{7,-3,11},{-4,8,19},{17,1,-6}};
 if(!ic||!dc_live_convert(p,0,&c)||!dc_live_bounded(ic->tx,1000000)||!dc_live_bounded(ic->ty,1000000)||
    !dc_live_bounded(ic->tz,1000000)||!dc_live_bounded(ic->yaw,4)||!dc_live_bounded(ic->pitch,2)||
    !dc_live_bounded(ic->distance,1000)||ic->distance<0||!dc_live_bounded(ic->draw_distance,128)||ic->draw_distance<=0)return 0;
 dc_instance_view(ic,b);
 for(i=0;i<4;++i){float world[3],actual[3];unsigned j;
  for(j=0;j<3;++j)world[j]=c.eye[j]+offsets[i][j];
  dc_instance_to_view(b,ic,world,actual);if(!point_matches(p,world,actual))return 0;
 }
 return 1;
}
int dc_live_camera_matches(const dc_live_pose *p,const dc_game_camera *c){
 dc_instance_camera submitted;unsigned i;float h;
 if(!p||!c||!dc_live_bounded(c->distance,1000)||c->distance<0||
    !dc_live_bounded(c->yaw,3.141593f)||!dc_live_bounded(c->pitch,1.571f))return 0;
 h=sqrtf(p->inv[8]*p->inv[8]+p->inv[10]*p->inv[10]);
 if(fabsf(c->yaw-atan2f(-p->inv[8],p->inv[10]))>.00001f||
    fabsf(c->pitch-atan2f(p->inv[9],h))>.00001f)return 0;
 for(i=0;i<3;++i)if(!dc_live_bounded(c->eye[i],1000000)||
    fabsf(c->eye[i]-p->trans[i]/4096.f)>.0001f||!dc_live_bounded(c->target[i],1000000)||
    fabsf(c->target[i]-(p->trans[i]/4096.f+p->inv[8+i]*c->distance))>.0001f)return 0;
 submitted=(dc_instance_camera){c->target[0],c->target[1],c->target[2],c->yaw,c->pitch,c->distance,80,1};
 return dc_live_instance_matches(p,&submitted);
}
/* Query received by the terrain cache, compared with raw original lanes.
 * Independent of dc_live_convert and of the renderer's target construction. */
int dc_live_query_matches(const dc_live_pose *p,const dc_render_query *q,float half){
 double x,z;
 if(!p||!q||!dc_live_bounded(half,18)||half<=0)return 0;
 x=(double)p->trans[0]/4096.+(double)p->inv[8]*55.;
 z=(double)p->trans[2]/4096.+(double)p->inv[10]*55.;
 return dc_live_bounded((float)q->x0,1000000)&&dc_live_bounded((float)q->x1,1000000)&&
        dc_live_bounded((float)q->z0,1000000)&&dc_live_bounded((float)q->z1,1000000)&&
        fabs(q->x0-(x-half))<.0002&&fabs(q->x1-(x+half))<.0002&&
        fabs(q->z0-(z-half))<.0002&&fabs(q->z1-(z+half))<.0002;
}

unsigned dc_live_delta(const dc_live_pose *a,const dc_live_pose *b){
 dc_game_camera c;unsigned i,delta=0;
 if(!dc_live_convert(a,0,&c)||!dc_live_convert(b,0,&c))return 0;
 for(i=0;i<3;++i)if(fabsf(a->trans[i]-b->trans[i])>.02f)delta|=2;
 for(i=0;i<12;++i)if(fabsf(a->inv[i]-b->inv[i])>.00001f)delta|=4;
 return delta?delta:1;
}
/* cam-states-dbg.gc:68/243/247; pad right-X 207 supplies exactly half
 * analog range. Original sin truncates 65536/turn angles to integers.
 * Two angle quanta plus affine float error cover that quantization, not
 * missing, delayed, doubled, or unrelated motion. PAL adjust can reach 4.8. */
int dc_live_turn_matches(float angle,float ratio){
 const float step=.010471975511966f,tolerance=.00021f;
 return dc_live_bounded(ratio,4.8f)&&ratio>=1.f&&dc_live_bounded(angle,.051f)&&
        fabsf(angle-step*ratio)<=tolerance;
}
