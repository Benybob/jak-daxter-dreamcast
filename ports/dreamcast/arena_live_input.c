/* Recorded synthetic hardware samples, original cpad services (AI-assisted), ISC. */
#include "arena_live.h"
#include <math.h>
#include <string.h>
static unsigned completed_updates;
/* Stages 0..4 retain the original windows; 5..11 add strafe legs and
 * settling. Extra hardware reads within a service never advance recording. */
unsigned dc_live_input_stage(unsigned completed){
 if(completed<16)return 0;
 if(completed<28)return 1;
 if(completed<40)return 2;
 if(completed<44)return 3;
 if(completed<64)return 4;
 if(completed<88)return 5;
 if(completed<100)return 6;
 if(completed<124)return 7;
 if(completed<136)return 8;
 if(completed<160)return 9;
 if(completed<172)return 10;
 return 11;
}
int dc_live_recorded_pad(unsigned completed,int port,dc_goal_pad_state *out){
 dc_goal_pad_state s={0};unsigned stage=dc_live_input_stage(completed);
 if(!out)return 0;
 if(port!=0){*out=s;return 0;}
 /* The captured display ratio is 4: +64 moved .80645 m/update and
  * breached the unchanged 6 m anchor cap on update 26. +52 (hw 180)
  * is four counts above the original dead zone, giving .20161 m/update.
  * Two right-X samples predict 4.8 degrees total at that ratio.
  * Keep every required moving/turning/neutral measurement window intact.
  * See ARENA_LIVE_NOTES.md and the recorded-path host regression. */
 if(stage==1||stage==7||stage==10)s.joyx=52;
 if(stage==5||stage==9)s.joyx=-52;
 if(stage==3 && completed>=42)s.buttons=DC_PAD_RX_POS; /* Right X=207: (207-128-48)/62=.5; no digital/pressure path. */
 *out=s;return 1;
}
void dc_live_input_advance(unsigned completed){completed_updates=completed;}
int dc_arena_live_pad_state(int port,dc_goal_pad_state *out){return dc_live_recorded_pad(completed_updates,port,out);}
int dc_live_initial_transform(float yaw,float pitch,const float target[3],float distance,float out[8]){
 float q[8],cy,sy,cp,sp;unsigned i;
 if(!target||!out||!dc_live_bounded(yaw,3.141593f)||!dc_live_bounded(pitch,1.56f)||
    !dc_live_bounded(distance,1000)||distance<0)return 0;
 for(i=0;i<3;++i)if(!dc_live_bounded(target[i],999000))return 0;
 cy=cosf(yaw*.5f);sy=sinf(yaw*.5f);cp=cosf(pitch*.5f);sp=sinf(pitch*.5f);
 /* Basis R_y(-yaw) R_x(-pitch), matching the renderer/GOAL convention. */
 q[4]=-cy*sp;q[5]=-sy*cp;q[6]=-sy*sp;q[7]=cy*cp;
 q[0]=(target[0]+cosf(pitch)*sinf(yaw)*distance)*4096.f;
 q[1]=(target[1]-sinf(pitch)*distance)*4096.f;
 q[2]=(target[2]-cosf(pitch)*cosf(yaw)*distance)*4096.f;q[3]=1;
 memcpy(out,q,sizeof q);return 1;
}
int dc_live_initial_matches(const dc_live_pose *p,const float q[8]){
 dc_game_camera c;float x,y,z,w,m[12];unsigned i;
 if(!q||!dc_live_convert(p,0,&c))return 0;
 for(i=0;i<8;++i)if(!dc_live_bounded(q[i],i<3?4096000000.f:1.001f))return 0;
 if(q[3]!=1)return 0;
 x=q[4];y=q[5];z=q[6];w=q[7];
 if(fabsf(x*x+y*y+z*z+w*w-1)>.0001f)return 0;
 {const float r[12]={1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
                    2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
                    2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0};memcpy(m,r,sizeof m);}
 for(i=0;i<3;++i)if(fabsf(p->trans[i]-q[i])>1)return 0;
 for(i=0;i<12;++i)if(fabsf(p->inv[i]-m[i])>.0001f)return 0;
 return 1;
}
