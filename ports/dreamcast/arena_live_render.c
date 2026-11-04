/* Beach renderer consuming original affine output only (AI-assisted), ISC. */
#ifdef DC_ARENA_LIVE
#include "arena_live.h"
#include "arena_live_assets.h"
#include <kos.h>
#include <malloc.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "render_base.h"
#include "render_block_cache.h"
#include "instance_cells.h"
#include "goal_fiber.h"
#include "build/arena_live_fixture.h"
#include "build/arena_live_assets.h"

/* Level files on the VRAM romdisk mounted at /rd after the boot image is released.
 * verify_backend.py launches this ELF with no disc, so /cd is empty. */
#ifndef DC_ARENA_FLY_ROOT
#define DC_ARENA_FLY_ROOT "/rd"
#endif
#ifdef DC_ARENA_JAK_PLAYER
#include "arena_jak_memory.h"
enum {VERTEX_BYTES=DC_ARENA_FLY_VERTEX_BYTES,SLAB_BYTES=DC_JAK_RENDER_SLAB_BYTES};
#else
enum {VERTEX_BYTES=DC_ARENA_FLY_VERTEX_BYTES,SLAB_BYTES=2621440};
#endif
typedef char arena_fly_path[FLY_FRAMES==DC_LIVE_UPDATES?1:-1];
extern const int romdisk_size;
extern volatile uint32_t dc_arena_slab_live,dc_arena_slab_high_water,dc_arena_slab_failures;
int dc_arena_fly_check(uint64_t,uint64_t,const char *);
int dc_arena_fly_release_boot(uint32_t *);
/* verify_backend.py --dump dc_arena_live_render_metrics 80, all words in this order. */
volatile struct {
 uint32_t frames,checks,passed,last_frame,slab_high_water,slab_live,slab_failures,
 vram_after_boot,vram_after_textures,texture_bytes,boot_bytes,dma_bytes,
 max_faces,max_vertices,max_instances,max_triangles,max_vertex_bytes,pvr_errors,
 cache_peak_bytes,cache_evictions;
} dc_arena_live_render_metrics;
extern volatile struct {
 uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];
} dc_backend;
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
static void sample(void);
static int check(uint64_t actual,uint64_t expected,const char *name){
 sample();
 ++dc_arena_live_render_metrics.checks;
 if(!dc_arena_fly_check(actual,expected,name))return 0;
 ++dc_arena_live_render_metrics.passed;return 1;
}
#define CHECK(a,b,name) do{if(!check((uint64_t)(a),(uint64_t)(b),name))return 0;}while(0)
static void sample(void){
 dc_arena_live_render_metrics.slab_high_water=dc_arena_slab_high_water;
 dc_arena_live_render_metrics.slab_live=dc_arena_slab_live;
 dc_arena_live_render_metrics.slab_failures=dc_arena_slab_failures;
}
/* No libc or PVR work in the error IRQ. Publish failure even if a wait hangs. */
static void pvr_fault(uint32_t code,void *context){
 static const char why[]="arena live PVR overflow/input error (observed = ASIC event)";
 unsigned i;(void)context;++dc_arena_live_render_metrics.pvr_errors;
 if(!dc_backend.failed_case){
  dc_backend.failed_case=12000u+code;dc_backend.observed_lo=code;dc_backend.observed_hi=0;
  dc_backend.expected_lo=dc_backend.expected_hi=0;
  for(i=0;i<sizeof why;++i)dc_backend.description[i]=why[i];
 }
 dc_backend.state=3;
}
static int initialize(void){
 FILE *file;const char *error;unsigned char *data;uint32_t slot,needed=0;int mapped=1,read,textures=1;
 static const uint32_t events[]={ASIC_EVT_PVR_ISP_OUTOFMEM,ASIC_EVT_PVR_STRIP_HALT,
  ASIC_EVT_PVR_PARAM_OUTOFMEM,ASIC_EVT_PVR_OPB_OUTOFMEM,ASIC_EVT_PVR_TA_INPUT_ERR,ASIC_EVT_PVR_TA_INPUT_OVERFLOW};
 dc_arena_live_render_metrics.boot_bytes=(uint32_t)romdisk_size;
 /* EXTENSION_BYTES in arena_coexist.c: DMA payload 3506176 plus the 64-byte type pad. */
 dc_arena_live_render_metrics.dma_bytes=3506240u;
 if(!dc_arena_fly_release_boot((uint32_t *)&dc_arena_live_render_metrics.vram_after_boot))return 0;
 for(slot=0;slot<sizeof events/sizeof events[0];++slot){
  asic_evt_set_handler(events[slot],pvr_fault,NULL);asic_evt_enable(events[slot],ASIC_IRQ_DEFAULT);
 }
 file=fopen(DC_ARENA_FLY_ROOT "/manifest.drm","rb");
 CHECK(file!=NULL,1,"arena live romdisk manifest opens");
 error=dc_render_catalog_load(file,&catalog);read=fclose(file);
 CHECK(error==NULL && read==0,1,error?error:"arena live manifest closes");
 for(slot=0;slot<catalog.texture_count;++slot){
  dc_render_texture t;
  if(!dc_render_texture_get(&catalog,slot,&t)){textures=0;break;}
  /* KOS PVR dlmalloc rounds each request+4 to 32 bytes. Include headroom. */
  if(t.format!=2u)needed+=(t.bytes+35u)&~31u;
 }
 CHECK(textures,1,"arena live texture entries");
 CHECK(pvr_mem_available()>=needed+4096u,1,"arena live VRAM fits DMA and rounded textures");
 error=dc_render_gpu_make(&catalog,&gpu);CHECK(error==NULL,1,error);
 dc_arena_live_render_metrics.texture_bytes=gpu.bytes;
 dc_arena_live_render_metrics.vram_after_textures=pvr_mem_available();
 error=dc_render_base_bank_make(&catalog,&gpu,&bank);CHECK(error==NULL,1,error);
 error=dc_render_block_cache_make_draw(&catalog,DC_ARENA_FLY_ROOT,fly_source_sha,FLY_CACHE_BUDGET+262144u,&cache);
 CHECK(error==NULL,1,error);dc_render_block_cache_probe=sample;
 error=dc_render_draw_window_make(&window,FLY_WINDOW_FACES+2048u,FLY_WINDOW_VERTICES+2048u,FLY_WINDOW_REMAP+4096u);
 CHECK(error==NULL,1,error);
 data=memalign(32,FLY_PACKAGE_BYTES);CHECK(data!=NULL,1,"arena live package slab allocation");
 file=fopen(DC_ARENA_FLY_ROOT "/package.dci","rb");CHECK(file!=NULL,1,"arena live romdisk package opens");
 read=fread(data,1,FLY_PACKAGE_BYTES,file)==FLY_PACKAGE_BYTES&&fgetc(file)==EOF&&!ferror(file);
 read=fclose(file)==0&&read;CHECK(read,1,"arena live package size");
 CHECK(crc32(0,data,FLY_PACKAGE_BYTES),FLY_PACKAGE_CRC,"arena live package CRC");
 error=dc_instance_package_open(data,FLY_PACKAGE_BYTES,&pkg);CHECK(error==NULL,1,error);
 error=dc_instance_cells_make(&cells,DC_ARENA_FLY_ROOT,16.0,FLY_INSTANCE_RECORDS+128u);CHECK(error==NULL,1,error);
 view=memalign(32,(size_t)pkg.max_proto_verts*sizeof *view);CHECK(view!=NULL,1,"arena live view slab allocation");
 bounds=memalign(32,(size_t)pkg.proto_count*sizeof *bounds);CHECK(bounds!=NULL,1,"arena live bounds slab allocation");
 dc_instance_bounds(&pkg,bounds);pkg.bounds=(const float (*)[4])bounds;
 for(slot=0;slot<FLY_MATERIAL_SLOTS;++slot){
  uint16_t c=fly_material_map[slot];if(c==0xffffu)continue;
  if(c>=bank.count){mapped=0;break;}
  materials[slot]=(dc_instance_material){&bank.headers[c],bank.materials[c].list,!(bank.materials[c].flags&64u)};
 }
 CHECK(mapped,1,"arena live material map inside catalog");
 pvr_set_bg_color(.45f,.62f,.80f);ready=1;return 1;
}
int dc_arena_live_initial_transform(float out[8]){
 /* One event argument, validated again against the first original output. */
 return dc_live_initial_transform(fly_yaw,fly_pitch,fly_frames[0].target,fly_distance,out);
}
int dc_arena_fly_frame(void){
 uint32_t f=dc_arena_live_render_metrics.frames,i;int ok=1,status;float half;
 dc_live_pose pose;dc_game_camera c;
 const char *error;dc_render_query query;
 dc_render_base_frame frame={0};dc_render_base_stats base={0};dc_instance_stats inst={0};pvr_stats_t stats;
 dc_render_camera rc;dc_instance_camera ic;
 static const int lists[]={PVR_LIST_OP_POLY,PVR_LIST_PT_POLY,PVR_LIST_TR_POLY};
 status=dc_arena_live_sample(&pose,&c);
 CHECK(status>=0,1,"arena live root accepts only fresh original output or explicit warmup");
 if(status==0){
  CHECK(pvr_wait_ready(),0,"arena live warmup PVR ready");
  pvr_scene_begin();pvr_set_bg_color(.05f,.1f,.2f);
  CHECK(pvr_scene_finish(),0,"arena live warmup clear, no camera fixture draw");return 1;
 }
 half=(float)((fly_frames[0].query.x1-fly_frames[0].query.x0)*.5);
 rc=(dc_render_camera){{c.target[0],c.target[1],c.target[2]},c.yaw,c.pitch,c.distance};
 ic=(dc_instance_camera){c.target[0],c.target[1],c.target[2],c.yaw,c.pitch,c.distance,fly_draw_distance};
 query=(dc_render_query){(double)c.target[0]-half,(double)c.target[2]-half,
                         (double)c.target[0]+half,(double)c.target[2]+half};
 CHECK(dc_live_coverage(&c,&query,live_crop,fly_frames[0].target,half,fly_draw_distance),1,
       "arena live original camera within staged terrain coverage and travel cap");
 CHECK(dc_live_instance_matches(&pose,&ic),1,"arena live actual instance camera transform matches original eye and basis");
 if(!ready){
  if(!initialize()){sample();return 0;}
  CHECK(dc_live_ledger_files(DC_ARENA_FLY_ROOT,live_cell_ledger,sizeof live_cell_ledger/sizeof live_cell_ledger[0]),1,"arena live occupied cell ledger sizes and CRCs match complete staged split");
 }
 error=dc_instance_cells_update(&cells,&pkg,ic.tx,ic.tz,ic.draw_distance);
 CHECK(error==NULL,1,error);
 CHECK(dc_live_ledger_cells(&cells,live_cell_ledger,sizeof live_cell_ledger/sizeof live_cell_ledger[0]),1,"arena live cells match occupied ledger; absent cells must be known empty");
 error=dc_render_block_cache_window(&cache,&catalog,&query,&window);CHECK(error==NULL,1,error);
 CHECK(dc_live_query_matches(&pose,&window.coverage,half),1,"arena live cache coverage matches independent raw GOAL anchor");
 CHECK(window.count>0,1,"arena live camera window contains terrain");
 error=dc_render_base_frame_make_draw(&window,&bank,&frame);CHECK(error==NULL,1,error);
 CHECK(dc_render_draw_window_prepare(&window,&rc),1,"arena live frame terrain prepare");
 CHECK(dc_live_terrain_matches(&pose,&window),1,"arena live terrain view matches GOAL readback basis and eye");
 CHECK(pvr_wait_ready(),0,"arena live PVR ready between GOAL frames");
 pvr_scene_begin();
 for(i=0;i<sizeof lists/sizeof lists[0];++i){
  ok&=pvr_list_begin(lists[i])==0;
  ok&=dc_render_base_draw(&bank,&frame,lists[i],&base);
  if(lists[i]!=PVR_LIST_TR_POLY)ok&=dc_instance_draw_list(&pkg,&ic,view,&inst,lists[i],materials,FLY_MATERIAL_SLOTS);
  ok&=pvr_list_finish()==0;
 }
 ok&=pvr_scene_finish()==0;
 CHECK(ok&&base.invalid==0&&inst.invalid==0,1,"arena live frame submitted without invalid geometry");
 CHECK(inst.drawn_instances>0,1,"arena live camera cells contain instances");
 CHECK(inst.triangles>0,1,"arena live camera draws instance triangles");
 CHECK(dc_arena_live_render_metrics.pvr_errors,0,"arena live frame PVR errors");
 CHECK(pvr_wait_ready(),0,"arena live frame TA complete");
 CHECK(pvr_wait_render_done(),0,"arena live frame rendered");
 CHECK(pvr_get_stats(&stats),0,"arena live frame PVR stats");
 dc_arena_live_render_metrics.max_vertex_bytes=(uint32_t)stats.vtx_buffer_used_max;
 CHECK(stats.vtx_buffer_used_max<=VERTEX_BYTES,1,"arena live frame fits reserved vertex buffer");
 CHECK(dc_arena_live_render_metrics.pvr_errors,0,"arena live rendered frame PVR errors");
 sample();CHECK(dc_arena_slab_failures,0,"arena live frame slab has no failed requests");
 if(window.count>dc_arena_live_render_metrics.max_faces)dc_arena_live_render_metrics.max_faces=window.count;
 if(window.vertex_count>dc_arena_live_render_metrics.max_vertices)dc_arena_live_render_metrics.max_vertices=window.vertex_count;
 if(inst.drawn_instances>dc_arena_live_render_metrics.max_instances)dc_arena_live_render_metrics.max_instances=inst.drawn_instances;
 if(inst.triangles>dc_arena_live_render_metrics.max_triangles)dc_arena_live_render_metrics.max_triangles=inst.triangles;
 dc_arena_live_render_metrics.last_frame=f;++dc_arena_live_render_metrics.frames;
 /* Check the camera parameters actually submitted, after drawing. */
 CHECK(dc_live_instance_matches(&pose,&ic),1,"arena live submitted instance camera matches raw GOAL pose");
 {dc_game_camera submitted={{c.eye[0],c.eye[1],c.eye[2]},{rc.target.x,rc.target.y,rc.target.z},rc.yaw,rc.pitch,rc.distance};
  return dc_arena_live_drawn(&submitted,half,fly_draw_distance);}

}
int dc_arena_fly_finish(void){
 dc_render_block_cache_stats stats;sample();dc_render_block_cache_measure(&cache,&stats);
 dc_arena_live_render_metrics.cache_peak_bytes=stats.peak_bytes;dc_arena_live_render_metrics.cache_evictions=stats.evictions;
 CHECK(dc_arena_live_render_metrics.frames,DC_LIVE_UPDATES,"arena live all original updates draw");
 CHECK(dc_arena_slab_high_water>0&&dc_arena_slab_high_water<=SLAB_BYTES,1,"arena live slab high-water within reserve");
 CHECK(dc_arena_slab_failures,0,"arena live all renderer allocations stayed in slab");
 CHECK(dc_arena_live_render_metrics.pvr_errors,0,"arena live no PVR overflow");
 return 1;
}
#undef CHECK
#endif
