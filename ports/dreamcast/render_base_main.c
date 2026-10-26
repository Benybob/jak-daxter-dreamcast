/* Native material base pass: factory rollback + actual raster (AI-assisted). */
#include <kos.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "render_base.h"
#include "build/render_base_fixture.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct {uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
typedef struct {uint32_t frames,faces,triangles,clipped,culled,secondary,cpu_live,cpu_peak,vram_bytes,frame_us;} base_metrics;
volatile base_metrics dc_base_metrics;
typedef struct {void *ptr;size_t bytes;} allocation;
static allocation allocations[16];static uint32_t attempt,fail_at,live,peak,bad_free;
static void *track(void *p,size_t n) {
 unsigned i;if(!p)return NULL;
 for(i=0;i<16;++i)if(!allocations[i].ptr){allocations[i]=(allocation){p,n};live+=(uint32_t)n;if(live>peak)peak=live;return p;}
 free(p);return NULL;
}
void *dc_base_malloc(size_t n){if(++attempt==fail_at)return NULL;return track(malloc(n),n);}
void *dc_base_calloc(size_t n,size_t s){if(++attempt==fail_at)return NULL;return track(calloc(n,s),n*s);}
void *dc_base_memalign(size_t a,size_t n){if(++attempt==fail_at)return NULL;return track(memalign(a,n),n);}
void dc_base_free(void *p) {
 unsigned i;if(!p)return;
 for(i=0;i<16;++i)if(allocations[i].ptr==p){live-=(uint32_t)allocations[i].bytes;allocations[i].ptr=NULL;free(p);return;}
 ++bad_free;
}
static int check(int ok,const char *why) {
 ++dc_backend.total;if(ok){++dc_backend.passed;return 1;}
 if(!dc_backend.failed_case){dc_backend.failed_case=dc_backend.total;snprintf((char *)dc_backend.description,80,"%s",why?why:"base failure");}
 return 0;
}
static int zero(const void *data,size_t size){const unsigned char *p=data;while(size--)if(*p++)return 0;return 1;}
static void fault_cases(const dc_render_catalog *catalog,const dc_render_gpu *gpu,const dc_render_window *window,
                        const dc_render_base_bank *bank,const dc_render_base_frame *frame) {
 unsigned kind,n;dc_render_base_bank before_bank=*bank;dc_render_base_frame before_frame=*frame;
 for(kind=0;kind<2;++kind)for(n=1;n<=2;++n) {
  uint32_t before=live;const char *error;dc_render_base_bank candidate_bank;dc_render_base_frame candidate_frame;
  memset(&candidate_bank,0,sizeof candidate_bank);memset(&candidate_frame,0,sizeof candidate_frame);attempt=0;fail_at=n;
  if(kind==0)error=dc_render_base_bank_make(catalog,gpu,&candidate_bank);
  else error=dc_render_base_frame_make(window,bank,&candidate_frame);
  check(error!=NULL,"base injected allocation refusal");
  check(zero(&candidate_bank,sizeof candidate_bank)&&zero(&candidate_frame,sizeof candidate_frame),"base candidate unchanged");
  check(live==before&&bad_free==0,"base candidate allocation cleanup");
  check(!memcmp(bank,&before_bank,sizeof before_bank)&&!memcmp(frame,&before_frame,sizeof before_frame),"base active owner preserved");
  dc_render_base_bank_free(&candidate_bank);dc_render_base_frame_free(&candidate_frame);
 }
 fail_at=0;attempt=0;
 for(kind=0;kind<2;++kind) {
  uint32_t before=live;const char *error;dc_render_base_bank candidate_bank={0};dc_render_base_frame candidate_frame={0};
  if(kind==0)error=dc_render_base_bank_make(catalog,gpu,&candidate_bank);
  else error=dc_render_base_frame_make(window,bank,&candidate_frame);
  check(error==NULL,"base complete candidate alongside active owner");
  check(live>before&&!memcmp(bank,&before_bank,sizeof before_bank)&&!memcmp(frame,&before_frame,sizeof before_frame),"base full replacement leaves active owner");
  dc_render_base_bank_free(&candidate_bank);dc_render_base_bank_free(&candidate_bank);
  dc_render_base_frame_free(&candidate_frame);dc_render_base_frame_free(&candidate_frame);
  check(live==before&&bad_free==0,"base complete candidate released independently");
 }
}
int main(void) {
 dc_render_catalog catalog={0};dc_render_gpu gpu={0};dc_render_window window={0};
 dc_render_base_bank bank={0};dc_render_base_frame frame={0};dc_render_base_stats stats={0};
 FILE *file=NULL;const char *error;int initialized=0;uint32_t vram_before=0;unsigned frames;
 pvr_init_params_t params={.opb_sizes={PVR_BINSIZE_32,0,0,0,PVR_BINSIZE_32},.vertex_buf_size=512*1024,.opb_overflow_count=3};
 dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;vid_set_mode(DM_640x480,PM_RGB565);
 if(!check(pvr_init(&params)==0,"base PVR init"))goto release;
 initialized=1;
 /* KOS's dlmalloc arena acquires permanent bookkeeping on its first request.
  * Compare two initialized allocator states, not pre-arena versus post-arena. */
 {pvr_ptr_t prime=pvr_mem_malloc(32);if(!check(prime!=NULL,"base VRAM allocator prime"))goto release;pvr_mem_free(prime);}
 vram_before=pvr_mem_available();pvr_set_bg_color(.1f,.2f,.3f);
 file=fopen("/rd/manifest.drm","rb");if(!check(file!=NULL,"base manifest open"))goto release;
 error=dc_render_catalog_load(file,&catalog);if(!check(error==NULL,error))goto release;
 if(!check(fclose(file)==0,"base manifest close")){file=NULL;goto release;}file=NULL;
 error=dc_render_gpu_make(&catalog,&gpu);if(!check(error==NULL,error))goto release;
 error=dc_render_base_bank_make(&catalog,&gpu,&bank);if(!check(error==NULL,error))goto release;
 error=dc_render_window_retarget(&catalog,"/rd",base_source_sha,&base_query,&window);if(!check(error==NULL,error))goto release;
 if(!check(window.count==BASE_EXPECTED_FACES&&crc32(0,window.faces,window.count*116u)==BASE_EXPECTED_CRC,"base original payload oracle"))goto release;
 error=dc_render_base_frame_make(&window,&bank,&frame);if(!check(error==NULL,error))goto release;
 fault_cases(&catalog,&gpu,&window,&bank,&frame);
 if(!check(crc32(0,window.faces,window.count*116u)==BASE_EXPECTED_CRC,"base source unchanged after staging"))goto release;
 if(!check(dc_render_base_prepare(&frame,&window,&base_camera)==1,"base packed world prepare"))goto release;
 {dc_render_camera invalid=base_camera;invalid.target.x=NAN;
  check(dc_render_base_prepare(&frame,&window,&invalid)==0&&frame.ready==0,"base invalid camera invalidates readiness");
  check(dc_render_base_draw(&bank,&frame,PVR_LIST_OP_POLY,&stats)==0&&stats.faces==0,"base stale camera cannot draw");
  check(dc_render_base_prepare(&frame,&window,&base_camera)==1&&frame.ready==1,"base valid camera restores readiness");
 }
 dc_base_metrics.faces=window.count;dc_base_metrics.secondary=bank.secondary_materials;dc_base_metrics.vram_bytes=gpu.bytes;
 for(frames=0;frames<12;++frames) {
  uint64_t start;pvr_wait_ready();start=timer_us_gettime64();memset(&stats,0,sizeof stats);
  pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);
  if(!dc_render_base_draw(&bank,&frame,PVR_LIST_OP_POLY,&stats)){check(0,"base OP draw");pvr_list_finish();pvr_scene_finish();goto release;}
  pvr_list_finish();pvr_list_begin(PVR_LIST_PT_POLY);
  if(!dc_render_base_draw(&bank,&frame,PVR_LIST_PT_POLY,&stats)){check(0,"base PT draw");pvr_list_finish();pvr_scene_finish();goto release;}
  pvr_list_finish();pvr_scene_finish();
  dc_base_metrics.frame_us=(uint32_t)(timer_us_gettime64()-start);dc_base_metrics.frames=frames+1;
 }
 pvr_wait_ready();check(stats.faces==window.count,"base every selected face visited");check(stats.invalid==0,"base no invalid clips");
 check(stats.triangles>0,"base visible triangles submitted");
 dc_base_metrics.triangles=stats.triangles;dc_base_metrics.clipped=stats.clipped;dc_base_metrics.culled=stats.culled;
 dc_backend.observed_lo=window.count;dc_backend.observed_hi=stats.triangles;dc_backend.expected_hi=bank.secondary_materials;
release:
 if(file)fclose(file);
 if(initialized)pvr_wait_ready();
 dc_render_base_frame_free(&frame);dc_render_base_frame_free(&frame);dc_render_base_bank_free(&bank);dc_render_base_bank_free(&bank);
 dc_render_window_free(&window);dc_render_gpu_free(&gpu);dc_render_catalog_free(&catalog);
 check(live==0&&bad_free==0,"base all CPU owners released");
 if(initialized)check(pvr_mem_available()==vram_before,"base borrowed GPU textures released once");
 dc_base_metrics.cpu_live=live;dc_base_metrics.cpu_peak=peak;dc_backend.expected_lo=peak;
 if(!dc_backend.failed_case)snprintf((char *)dc_backend.description,80,"Base pass faces=%u tris=%u clipped=%u secondary=%u",(unsigned)dc_base_metrics.faces,(unsigned)dc_base_metrics.triangles,(unsigned)dc_base_metrics.clipped,(unsigned)dc_base_metrics.secondary);
 /* Displayed framebuffer remains after GPU completion; Lua captures it before
  * terminating the diagnostic. Texture memory has already been released. */
 dc_backend.state=dc_backend.failed_case?3:2;for(;;)thd_sleep(100);return 0;
}
