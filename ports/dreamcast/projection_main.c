/* Native projection accuracy/state and emulator timing oracle (AI-assisted), ISC. */
#include <kos.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
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
extern int dc_scene_projection_bad_mode(uint32_t);
static struct{float v[3];uint32_t ignored;} input[64];
static struct{uint32_t before[4];dc_projected_vertex v[64];uint32_t after[4];} actual;
static dc_projected_vertex reference[64];
int main(void){
 unsigned i,j;shz_mat4x4_t seed,prior,after;uint32_t sr,fpscr,fpscr_after;uint64_t start,scalar_us,shz_us;
 dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
 __asm__ volatile("stc sr,%0":"=r"(sr));__asm__ volatile("sts fpscr,%0":"=r"(fpscr));
 shz_xmtrx_store_4x4(&prior);
 for(i=0;i<16;++i){uint32_t bits=0x12345678u+i*0x1020304u;memcpy(&seed.elem[i],&bits,4);}
 {const uint32_t special[4]={0x80000000u,0x7f800001u,0x7fc01234u,1u};memcpy(seed.elem,special,sizeof special);}
 for(i=0;i<64;++i){float x=(float)((int)(i%4)-2)*0.2f,y=(float)((int)((i/4)%4)-2)*0.2f,z=(float)((int)(i/16)-2)*0.2f;
  input[i].v[0]=x;input[i].v[1]=y;input[i].v[2]=z;input[i].ignored=0x7fc01234;}
 memset(input[0].v,0,sizeof input[0].v);
 for(i=0;i<6;++i){memset(input[58+i].v,0,sizeof input[58+i].v);input[58+i].v[i/2]=(i&1)?-1.0f:1.0f;}
 memset(&actual,0x6d,sizeof actual);
 for(j=0;j<12;++j){float yaw=(float)j*0.61f-3.1f,pitch=(float)j*(2.6f/11.0f)-1.3f,distance=1.5f+(float)(j%4)*1.5f;
  shz_xmtrx_load_4x4(&seed);
  check(dc_scene_project(input,sizeof input[0],actual.v,64,yaw,pitch,distance)==1,"SH4ZAM batch returns");
  check(dc_scene_project_reference(input,sizeof input[0],reference,64,yaw,pitch,distance)==1,"scalar batch returns");
  for(i=0;i<64;++i){
   check(isfinite(actual.v[i].x)&&fabsf(actual.v[i].x-reference[i].x)<0.01f,"projected X tolerance 0.01px");
   check(isfinite(actual.v[i].y)&&fabsf(actual.v[i].y-reference[i].y)<0.01f,"projected Y tolerance 0.01px");
   check(isfinite(actual.v[i].z)&&fabsf(actual.v[i].z-reference[i].z)<0.00001f,"reciprocal depth tolerance");
  }
  shz_xmtrx_store_4x4(&after);check(!memcmp(&seed,&after,sizeof seed),"XMTRX exact preservation");
  __asm__ volatile("sts fpscr,%0":"=r"(fpscr_after));check((fpscr_after&0x003c0003u)==(fpscr&0x003c0003u),"FPU control preserved");
  check(actual.before[0]==0x6d6d6d6d&&actual.before[3]==0x6d6d6d6d,"leading output fence");
  check(actual.after[0]==0x6d6d6d6d&&actual.after[3]==0x6d6d6d6d,"trailing output fence");
 }
 check(dc_scene_projection_bad_mode(0x00080000)==0,"PR rejects before FPU prologue");
 check(dc_scene_projection_bad_mode(0x00100000)==0,"SZ rejects before FPU prologue");
 check(dc_scene_projection_bad_mode(0x00200000)==0,"FR rejects before FPU prologue");
 check(dc_scene_projection_bad_mode(0x00008000)==0,"FD rejects before FPSCR read");
 check(dc_scene_project(NULL,12,NULL,0,0,0,2)==1,"empty batch no memory access");
 shz_xmtrx_store_4x4(&after);check(!memcmp(&seed,&after,sizeof seed),"empty batch preserves XMTRX");
 /* Warm both paths, alternate measurement order, report medians. Emulated time
  * is diagnostic; it is never a pass criterion or a hardware speed claim. */
 dc_scene_project_reference(input,sizeof input[0],reference,64,0.4f,0.3f,2.6f);
 dc_scene_project(input,sizeof input[0],actual.v,64,0.4f,0.3f,2.6f);
 {uint64_t times[2][3],temp;unsigned round,path,order,k;
  for(round=0;round<3;++round)for(order=0;order<2;++order){
   path=order^(round&1u);start=timer_us_gettime64();
   if(path)for(j=0;j<2048;++j)dc_scene_project(input,sizeof input[0],actual.v,64,0.4f,0.3f,2.6f);
   else for(j=0;j<2048;++j)dc_scene_project_reference(input,sizeof input[0],reference,64,0.4f,0.3f,2.6f);
   times[path][round]=timer_us_gettime64()-start;
  }
  for(path=0;path<2;++path)for(j=0;j<3;++j)for(k=j+1;k<3;++k)
   if(times[path][k]<times[path][j]){temp=times[path][k];times[path][k]=times[path][j];times[path][j]=temp;}
  scalar_us=times[0][1];shz_us=times[1][1];
 }
 shz_xmtrx_load_4x4(&prior);
 if(!dc_backend.failed_case)snprintf((char *)dc_backend.description,80,"SH4ZAM vertices=131072 scalar_us=%lu shz_us=%lu",(unsigned long)scalar_us,(unsigned long)shz_us);
 dc_backend.observed_lo=(uint32_t)scalar_us;dc_backend.observed_hi=(uint32_t)shz_us;
 dc_backend.state=dc_backend.failed_case?3:2;
 printf("PROJECTION %lu/%lu %s sr=%lx\n",(unsigned long)dc_backend.passed,(unsigned long)dc_backend.total,(const char*)dc_backend.description,(unsigned long)sr);
 thd_sleep(500);return dc_backend.failed_case?1:0;
}

