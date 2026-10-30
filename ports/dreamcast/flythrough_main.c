/* A camera path through a level: the terrain window retargeted every frame
 * under the resident instance pass, every frame checked against the host
 * (AI-assisted), ISC. */
#include <kos.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "render_base.h"
#include "instance_pass.h"
#include "build/flythrough_fixture.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct {uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
typedef struct {uint32_t frames,retargets,max_faces,max_instance_triangles,instance_submitted,terrain_triangles,
 max_frame_us,total_us,last_frame,textured_materials;} fly_metrics;
volatile fly_metrics dc_fly_metrics;
static int check(int ok,const char *why) {
 ++dc_backend.total;if(ok){++dc_backend.passed;return 1;}
 if(!dc_backend.failed_case){dc_backend.failed_case=dc_backend.total;snprintf((char *)dc_backend.description,80,"%s",why?why:"flythrough failure");}
 return 0;
}
int main(void) {
 static dc_instance_material materials[FLY_MATERIAL_SLOTS];
 dc_render_catalog catalog={0};dc_render_gpu gpu={0};dc_render_window window={0};
 dc_render_base_bank bank={0};dc_render_base_frame frame={0};
 dc_instance_package pkg;unsigned char *data=NULL;float (*view)[3]=NULL;
 FILE *file=NULL;const char *error;int initialized=0;uint32_t f,slot,textured=0,windows_ok=1,instances_ok=1,lods_ok=1;
 enum {VERTEX_BUFFER=2048*1024};
 pvr_init_params_t params={.opb_sizes={PVR_BINSIZE_32,0,0,0,PVR_BINSIZE_32},.vertex_buf_size=VERTEX_BUFFER,.opb_overflow_count=8};
 dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;vid_set_mode(DM_640x480,PM_RGB565);
 if(!check(pvr_init(&params)==0,"fly PVR init"))goto release;
 initialized=1;pvr_set_bg_color(.45f,.62f,.80f);
 file=fopen("/rd/manifest.drm","rb");if(!check(file!=NULL,"fly manifest open"))goto release;
 error=dc_render_catalog_load(file,&catalog);if(!check(error==NULL,error))goto release;
 fclose(file);file=NULL;
 error=dc_render_gpu_make(&catalog,&gpu);if(!check(error==NULL,error))goto release;
 error=dc_render_base_bank_make(&catalog,&gpu,&bank);if(!check(error==NULL,error))goto release;
 data=memalign(32,FLY_PACKAGE_BYTES);if(!check(data!=NULL,"fly package allocation"))goto release;
 file=fopen("/rd/package.dci","rb");if(!check(file!=NULL,"fly package open"))goto release;
 if(!check(fread(data,1,FLY_PACKAGE_BYTES,file)==FLY_PACKAGE_BYTES&&fgetc(file)==EOF,"fly package size"))goto release;
 fclose(file);file=NULL;
 if(!check(crc32(0,data,FLY_PACKAGE_BYTES)==FLY_PACKAGE_CRC,"fly package CRC"))goto release;
 error=dc_instance_package_open(data,FLY_PACKAGE_BYTES,&pkg);if(!check(error==NULL,error))goto release;
 view=memalign(32,(size_t)pkg.max_proto_verts*sizeof *view);if(!check(view!=NULL,"fly view scratch"))goto release;
 {int inside=1;
  for(slot=0;slot<FLY_MATERIAL_SLOTS;++slot){uint16_t c=fly_material_map[slot];
   if(c==0xffffu)continue;
   if(c>=bank.count){inside=0;break;}
   materials[slot]=(dc_instance_material){&bank.headers[c],bank.materials[c].list};++textured;}
  if(!check(inside,"fly material map inside the catalog"))goto release;}
 dc_fly_metrics.textured_materials=textured;
 for(f=0;f<FLY_FRAMES;++f) {
  const fly_frame *e=&fly_frames[f];dc_render_base_stats base={0};dc_instance_stats inst={0};
  dc_render_camera rc={{e->target[0],e->target[1],e->target[2]},fly_yaw,fly_pitch,fly_distance};
  dc_instance_camera ic={e->target[0],e->target[1],e->target[2],fly_yaw,fly_pitch,fly_distance,fly_draw_distance};
  uint64_t start=timer_us_gettime64();int ok;uint32_t us;
  /* the terrain: retarget the window, then a frame owner for it */
  error=dc_render_window_retarget(&catalog,"/rd",fly_source_sha,&e->query,&window);
  if(!check(error==NULL,error))goto release;
  ++dc_fly_metrics.retargets;
  if(window.count!=e->faces||crc32(0,window.faces,window.count*116u)!=e->crc)windows_ok=0;
  dc_render_base_frame_free(&frame);
  error=dc_render_base_frame_make(&window,&bank,&frame);if(!check(error==NULL,error))goto release;
  if(!check(dc_render_base_prepare(&frame,&window,&rc)==1,"fly terrain prepare"))goto release;
  pvr_wait_ready();pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);
  ok=dc_render_base_draw(&bank,&frame,PVR_LIST_OP_POLY,&base);
  ok=ok&&dc_instance_draw_list(&pkg,&ic,view,&inst,PVR_LIST_OP_POLY,materials,FLY_MATERIAL_SLOTS);
  pvr_list_finish();pvr_list_begin(PVR_LIST_PT_POLY);
  ok=ok&&dc_render_base_draw(&bank,&frame,PVR_LIST_PT_POLY,&base);
  ok=ok&&dc_instance_draw_list(&pkg,&ic,view,&inst,PVR_LIST_PT_POLY,materials,FLY_MATERIAL_SLOTS);
  pvr_list_finish();pvr_scene_finish();
  if(!check(ok&&base.invalid==0&&inst.invalid==0,"fly draw"))goto release;
  if(inst.drawn_instances!=e->instances||inst.triangles!=e->triangles)instances_ok=0;
  if(memcmp(inst.lod_instances,e->lods,sizeof e->lods))lods_ok=0;
  us=(uint32_t)(timer_us_gettime64()-start);
  if(us>dc_fly_metrics.max_frame_us)dc_fly_metrics.max_frame_us=us;
  dc_fly_metrics.total_us+=us;
  if(window.count>dc_fly_metrics.max_faces)dc_fly_metrics.max_faces=window.count;
  if(inst.triangles>dc_fly_metrics.max_instance_triangles)dc_fly_metrics.max_instance_triangles=inst.triangles;
  dc_fly_metrics.instance_submitted=inst.submitted;dc_fly_metrics.terrain_triangles=base.triangles;
  dc_fly_metrics.frames=f+1;dc_fly_metrics.last_frame=f;
 }
 /* The display shows the scene before the last one submitted, so the last
  * camera's scene goes once more: the captured frame is then the last frame. */
 if(dc_fly_metrics.frames==FLY_FRAMES) {
  const fly_frame *e=&fly_frames[FLY_FRAMES-1];dc_render_base_stats base={0};dc_instance_stats inst={0};int ok;
  dc_instance_camera ic={e->target[0],e->target[1],e->target[2],fly_yaw,fly_pitch,fly_distance,fly_draw_distance};
  pvr_wait_ready();pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);
  ok=dc_render_base_draw(&bank,&frame,PVR_LIST_OP_POLY,&base);
  ok=ok&&dc_instance_draw_list(&pkg,&ic,view,&inst,PVR_LIST_OP_POLY,materials,FLY_MATERIAL_SLOTS);
  pvr_list_finish();pvr_list_begin(PVR_LIST_PT_POLY);
  ok=ok&&dc_render_base_draw(&bank,&frame,PVR_LIST_PT_POLY,&base);
  ok=ok&&dc_instance_draw_list(&pkg,&ic,view,&inst,PVR_LIST_PT_POLY,materials,FLY_MATERIAL_SLOTS);
  pvr_list_finish();pvr_scene_finish();
  check(ok,"fly last frame held for the display");
 }
 pvr_wait_ready();
 check(windows_ok,"fly every window matches the host's faces and CRC");
 check(instances_ok,"fly every frame's instances and triangles match the host");
 check(lods_ok,"fly every frame's LOD choice matches the host");
 check(dc_fly_metrics.frames==FLY_FRAMES,"fly every frame drawn");
 dc_backend.observed_lo=dc_fly_metrics.frames;dc_backend.observed_hi=dc_fly_metrics.max_faces;
 dc_backend.expected_lo=FLY_FRAMES;dc_backend.expected_hi=dc_fly_metrics.max_instance_triangles;
release:
 if(file)fclose(file);
 if(initialized)pvr_wait_ready();
 if(!dc_backend.failed_case)snprintf((char *)dc_backend.description,80,"Flythrough frames=%u faces<=%u inst<=%u us<=%u",
   (unsigned)dc_fly_metrics.frames,(unsigned)dc_fly_metrics.max_faces,(unsigned)dc_fly_metrics.max_instance_triangles,
   (unsigned)dc_fly_metrics.max_frame_us);
 /* The last frame stays up for the capture; owners stay allocated with it. */
 dc_backend.state=dc_backend.failed_case?3:2;for(;;)thd_sleep(100);return 0;
}
