/* Fresh-ray collision cap and bounded extension smoothing (AI-assisted), ISC. */
#include "camera_rig.h"
#include <math.h>
static int covered(dc_vec3 target,dc_vec3 dir,dc_vec3 extent,float *request){
 const float p[3]={target.x,target.y,target.z},d[3]={dir.x,dir.y,dir.z},e[3]={extent.x,extent.y,extent.z};
 float limit=DC_CAMERA_DESIRED;unsigned i;
 for(i=0;i<3;++i){float edge,step;
  if(!isfinite(e[i])||e[i]<=DC_CAMERA_BOOM_RADIUS+.003f||e[i]>64)return 0;
  edge=e[i]-DC_CAMERA_BOOM_RADIUS-.003f;
  if(!isfinite(p[i])||p[i]<-edge||p[i]>edge)return 0;
  if(d[i]>0){step=(edge-p[i])/d[i];if(step<limit)limit=step;}
  else if(d[i]<0){step=(-edge-p[i])/d[i];if(step<limit)limit=step;}
 }
 if(limit<DC_CAMERA_BOOM_MIN_METERS)return 0;
 *request=limit;return 1;
}
int dc_camera_rig_update(dc_camera_rig *rig,dc_collision_world *world,dc_vec3 extent,dc_vec3 feet,
                         float floor,float yaw,float dt){
 dc_camera_rig next;dc_vec3 target,direction,eye_feet;float cp,sp,sample,length,request;int fallback=0;
 if(!rig||rig->initialized>1||!isfinite(floor)||!isfinite(yaw)||!isfinite(dt)||dt<0||dt>.05f||
    !isfinite(feet.x)||!isfinite(feet.y)||!isfinite(feet.z))return 0;
 if(rig->initialized&&(!isfinite(rig->distance)||rig->distance<0||rig->distance>DC_CAMERA_DESIRED))return 0;
 cp=cosf(DC_CAMERA_PITCH);sp=sinf(DC_CAMERA_PITCH);
 direction=(dc_vec3){cp*sinf(yaw),-sp,-cp*cosf(yaw)}; /* target -> eye */
 target=(dc_vec3){feet.x,floor+.75f,feet.z};
 if(!covered(target,direction,extent,&request)||!dc_camera_boom_query(world,target,direction,request,&sample)){
  if(fabsf(floor-feet.y)<.0001f)return 0;
  target.y=feet.y+.75f;
  if(!covered(target,direction,extent,&request)||!dc_camera_boom_query(world,target,direction,request,&sample))return 0;
  fallback=1;
 }
 length=sample;
 if(rig->initialized&&length>rig->distance){
  float extend=rig->distance+6*dt;if(length>extend)length=extend;
 }
 next=*rig;next.target=target;next.direction=direction;next.distance=length;next.limit=sample;
 next.eye=(dc_vec3){target.x+direction.x*length,target.y+direction.y*length,target.z+direction.z*length};
 eye_feet=next.eye;eye_feet.y-=DC_CAMERA_BOOM_RADIUS;
 if(!dc_collision_clear(world,eye_feet,DC_CAMERA_BOOM_RADIUS,DC_CAMERA_BOOM_HEIGHT))return 0;
 next.initialized=1;if(sample<request-.01f)++next.shortened;
 if(request<DC_CAMERA_DESIRED-.01f)++next.coverage_limited;
 if(fallback)++next.fallbacks;
 *rig=next;return 1;
}
