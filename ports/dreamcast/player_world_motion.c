/* Native prototype controller using bounded capsule sweeps (AI-assisted), ISC. */
#include "player_world_motion.h"
#include "player_motion_internal.h"
#include <math.h>
int dc_player_world_place(dc_player *p,dc_collision_world *w,dc_vec3 start){
 dc_player q;dc_collision_result hit;
 if(!p || !dc_collision_clear(w,start,.28f,1.7f) ||
    !dc_collision_cast(w,start,(dc_vec3){0,-4,0},.28f,1.7f,&hit) || !hit.grounded)return 0;
 dc_player_init(&q);q.x=hit.position.x;q.y=hit.position.y;q.z=hit.position.z;*p=q;return 1;
}
int dc_player_world_tick(dc_player *p,dc_collision_world *w,float ix,float iz,int jump,dc_collision_result *out){
 const float dt=1.0f/60.0f;dc_player q;dc_collision_result hit,snap;dc_vec3 start,motion;float speed;unsigned grounded;
 if(!p||!out||!isfinite(ix)||!isfinite(iz))return 0;
 q=*p;grounded=q.grounded;
 if(!dc_player_drive(&q,ix,iz,jump,&speed))return 0;
 if(!q.grounded)q.vy-=18*dt;else q.vy=0;
 start=(dc_vec3){q.x,q.y,q.z};motion=(dc_vec3){q.vx*dt,q.vy*dt,q.vz*dt};
 if(!dc_collision_move(w,start,motion,.28f,1.7f,&hit))return 0;
 if(hit.ceiling && q.vy>0)q.vy=0;
 q.grounded=q.vy<=0 && hit.grounded;
 if(grounded && q.vy<=0){
  if(!dc_collision_cast(w,hit.position,(dc_vec3){0,-.15f,0},.28f,1.7f,&snap))return 0;
  if(snap.grounded){
   hit.position=snap.position;hit.grounded=1;hit.ground_normal=snap.ground_normal;hit.ground_pat=snap.ground_pat;
   hit.contacts+=snap.contacts;hit.event_bits[0]|=snap.event_bits[0];hit.event_bits[1]|=snap.event_bits[1];q.grounded=1;
  }
  hit.distance_tests+=snap.distance_tests;
 }
 if(q.grounded){q.vy=0;if(!grounded)++q.landings;}
 q.x=hit.position.x;q.y=hit.position.y;q.z=hit.position.z;
 if(hit.contacts){q.vx=(q.x-start.x)/dt;q.vz=(q.z-start.z)/dt;}
 dc_player_finish_tick(&q,speed);*p=q;*out=hit;return 1;
}
