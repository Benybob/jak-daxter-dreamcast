/* Instanced TIE/shrub pass over a staged DCI1 package (AI-assisted), ISC. */
#include <kos.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "instance_pass.h"
#include "build/instance_fixture.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct {uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
typedef struct {uint32_t frames,instances,drawn_instances,triangles,submitted,near_clipped,culled,invalid,frame_us,package_bytes,lod_instances[4];} instance_metrics;
volatile instance_metrics dc_instance_metrics;
/* zlib's CRC-32, which make_instance_fixture.py records (no zlib in this SDK). */
static uint32_t crc_table[256];
static uint32_t crc32(uint32_t crc,const unsigned char *p,size_t n) {
 unsigned i,k;
 if(!crc_table[1])for(i=0;i<256;++i){uint32_t c=i;for(k=0;k<8;++k)c=c&1?0xedb88320u^(c>>1):c>>1;crc_table[i]=c;}
 crc=~crc;while(n--)crc=crc_table[(crc^*p++)&255]^(crc>>8);return ~crc;
}
static int check(int ok,const char *why) {
 ++dc_backend.total;if(ok){++dc_backend.passed;return 1;}
 if(!dc_backend.failed_case){dc_backend.failed_case=dc_backend.total;snprintf((char *)dc_backend.description,80,"%s",why?why:"instance failure");}
 return 0;
}
int main(void) {
 dc_instance_package pkg;dc_instance_stats stats={0};FILE *file=NULL;unsigned char *data=NULL;float (*view)[3]=NULL;
 const char *error;int initialized=0;unsigned frames;
 /* 41k triangles in the densest village1 window: the TA's vertex buffer holds
  * them as strips of three, so it is sized well past render_base's 512 KiB. */
 pvr_init_params_t params={.opb_sizes={PVR_BINSIZE_32,0,0,0,0},.vertex_buf_size=3*1024*1024,.opb_overflow_count=8};
 dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;vid_set_mode(DM_640x480,PM_RGB565);
 if(!check(pvr_init(&params)==0,"instance PVR init"))goto release;
 initialized=1;pvr_set_bg_color(.45f,.62f,.80f);
 data=memalign(32,INSTANCE_PACKAGE_BYTES);if(!check(data!=NULL,"instance package allocation"))goto release;
 file=fopen("/rd/package.dci","rb");if(!check(file!=NULL,"instance package open"))goto release;
 if(!check(fread(data,1,INSTANCE_PACKAGE_BYTES,file)==INSTANCE_PACKAGE_BYTES&&fgetc(file)==EOF,"instance package size"))goto release;
 fclose(file);file=NULL;
 if(!check(crc32(0,data,INSTANCE_PACKAGE_BYTES)==INSTANCE_PACKAGE_CRC,"instance package CRC"))goto release;
 {dc_instance_package untouched;memset(&untouched,0x5a,sizeof untouched);pkg=untouched;
  check(dc_instance_package_open(data,INSTANCE_PACKAGE_BYTES-1,&pkg)!=NULL&&!memcmp(&pkg,&untouched,sizeof pkg),"instance truncated package refused");
  check(dc_instance_package_open(data+2,INSTANCE_PACKAGE_BYTES-2,&pkg)!=NULL,"instance unaligned package refused");}
 error=dc_instance_package_open(data,INSTANCE_PACKAGE_BYTES,&pkg);if(!check(error==NULL,error))goto release;
 if(!check(pkg.proto_count==INSTANCE_PROTOTYPES&&pkg.instance_count==INSTANCE_COUNT&&(pkg.lods!=NULL)==INSTANCE_LODS,"instance package counts"))goto release;
 view=memalign(32,(size_t)pkg.max_proto_verts*sizeof *view);if(!check(view!=NULL,"instance view scratch"))goto release;
 {dc_instance_camera bad=instance_camera;bad.draw_distance=NAN;
  check(dc_instance_draw(&pkg,&bad,view,&stats)==0&&stats.instances==0,"instance invalid camera refused");}
 dc_instance_metrics.package_bytes=INSTANCE_PACKAGE_BYTES;
 for(frames=0;frames<12;++frames) {
  uint64_t start;int ok;pvr_wait_ready();start=timer_us_gettime64();memset(&stats,0,sizeof stats);
  pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);
  ok=dc_instance_draw(&pkg,&instance_camera,view,&stats);
  pvr_list_finish();pvr_scene_finish();
  if(!check(ok,"instance draw"))goto release;
  dc_instance_metrics.frame_us=(uint32_t)(timer_us_gettime64()-start);dc_instance_metrics.frames=frames+1;
 }
 pvr_wait_ready();
 check(stats.instances==INSTANCE_COUNT,"instance every instance visited");
 check(stats.drawn_instances==INSTANCE_EXPECTED_DRAWN,"instance draw-distance selection matches the host");
 check(stats.triangles==INSTANCE_EXPECTED_TRIANGLES,"instance triangles visited match the host");
 check(stats.invalid==0,"instance no invalid clips");
 check(!memcmp(stats.lod_instances,instance_expected_lods,sizeof instance_expected_lods),"instance LOD choice matches the host");
 check(stats.submitted>0,"instance visible triangles submitted");
 check(crc32(0,data,INSTANCE_PACKAGE_BYTES)==INSTANCE_PACKAGE_CRC,"instance package unchanged by drawing");
 dc_instance_metrics.instances=stats.instances;dc_instance_metrics.drawn_instances=stats.drawn_instances;
 dc_instance_metrics.triangles=stats.triangles;dc_instance_metrics.submitted=stats.submitted;
 memcpy((void *)dc_instance_metrics.lod_instances,stats.lod_instances,sizeof stats.lod_instances);
 dc_instance_metrics.near_clipped=stats.near_clipped;dc_instance_metrics.culled=stats.culled;dc_instance_metrics.invalid=stats.invalid;
 dc_backend.observed_lo=stats.drawn_instances;dc_backend.observed_hi=stats.submitted;
 dc_backend.expected_lo=INSTANCE_EXPECTED_DRAWN;dc_backend.expected_hi=INSTANCE_EXPECTED_TRIANGLES;
release:
 if(file)fclose(file);
 if(initialized)pvr_wait_ready();
 if(!dc_backend.failed_case)snprintf((char *)dc_backend.description,80,"Instances drawn=%u tris=%u sent=%u us=%u",
   (unsigned)dc_instance_metrics.drawn_instances,(unsigned)dc_instance_metrics.triangles,(unsigned)dc_instance_metrics.submitted,(unsigned)dc_instance_metrics.frame_us);
 /* The displayed framebuffer stays up for the capture; the package and the
  * view scratch stay allocated with it. */
 dc_backend.state=dc_backend.failed_case?3:2;for(;;)thd_sleep(100);return 0;
}
