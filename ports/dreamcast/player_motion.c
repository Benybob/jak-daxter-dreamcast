/* Portable fixed-step native movement prototype (AI-assisted), ISC. */
#include "player_motion.h"
#include "player_motion_internal.h"
#include <math.h>
#include <string.h>
const dc_test_platform dc_test_platforms[2]={{2,4,-2,2,0.55f},{-5,-3,-1,1,1.0f}};
static float approach(float a,float b,float amount){
 if(a<b)return a+amount<b?a+amount:b;
 return a-amount>b?a-amount:b;
}
static int inside(float x,float z,const dc_test_platform *b,float radius){
 return x>b->x0-radius && x<b->x1+radius && z>b->z0-radius && z<b->z1+radius;
}
static float support(float x,float z,float feet){
 unsigned i;float h=0;
 for(i=0;i<2;++i)if(inside(x,z,&dc_test_platforms[i],0.28f) &&
    dc_test_platforms[i].top<=feet+0.001f && dc_test_platforms[i].top>h)h=dc_test_platforms[i].top;
 return h;
}
void dc_player_init(dc_player *p){if(p){memset(p,0,sizeof(*p));p->grounded=1;}}
int dc_player_drive(dc_player *p,float ix,float iz,int jump,float *desired){
 const float dt=1.0f/60.0f;float mag,speed=0,ax=0,az=0;
 if(!p || !desired || !isfinite(ix) || !isfinite(iz))return 0;
 /* Clamp components before squaring, including very large finite input. */
 ix=fmaxf(-1,fminf(1,ix));iz=fmaxf(-1,fminf(1,iz));mag=sqrtf(ix*ix+iz*iz);
 if(mag>0.18f){
  float amount=(fminf(mag,1)-0.18f)/0.82f;
  speed=amount<=0.55f?amount*(2.2f/0.55f):2.2f+(amount-0.55f)*(3.3f/0.45f);
  ax=ix/mag*speed;az=iz/mag*speed;
 }
 if(jump && !p->jump_held && p->grounded){p->vy=7;p->grounded=0;++p->jumps;}
 p->jump_held=jump!=0;
 p->vx=approach(p->vx,ax,(p->grounded?22.0f:8.0f)*dt);
 p->vz=approach(p->vz,az,(p->grounded?22.0f:8.0f)*dt);
 if(speed>0.05f)p->yaw=atan2f(ix,iz);
 *desired=speed;return 1;
}
void dc_player_finish_tick(dc_player *p,float speed){
 unsigned mode=!p->grounded?DC_PLAYER_AIR:(speed>2.3f?DC_PLAYER_RUN:(speed>0.05f?DC_PLAYER_WALK:DC_PLAYER_IDLE));
 p->anim_time=mode==p->mode?p->anim_time+1.0f/60.0f:0;p->mode=mode;++p->ticks;
}
int dc_player_tick(dc_player *p,float ix,float iz,int jump){
 const float dt=1.0f/60.0f,r=0.28f;float speed,old_y,nx,nz,h;unsigned i;
 if(!p || !isfinite(ix) || !isfinite(iz))return 0;
 h=support(p->x,p->z,p->y);
 if(p->grounded && p->y>h+0.002f)p->grounded=0;
 if(!dc_player_drive(p,ix,iz,jump,&speed))return 0;
 old_y=p->y;
 if(!p->grounded)p->vy-=18*dt;
 p->y+=p->vy*dt;
 nx=p->x+p->vx*dt;nz=p->z+p->vz*dt;
 /* Axis-separated swept faces. Per-tick travel is below radius, preventing
  * tunnelling at the bounded 5.5 units/s speed. Tops are resolved separately. */
 for(i=0;i<2;++i){const dc_test_platform *b=&dc_test_platforms[i];
  if(p->y>=b->top-0.001f || p->y+1.7f<=0)continue;
  if(p->z>b->z0-r && p->z<b->z1+r){
   if(p->x<=b->x0-r && nx>b->x0-r){nx=b->x0-r;p->vx=0;}
   if(p->x>=b->x1+r && nx<b->x1+r){nx=b->x1+r;p->vx=0;}
  }
  if(nx>b->x0-r && nx<b->x1+r){
   if(p->z<=b->z0-r && nz>b->z0-r){nz=b->z0-r;p->vz=0;}
   if(p->z>=b->z1+r && nz<b->z1+r){nz=b->z1+r;p->vz=0;}
  }
 }
 if(nx>10-r){nx=10-r;p->vx=0;}if(nx<-10+r){nx=-10+r;p->vx=0;}
 if(nz>10-r){nz=10-r;p->vz=0;}if(nz<-10+r){nz=-10+r;p->vz=0;}
 p->x=nx;p->z=nz;
 h=support(nx,nz,old_y);
 if(p->vy<=0 && p->y<=h && old_y>=h-0.001f){
  if(!p->grounded)++p->landings;
  p->y=h;p->vy=0;p->grounded=1;
 }else if(p->grounded && p->y>support(nx,nz,p->y)+0.002f)p->grounded=0;
 dc_player_finish_tick(p,speed);return 1;
}
