/* Independent world-math, packed-layout and state oracle (AI-assisted), ISC. */
#include <kos.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sh4zam/shz_matrix.h>
#include <sh4zam/shz_xmtrx.h>
#include "render_world_view.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct {uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
extern int dc_render_world_bad_mode(uint32_t,const dc_render_window *,dc_render_vertex *,const dc_render_camera *);
static void check(int ok,const char *why) {
 ++dc_backend.total;if(ok){++dc_backend.passed;return;}
 if(!dc_backend.failed_case){dc_backend.failed_case=dc_backend.total;snprintf((char *)dc_backend.description,80,"%s",why);}
}
static int close_to(double got,double expected,double absolute,double relative) {
 return isfinite(got)&&fabs(got-expected)<=absolute+relative*fabs(expected);
}
static unsigned char records[64*116] __attribute__((aligned(4))),backup[sizeof records];
static struct {uint32_t before[4];dc_render_vertex v[64*3];uint32_t after[4];} output;
static double ref_cy,ref_sy,ref_cp,ref_sp;
static void reference_camera(const dc_render_camera *camera) {
 ref_cy=cos((double)camera->yaw);ref_sy=sin((double)camera->yaw);
 ref_cp=cos((double)camera->pitch);ref_sp=sin((double)camera->pitch);
}
static void face_make(unsigned char *record,uint32_t id,dc_view_vertex center) {
 uint32_t v;memcpy(record,&id,4);memset(record+4,0,112);
 for(v=0;v<3;++v) {
  float xyz[3]={center.x+(float)((int)(id%8)-4)*.5f+(float)v*.125f,
                center.y+(float)((int)((id/8)%4)-2)*.25f,
                center.z+(float)((int)(id/32)-1)*.75f+(float)v*.125f};
  float uv[2]={.125f*v,.25f};uint32_t argb=UINT32_C(0x7fc01234)+id+v;
  memcpy(record+8+v*36,xyz,12);memcpy(record+20+v*36,uv,8);
  memcpy(record+40+v*36,&argb,4);
 }
}
static void vertex_check(const unsigned char *record,const dc_render_vertex *got,const dc_render_camera *camera) {
 float xyz[3];double x,y,z,rx,ry,rz,inv,px,py,budget;
 memcpy(xyz,record,12);x=((double)xyz[0]-camera->target.x)*.25;
 y=((double)xyz[1]-camera->target.y)*.25;z=((double)xyz[2]-camera->target.z)*.25;
 rx=ref_cy*x+ref_sy*z;rz=-ref_sy*x+ref_cy*z;ry=ref_cp*y-ref_sp*rz;rz=ref_sp*y+ref_cp*rz+camera->distance*.25;
 budget=.000002+.0000002*(fabs(x)+fabs(y)+fabs(z)+camera->distance*.25);
 check(close_to(got->view.x,rx,budget,0),"world view x");
 check(close_to(got->view.y,ry,budget,0),"world view y");
 check(close_to(got->view.z,rz,budget,0),"world view z");
 /* View accuracy is independently checked above. Projection is checked using
  * the returned binary32 depth, including its exact near-plane decision. */
 inv=px=py=0;
 if(got->view.z>=DC_SCENE_NEAR) {inv=1./got->view.z;px=320.+390.*got->view.x*inv;py=260.-390.*got->view.y*inv;}
 check(close_to(got->projected.x,px,.0001,.000002),"world projected x");
 check(close_to(got->projected.y,py,.0001,.000002),"world projected y");
 check(close_to(got->projected.z,inv,.000001,.000002),"world reciprocal");
}
int main(void) {
 shz_mat4x4_t seed,prior,after;uint32_t fpscr,control,i,j,v;dc_render_window window={0};
 dc_render_camera camera={{0,0,0},0,0,8};unsigned char *large=NULL;dc_render_vertex *large_out=NULL;
 dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
 __asm__ volatile("sts fpscr,%0":"=r"(fpscr));shz_xmtrx_store_4x4(&prior);
 for(i=0;i<16;++i){uint32_t bits=0x12345678u+i*0x1020304u;memcpy(&seed.elem[i],&bits,4);}
 {const uint32_t special[4]={0x80000000,0x7f800001,0x7fc01234,1};memcpy(seed.elem,special,sizeof special);}
 window.faces=records;window.count=64;window.valid=window.ready=1;
 for(j=0;j<15;++j) {
  float center=(j%3==0)?0.f:((j%3==1)?999900.f:-999900.f);
  camera=(dc_render_camera){{center,center,center},(float)j*.61f-3.1f,(float)j*(2.6f/11.f)-1.3f,(j%4)*2.f};
  if(j>=12){const float huge_angles[3]={1.e20f,1.e30f,3.402823466e38f};camera.yaw=huge_angles[j-12];camera.pitch=-camera.yaw;}
  reference_camera(&camera);
  for(i=0;i<64;++i)face_make(records+i*116,i,camera.target);
  memcpy(backup,records,sizeof records);memset(&output,0x6d,sizeof output);shz_xmtrx_load_4x4(&seed);
  check(dc_render_world_prepare(&window,output.v,64*3,&camera)==1,"packed prepare accepts");
  for(i=0;i<64;++i)for(v=0;v<3;++v)vertex_check(records+i*116+8+v*36,&output.v[i*3+v],&camera);
  shz_xmtrx_store_4x4(&after);check(!memcmp(&seed,&after,sizeof seed),"world XMTRX bits");
  __asm__ volatile("sts fpscr,%0":"=r"(control));check((control&0x003c0003u)==(fpscr&0x003c0003u),"world FPSCR controls");
  check(!memcmp(records,backup,sizeof records),"packed source unchanged");
  check(output.before[0]==0x6d6d6d6d&&output.before[3]==0x6d6d6d6d,"world leading fence");
  check(output.after[0]==0x6d6d6d6d&&output.after[3]==0x6d6d6d6d,"world trailing fence");
 }
 /* Every rejection must leave all output bytes unchanged. */
 camera=(dc_render_camera){{0,0,0},0,0,8};memset(&output,0x6d,sizeof output);
 check(dc_render_world_prepare(&window,output.v,191,&camera)==0,"world capacity in vertices");
 check(dc_render_world_prepare(NULL,output.v,192,&camera)==0,"world NULL window");
 check(dc_render_world_prepare(&window,NULL,192,&camera)==0,"world NULL output");
 check(dc_render_world_prepare(&window,output.v,192,NULL)==0,"world NULL camera");
 window.valid=0;check(dc_render_world_prepare(&window,output.v,192,&camera)==0,"world uncommitted");window.valid=1;
 {const uint32_t modes[4]={0x00080000,0x00100000,0x00200000,0x00008000};
  window.count=1;
  for(i=0;i<4;++i)check(dc_render_world_bad_mode(modes[i],&window,output.v,&camera)==0,"world mode before C");
  window.count=64;
 }
 {const float bad[4]={NAN,INFINITY,-INFINITY,1000001.f};
  for(i=0;i<4;++i){camera.target.x=bad[i];check(dc_render_world_prepare(&window,output.v,192,&camera)==0,"world invalid target");}
  camera.target.x=0;camera.distance=-1;check(dc_render_world_prepare(&window,output.v,192,&camera)==0,"world negative distance");
  camera.distance=NAN;check(dc_render_world_prepare(&window,output.v,192,&camera)==0,"world NaN distance");
  camera.distance=8;camera.yaw=NAN;check(dc_render_world_prepare(&window,output.v,192,&camera)==0,"world NaN yaw");
  camera.yaw=0;camera.pitch=INFINITY;check(dc_render_world_prepare(&window,output.v,192,&camera)==0,"world infinite pitch");camera.pitch=0;
 }
 {unsigned char *bytes=(unsigned char *)&output;int same=1;for(i=0;i<sizeof output;++i)if(bytes[i]!=0x6d)same=0;check(same,"all rejected writes masked");}
 window.count=0;window.faces=NULL;check(dc_render_world_prepare(&window,NULL,0,&camera)==1,"world known empty");
 shz_xmtrx_store_4x4(&after);check(!memcmp(&seed,&after,sizeof seed),"world empty/rejection matrix");
 /* Exact camera pole and plane, no angular roundoff. */
 window.count=1;window.faces=records;camera=(dc_render_camera){{0,0,0},0,0,0};
 memset(records,0,116);{const float z[3]={0,.2f,-1};for(i=0;i<3;++i)memcpy(records+16+i*36,&z[i],4);}
 check(dc_render_world_prepare(&window,output.v,3,&camera)==1,"world near triplet");
 check(output.v[0].view.z==0&&output.v[0].projected.z==0,"world pole not divided");
 check(output.v[1].view.z==DC_SCENE_NEAR&&output.v[1].projected.z==20,"world near scale meters");
 check(output.v[2].view.z==-.25f&&output.v[2].projected.z==0,"world behind not divided");
 /* Maximum legal window proves packed stride/count and the full output span. */
 large=malloc(DC_RENDER_WINDOW_CAP*116u);large_out=malloc(DC_RENDER_WINDOW_CAP*3u*sizeof(*large_out));
 check(large&&large_out,"world maximum allocations");
 if(large&&large_out) {
  camera=(dc_render_camera){{999900,999900,999900},.73f,-.21f,8};
  reference_camera(&camera);
  for(i=0;i<DC_RENDER_WINDOW_CAP;++i)face_make(large+i*116,i,camera.target);
  window.faces=large;window.count=DC_RENDER_WINDOW_CAP;
  check(dc_render_world_prepare(&window,large_out,DC_RENDER_WINDOW_CAP*3u,&camera)==1,"world maximum accepted");
  for(i=0;i<DC_RENDER_WINDOW_CAP;++i)for(v=0;v<3;++v)vertex_check(large+i*116+8+v*36,&large_out[i*3+v],&camera);
 }
 free(large_out);free(large);shz_xmtrx_load_4x4(&prior);
 dc_backend.observed_lo=DC_RENDER_WINDOW_CAP*3u;dc_backend.observed_hi=DC_RENDER_WINDOW_CAP*3u*sizeof(dc_render_vertex);
 if(!dc_backend.failed_case)snprintf((char *)dc_backend.description,80,"Packed SH4ZAM world view, max24576 vertices, no deinterleave");
 dc_backend.state=dc_backend.failed_case?3:2;for(;;)thd_sleep(100);
 return 0;
}
