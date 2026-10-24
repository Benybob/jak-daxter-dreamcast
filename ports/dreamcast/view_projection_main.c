/* SH4ZAM view/cache state oracle and portable clipping cases (AI-assisted), ISC. */
#include <kos.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sh4zam/shz_matrix.h>
#include <sh4zam/shz_xmtrx.h>
#include "scene_projection.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct{uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
static void check(int ok,const char *why){
 ++dc_backend.total;if(ok){++dc_backend.passed;return;}
 if(!dc_backend.failed_case){dc_backend.failed_case=dc_backend.total;
  snprintf((char *)dc_backend.description,80,"%s",why);}
}
extern int dc_scene_prepare_bad_mode(uint32_t);
unsigned dc_scene_clip_cases(unsigned *);
static struct{float v[3];uint32_t ignored;} input[64];
static struct{uint32_t before[4];dc_render_vertex v[64];uint32_t after[4];} output;
int main(void){
 shz_mat4x4_t seed,prior,after;unsigned i,j,n,bad;uint32_t fpscr,control;
 dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
 __asm__ volatile("sts fpscr,%0":"=r"(fpscr));shz_xmtrx_store_4x4(&prior);
 for(i=0;i<16;++i){uint32_t bits=0x12345678u+i*0x1020304u;memcpy(&seed.elem[i],&bits,4);}
 {const uint32_t special[4]={0x80000000,0x7f800001,0x7fc01234,1};memcpy(seed.elem,special,sizeof special);}
 for(i=0;i<64;++i){
  input[i].v[0]=(float)((int)(i%4)-2)*.2f;input[i].v[1]=(float)((int)((i/4)%4)-2)*.2f;
  input[i].v[2]=(float)((int)(i/16)-2)*.2f;input[i].ignored=0x7fc01234;
 }
 memset(&output,0x6d,sizeof output);
 for(j=0;j<12;++j){
  float yaw=j*.61f-3.1f,pitch=j*(2.6f/11)-1.3f,distance=.25f+(j%4)*1.5f;
  float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
  shz_xmtrx_load_4x4(&seed);
  check(dc_scene_prepare(input,sizeof input[0],output.v,64,yaw,pitch,distance)==1,"prepare returns");
  for(i=0;i<64;++i){
   const float *v=input[i].v;float x=cy*v[0]+sy*v[2],z=-sy*v[0]+cy*v[2],y=cp*v[1]-sp*z;
   float px=0,py=0,inv=0;z=sp*v[1]+cp*z+distance;
   if(z>=DC_SCENE_NEAR){inv=1/z;px=320+390*x*inv;py=260-390*y*inv;}
   check(fabsf(output.v[i].view.x-x)<.00002f,"view x");
   check(fabsf(output.v[i].view.y-y)<.00002f,"view y");
   check(fabsf(output.v[i].view.z-z)<.00002f,"view depth");
   check(isfinite(output.v[i].projected.x)&&fabsf(output.v[i].projected.x-px)<.03f,"cached x");
   check(isfinite(output.v[i].projected.y)&&fabsf(output.v[i].projected.y-py)<.03f,"cached y");
   check(isfinite(output.v[i].projected.z)&&fabsf(output.v[i].projected.z-inv)<.0001f,"cached inverse");
  }
  shz_xmtrx_store_4x4(&after);check(!memcmp(&seed,&after,sizeof seed),"XMTRX bit preservation");
  __asm__ volatile("sts fpscr,%0":"=r"(control));check((control&0x003c0003u)==(fpscr&0x003c0003u),"FPSCR controls");
  check(output.before[0]==0x6d6d6d6d&&output.before[3]==0x6d6d6d6d,"leading fence");
  check(output.after[0]==0x6d6d6d6d&&output.after[3]==0x6d6d6d6d,"trailing fence");
 }
 check(dc_scene_prepare_bad_mode(0x00080000)==0,"PR refusal");
 check(dc_scene_prepare_bad_mode(0x00100000)==0,"SZ refusal");
 check(dc_scene_prepare_bad_mode(0x00200000)==0,"FR refusal");
 check(dc_scene_prepare_bad_mode(0x00008000)==0,"FD refusal before FPSCR");
 check(dc_scene_prepare(NULL,12,NULL,0,0,0,0)==1,"empty prepare");
 shz_xmtrx_store_4x4(&after);check(!memcmp(&seed,&after,sizeof seed),"empty matrix preservation");
 {const float points[3][3]={{0,0,0},{0,0,DC_SCENE_NEAR},{0,0,-1}};dc_render_vertex v[3];
  check(dc_scene_prepare(points,12,v,3,0,0,0)==1,"camera pole accepted without division");
  check(v[0].view.z==0,"zero view depth");check(v[1].view.z==DC_SCENE_NEAR,"plane view depth");
  check(v[2].view.z==-1,"negative view depth");
  check(v[0].projected.x==0&&v[0].projected.y==0&&v[0].projected.z==0,"zero-depth cache masked");
  check(v[2].projected.x==0&&v[2].projected.y==0&&v[2].projected.z==0,"behind cache masked");
  check(v[1].projected.z==20,"near reciprocal");check(v[1].projected.x==320&&v[1].projected.y==260,"plane center");
 }
 bad=dc_scene_clip_cases(&n);dc_backend.total+=n;dc_backend.passed+=n-bad;
 if(bad&&!dc_backend.failed_case)dc_backend.failed_case=dc_backend.total;
 shz_xmtrx_load_4x4(&prior);
 if(!dc_backend.failed_case)snprintf((char *)dc_backend.description,80,"SH4ZAM view/cache controls + 285 analytic clip checks");
 dc_backend.state=dc_backend.failed_case?3:2;for(;;)thd_sleep(100);
 return dc_backend.failed_case?1:0;
}
