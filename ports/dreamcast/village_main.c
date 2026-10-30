/* Terrain and instances in one frame: the cached tfrag base pass under the
 * instanced TIE/shrub pass, from one camera (AI-assisted), ISC. */
#include <kos.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "render_base.h"
#include "instance_pass.h"
#include "build/village_base_fixture.h"
#include "build/village_instance_fixture.h"
#include "build/village_material_map.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct {uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
typedef struct {uint32_t frames,terrain_faces,terrain_triangles,terrain_clipped,instances_drawn,instance_triangles,
 instance_submitted,lod_instances[4],frame_us,vram_bytes,package_bytes,vertex_buffer_peak,textured_materials;} village_metrics;
volatile village_metrics dc_village_metrics;
static int check(int ok,const char *why) {
 ++dc_backend.total;if(ok){++dc_backend.passed;return 1;}
 if(!dc_backend.failed_case){dc_backend.failed_case=dc_backend.total;snprintf((char *)dc_backend.description,80,"%s",why?why:"village failure");}
 return 0;
}
int main(void) {
 dc_render_catalog catalog={0};dc_render_gpu gpu={0};dc_render_window window={0};
 dc_render_base_bank bank={0};dc_render_base_frame frame={0};dc_render_base_stats base={0};
 static dc_instance_material materials[VILLAGE_MATERIAL_SLOTS];uint32_t textured=0,slot;
 dc_instance_package pkg;dc_instance_stats inst={0};unsigned char *data=NULL;float (*view)[3]=NULL;
 FILE *file=NULL;const char *error;int initialized=0;unsigned frames;
 /* the terrain window and about 55k instance triangles; KOS double-buffers the
  * TA vertex buffer, so two of these share 8 MiB with the bins, framebuffers
  * and textures. The peak KOS measures is checked against it below. */
 enum {VERTEX_BUFFER=2048*1024};
 pvr_init_params_t params={.opb_sizes={PVR_BINSIZE_32,0,0,0,PVR_BINSIZE_32},.vertex_buf_size=VERTEX_BUFFER,.opb_overflow_count=8};
 dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;vid_set_mode(DM_640x480,PM_RGB565);
 if(!check(pvr_init(&params)==0,"village PVR init"))goto release;
 initialized=1;pvr_set_bg_color(.45f,.62f,.80f);
 /* the terrain: catalog, resident textures, the window around the target */
 file=fopen("/rd/manifest.drm","rb");if(!check(file!=NULL,"village manifest open"))goto release;
 error=dc_render_catalog_load(file,&catalog);if(!check(error==NULL,error))goto release;
 fclose(file);file=NULL;
 error=dc_render_gpu_make(&catalog,&gpu);if(!check(error==NULL,error))goto release;
 error=dc_render_base_bank_make(&catalog,&gpu,&bank);if(!check(error==NULL,error))goto release;
 error=dc_render_window_retarget(&catalog,"/rd",base_source_sha,&base_query,&window);if(!check(error==NULL,error))goto release;
 if(!check(window.count==BASE_EXPECTED_FACES&&crc32(0,window.faces,window.count*116u)==BASE_EXPECTED_CRC,"village terrain window oracle"))goto release;
 error=dc_render_base_frame_make(&window,&bank,&frame);if(!check(error==NULL,error))goto release;
 if(!check(dc_render_base_prepare(&frame,&window,&base_camera)==1,"village terrain prepare"))goto release;
 /* the instances */
 data=memalign(32,INSTANCE_PACKAGE_BYTES);if(!check(data!=NULL,"village package allocation"))goto release;
 file=fopen("/rd/package.dci","rb");if(!check(file!=NULL,"village package open"))goto release;
 if(!check(fread(data,1,INSTANCE_PACKAGE_BYTES,file)==INSTANCE_PACKAGE_BYTES&&fgetc(file)==EOF,"village package size"))goto release;
 fclose(file);file=NULL;
 if(!check(crc32(0,data,INSTANCE_PACKAGE_BYTES)==INSTANCE_PACKAGE_CRC,"village package CRC"))goto release;
 error=dc_instance_package_open(data,INSTANCE_PACKAGE_BYTES,&pkg);if(!check(error==NULL,error))goto release;
 view=memalign(32,(size_t)pkg.max_proto_verts*sizeof *view);if(!check(view!=NULL,"village view scratch"))goto release;
 /* each instance material's catalog header, from the probes (none: untextured) */
 {int inside=1;
  for(slot=0;slot<VILLAGE_MATERIAL_SLOTS;++slot){uint16_t c=village_material_map[slot];
   if(c==0xffffu)continue;
   if(c>=bank.count){inside=0;break;}
   materials[slot]=(dc_instance_material){&bank.headers[c],bank.materials[c].list};++textured;}
  if(!check(inside,"village material map inside the catalog"))goto release;}
 /* one camera for both passes */
 check(base_camera.target.x==instance_camera.tx&&base_camera.target.y==instance_camera.ty&&base_camera.target.z==instance_camera.tz&&
       base_camera.yaw==instance_camera.yaw&&base_camera.pitch==instance_camera.pitch&&base_camera.distance==instance_camera.distance,
       "village passes share the camera");
 dc_village_metrics.package_bytes=INSTANCE_PACKAGE_BYTES;dc_village_metrics.vram_bytes=gpu.bytes;
 for(frames=0;frames<12;++frames) {
  uint64_t start;int ok;pvr_wait_ready();start=timer_us_gettime64();memset(&base,0,sizeof base);memset(&inst,0,sizeof inst);
  pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);
  ok=dc_render_base_draw(&bank,&frame,PVR_LIST_OP_POLY,&base);
  ok=ok&&dc_instance_draw_list(&pkg,&instance_camera,view,&inst,PVR_LIST_OP_POLY,materials,VILLAGE_MATERIAL_SLOTS);
  pvr_list_finish();pvr_list_begin(PVR_LIST_PT_POLY);
  ok=ok&&dc_render_base_draw(&bank,&frame,PVR_LIST_PT_POLY,&base);
  ok=ok&&dc_instance_draw_list(&pkg,&instance_camera,view,&inst,PVR_LIST_PT_POLY,materials,VILLAGE_MATERIAL_SLOTS);
  pvr_list_finish();pvr_scene_finish();
  if(!check(ok,"village draw"))goto release;
  dc_village_metrics.frame_us=(uint32_t)(timer_us_gettime64()-start);dc_village_metrics.frames=frames+1;
 }
 pvr_wait_ready();
 /* KOS keeps this statistic only for DMA submission, so it reads 0 here; an
  * overflow would show in the frame, which is compared with a reference. */
 {pvr_stats_t s;pvr_get_stats(&s);dc_village_metrics.vertex_buffer_peak=(uint32_t)s.vtx_buffer_used_max;
  check(s.vtx_buffer_used_max<VERTEX_BUFFER,"village TA vertex buffer statistic under its size");}
 check(base.faces==window.count&&base.invalid==0,"village every terrain face visited, no invalid clips");
 check(base.triangles>0,"village terrain triangles submitted");
 check(inst.drawn_instances==INSTANCE_EXPECTED_DRAWN&&inst.triangles==INSTANCE_EXPECTED_TRIANGLES,"village instances match the host");
 check(!memcmp(inst.lod_instances,instance_expected_lods,sizeof instance_expected_lods),"village LOD choice matches the host");
 check(inst.invalid==0&&inst.submitted>0,"village instance triangles submitted");
 dc_village_metrics.terrain_faces=window.count;dc_village_metrics.terrain_triangles=base.triangles;dc_village_metrics.terrain_clipped=base.clipped;
 dc_village_metrics.instances_drawn=inst.drawn_instances;dc_village_metrics.instance_triangles=inst.triangles;
 dc_village_metrics.instance_submitted=inst.submitted;dc_village_metrics.textured_materials=textured;
 memcpy((void *)dc_village_metrics.lod_instances,inst.lod_instances,sizeof inst.lod_instances);
 dc_backend.observed_lo=base.triangles;dc_backend.observed_hi=inst.submitted;
 dc_backend.expected_lo=window.count;dc_backend.expected_hi=INSTANCE_EXPECTED_TRIANGLES;
release:
 if(file)fclose(file);
 if(initialized)pvr_wait_ready();
 if(!dc_backend.failed_case)snprintf((char *)dc_backend.description,80,"Village terrain=%u/%u inst=%u tris=%u us=%u",
   (unsigned)dc_village_metrics.terrain_triangles,(unsigned)dc_village_metrics.terrain_faces,(unsigned)dc_village_metrics.instances_drawn,
   (unsigned)dc_village_metrics.instance_submitted,(unsigned)dc_village_metrics.frame_us);
 /* The framebuffer stays up for the capture; owners stay allocated with it. */
 dc_backend.state=dc_backend.failed_case?3:2;for(;;)thd_sleep(100);return 0;
}
