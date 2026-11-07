/* Geyser Rock drawn from the game's own camera in the level ELF (AI-assisted), ISC.
 * The render libraries arena_live_render.c draws beach with (catalog, base bank,
 * block cache, draw window, instance package and cells), fed each frame by the
 * original *math-camera* (dc_live_read/dc_live_convert) instead of a recorded
 * path. Level files are read from the disc (/cd/GEYSER, make_level_render_assets.py);
 * every renderer allocation is in the slab (compiled with the slab redirect).
 * Skinned Jak and actors: level_models.c. --dump dc_level_draw_metrics 72 */
#ifdef DC_LEVEL_DRAW
#include <kos.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include "goal_runtime.h"
#include "render_base.h"
#include "render_block_cache.h"
#include "instance_cells.h"
#include "arena_live_math.h"
#include "level_models.h"
#include "level_player.h"
#include "level_logic.h"
#include "build/level_render_fixture.h"

#define LEVEL_DRAW_ROOT "/cd/GEYSER"
enum {TARGET_DISTANCE_M=12,DRAW_DISTANCE_M=60,WINDOW_HALF_M=14};
int dc_level_draw_check(uint64_t,uint64_t,const char *);
int dc_level_draw_release_boot(uint32_t *vram_available);
extern volatile uint32_t dc_arena_slab_live,dc_arena_slab_high_water,dc_arena_slab_failures;
volatile struct {
 uint32_t frames,cleared,drawn,slab_high_water,slab_failures,texture_bytes,vram_after_textures,pvr_errors;
 uint32_t max_faces,max_instances,max_triangles,max_vertex_bytes,last_window_faces,last_instances,last_triangles,camera_reads;
 uint32_t walk_frames,walk_travel_cm;
} dc_level_draw_metrics;
static dc_render_catalog catalog;
static dc_render_gpu gpu;
static dc_render_base_bank bank;
static dc_render_block_cache cache;
static dc_render_draw_window window;
static dc_instance_package pkg;
static dc_instance_cells cells;
static dc_instance_material materials[FLY_MATERIAL_SLOTS];
static float (*view)[3],(*bounds)[4];
static int ready;
#define CHECK(a,b,name) do{if(!dc_level_draw_check((uint64_t)(a),(uint64_t)(b),name))return 0;}while(0)
static void pvr_fault(uint32_t code,void *context){(void)code;(void)context;++dc_level_draw_metrics.pvr_errors;}
static uint32_t named(dc_goal_runtime *rt,const char *name){
 enum dc_goal_error prior=rt->error;uint32_t s=dc_goal_find(rt,name),v=s?dc_goal_symbol_value(rt,s):0;
 rt->error=prior;return v;
}
static int initialize(void){
 FILE *file;const char *error;unsigned char *data;uint32_t slot,needed=0;int mapped=1,read,textures=1;
 static const uint32_t events[]={ASIC_EVT_PVR_ISP_OUTOFMEM,ASIC_EVT_PVR_STRIP_HALT,
  ASIC_EVT_PVR_PARAM_OUTOFMEM,ASIC_EVT_PVR_OPB_OUTOFMEM,ASIC_EVT_PVR_TA_INPUT_ERR,ASIC_EVT_PVR_TA_INPUT_OVERFLOW};
 /* The staged path and orbit of the fixture are for make_flythrough_fixture's own
  * checks; this renderer follows the game camera. */
 (void)fly_frames;(void)fly_yaw;(void)fly_pitch;(void)fly_distance;(void)fly_draw_distance;
 if(!dc_level_draw_release_boot(NULL))return 0;
 for(slot=0;slot<sizeof events/sizeof events[0];++slot){
  asic_evt_set_handler(events[slot],pvr_fault,NULL);asic_evt_enable(events[slot],ASIC_IRQ_DEFAULT);
 }
 file=fopen(LEVEL_DRAW_ROOT "/MANIFEST.DRM","rb");
 CHECK(file!=NULL,1,"level draw manifest opens from the disc");
 error=dc_render_catalog_load(file,&catalog);read=fclose(file);
 CHECK(error==NULL && read==0,1,error?error:"level draw manifest closes");
 for(slot=0;slot<catalog.texture_count;++slot){
  dc_render_texture t;
  if(!dc_render_texture_get(&catalog,slot,&t)){textures=0;break;}
  if(t.format!=2u)needed+=(t.bytes+35u)&~31u;  /* KOS PVR rounds each request+4 to 32 */
 }
 CHECK(textures,1,"level draw texture entries");
 CHECK(pvr_mem_available()>=needed+4096u,1,"level draw VRAM fits the DMA loan and Geyser Rock's textures");
 error=dc_render_gpu_make(&catalog,&gpu);CHECK(error==NULL,1,error);
 dc_level_draw_metrics.texture_bytes=gpu.bytes;dc_level_draw_metrics.vram_after_textures=pvr_mem_available();
 error=dc_render_base_bank_make(&catalog,&gpu,&bank);CHECK(error==NULL,1,error);
 error=dc_render_block_cache_make_draw(&catalog,LEVEL_DRAW_ROOT,fly_source_sha,FLY_CACHE_BUDGET+262144u,&cache);
 CHECK(error==NULL,1,error);
 error=dc_render_draw_window_make(&window,FLY_WINDOW_FACES+2048u,FLY_WINDOW_VERTICES+2048u,FLY_WINDOW_REMAP+4096u);
 CHECK(error==NULL,1,error);
 /* The package is read once and kept: the KOS heap holds it, leaving the slab
  * to the block cache, which reads terrain as the camera moves. */
 data=dc_level_heap_alloc(FLY_PACKAGE_BYTES);CHECK(data!=NULL,1,"level draw package KOS heap allocation");
 file=fopen(LEVEL_DRAW_ROOT "/PACKAGE.DCI","rb");CHECK(file!=NULL,1,"level draw package opens from the disc");
 read=fread(data,1,FLY_PACKAGE_BYTES,file)==FLY_PACKAGE_BYTES&&fgetc(file)==EOF&&!ferror(file);
 read=fclose(file)==0&&read;CHECK(read,1,"level draw package size");
 CHECK(crc32(0,data,FLY_PACKAGE_BYTES),FLY_PACKAGE_CRC,"level draw package CRC");
 error=dc_instance_package_open(data,FLY_PACKAGE_BYTES,&pkg);CHECK(error==NULL,1,error);
 error=dc_instance_cells_make(&cells,LEVEL_DRAW_ROOT,16.0,FLY_INSTANCE_RECORDS+128u);CHECK(error==NULL,1,error);
 view=memalign(32,(size_t)pkg.max_proto_verts*sizeof *view);CHECK(view!=NULL,1,"level draw view slab allocation");
 bounds=memalign(32,(size_t)pkg.proto_count*sizeof *bounds);CHECK(bounds!=NULL,1,"level draw bounds slab allocation");
 dc_instance_bounds(&pkg,bounds);pkg.bounds=(const float (*)[4])bounds;
 for(slot=0;slot<FLY_MATERIAL_SLOTS;++slot){
  uint16_t c=fly_material_map[slot];if(c==0xffffu)continue;
  if(c>=bank.count){mapped=0;break;}
  materials[slot]=(dc_instance_material){&bank.headers[c],bank.materials[c].list,!(bank.materials[c].flags&64u)};
 }
 CHECK(mapped,1,"level draw material map inside catalog");
 CHECK(dc_level_models_open(LEVEL_DRAW_ROOT "/MODELS.DSK"),1,"level draw model bundle opens from the disc");
 ready=1;return 1;
}
/* A clear scene while the game camera is not yet valid (boot, early frames). */
static int clear(void){
 CHECK(pvr_wait_ready(),0,"level draw PVR ready for a clear");
 pvr_scene_begin();pvr_set_bg_color(.05f,.1f,.2f);
 CHECK(pvr_scene_finish(),0,"level draw clear submitted");
 ++dc_level_draw_metrics.cleared;return 1;
}
int dc_level_draw_frame(dc_goal_runtime *rt){
 static const int lists[]={PVR_LIST_OP_POLY,PVR_LIST_PT_POLY,PVR_LIST_TR_POLY};
 uint32_t camera=named(rt,"*math-camera*"),type=named(rt,"math-camera"),i;int ok=1;
 dc_live_pose pose;dc_game_camera c;const char *error;dc_render_query query;
 dc_render_base_frame frame={0};dc_render_base_stats base={0};dc_instance_stats inst={0};pvr_stats_t stats;
 dc_render_camera rc;dc_instance_camera ic;
 ++dc_level_draw_metrics.frames;
 dc_level_player_sample(rt);
 dc_level_actors_sample(rt);
 if(!camera || !type || !dc_live_read(rt,camera,type,&pose) || !dc_live_convert(&pose,TARGET_DISTANCE_M,&c))
  {rt->error=DC_GOAL_OK;return clear();}
 ++dc_level_draw_metrics.camera_reads;
 if(!ready && !initialize())return 0;
 rc=(dc_render_camera){{c.target[0],c.target[1],c.target[2]},c.yaw,c.pitch,c.distance};
 ic=(dc_instance_camera){c.target[0],c.target[1],c.target[2],c.yaw,c.pitch,c.distance,DRAW_DISTANCE_M};
 query=(dc_render_query){(double)c.target[0]-WINDOW_HALF_M,(double)c.target[2]-WINDOW_HALF_M,
                         (double)c.target[0]+WINDOW_HALF_M,(double)c.target[2]+WINDOW_HALF_M};
 error=dc_instance_cells_update(&cells,&pkg,ic.tx,ic.tz,ic.draw_distance);CHECK(error==NULL,1,error);
 error=dc_render_block_cache_window(&cache,&catalog,&query,&window);CHECK(error==NULL,1,error);
 error=dc_render_base_frame_make_draw(&window,&bank,&frame);CHECK(error==NULL,1,error);
 CHECK(window.count==0 || dc_render_draw_window_prepare(&window,&rc),1,"level draw terrain prepare");
 dc_level_models_frame(rt,&ic);
 CHECK(pvr_wait_ready(),0,"level draw PVR ready between GOAL frames");
 pvr_scene_begin();pvr_set_bg_color(.45f,.62f,.80f);
 for(i=0;i<sizeof lists/sizeof lists[0];++i){
  ok&=pvr_list_begin(lists[i])==0;
  if(window.count)ok&=dc_render_base_draw(&bank,&frame,lists[i],&base);
  if(lists[i]!=PVR_LIST_TR_POLY)ok&=dc_instance_draw_list(&pkg,&ic,view,&inst,lists[i],materials,FLY_MATERIAL_SLOTS);
  ok&=dc_level_models_draw(rt,&ic,lists[i]);
  ok&=pvr_list_finish()==0;
 }
 ok&=pvr_scene_finish()==0;
 dc_level_models_end_frame();
 CHECK(ok&&base.invalid==0&&inst.invalid==0,1,"level draw frame submitted without invalid geometry");
 CHECK(pvr_wait_ready(),0,"level draw frame TA complete");
 CHECK(pvr_wait_render_done(),0,"level draw frame rendered");
 CHECK(pvr_get_stats(&stats),0,"level draw PVR stats");
 if((uint32_t)stats.vtx_buffer_used_max>dc_level_draw_metrics.max_vertex_bytes)
  dc_level_draw_metrics.max_vertex_bytes=(uint32_t)stats.vtx_buffer_used_max;
 CHECK(dc_level_draw_metrics.pvr_errors,0,"level draw frame PVR errors");
 CHECK(dc_arena_slab_failures,0,"level draw frame slab has no failed requests");
 dc_level_draw_metrics.last_window_faces=window.count;
 dc_level_draw_metrics.last_instances=inst.drawn_instances;dc_level_draw_metrics.last_triangles=inst.triangles;
 if(window.count>dc_level_draw_metrics.max_faces)dc_level_draw_metrics.max_faces=window.count;
 if(inst.drawn_instances>dc_level_draw_metrics.max_instances)dc_level_draw_metrics.max_instances=inst.drawn_instances;
 if(inst.triangles>dc_level_draw_metrics.max_triangles)dc_level_draw_metrics.max_triangles=inst.triangles;
 dc_level_draw_metrics.slab_high_water=dc_arena_slab_high_water;
 ++dc_level_draw_metrics.drawn;return 1;
}
int dc_level_draw_finish(void){
 dc_level_draw_metrics.slab_high_water=dc_arena_slab_high_water;dc_level_draw_metrics.slab_failures=dc_arena_slab_failures;
 CHECK(dc_level_draw_metrics.drawn>0,1,"level draw drew Geyser Rock from the game camera");
 CHECK(dc_level_draw_metrics.max_faces+dc_level_draw_metrics.max_triangles>0,1,"level draw frames contain geometry");
 CHECK(dc_arena_slab_failures,0,"level draw all renderer allocations stayed in the slab");
 CHECK(dc_level_draw_metrics.pvr_errors,0,"level draw no PVR overflow");
 CHECK(dc_level_model_metrics.load_failures,0,"level models load and validate from the bundle");
 CHECK(dc_level_model_metrics.bind_checked>0,1,"level models compared bundle and game bind-poses");
 CHECK(dc_level_model_metrics.bind_error_ppm<1000u,1,"level models bundle joints are the game's joints");
 CHECK(dc_level_model_metrics.jak_frames>0,1,"level models drew Jak from the game's joints");
 CHECK(dc_level_model_metrics.actor_frames>0,1,"level models drew actors from the game's joints");
 CHECK(dc_level_model_metrics.invalid,0,"level models no invalid clip");
 /* Jak walks on Geyser Rock's original collision: the scripted stick takes him
  * from target-stance to target-walk (a go from a trans hook), he covers ground,
  * stays within a meter of his start height (the slope there rises 0.2 m) and is
  * on a surface for most of the walk. */
 {dc_level_walk w;dc_level_player_walk(&w);
  dc_level_draw_metrics.walk_frames=w.walk_frames;dc_level_draw_metrics.walk_travel_cm=(uint32_t)(w.travel*100.f);
  CHECK(w.walk_frames>=40u,1,"level Jak walked from the stick (target-walk)");
  CHECK(w.travel>=2.f,1,"level Jak travelled at least 2 m");
  CHECK(w.below<=1.f && w.above<=1.f,1,"level Jak stayed on the ground (within 1 m of his start height)");
  CHECK(w.walk_onsurf*3u>=w.walk_frames*2u,1,"level Jak on a surface for two thirds of his walk");}
 return 1;
}
#undef CHECK
#endif
