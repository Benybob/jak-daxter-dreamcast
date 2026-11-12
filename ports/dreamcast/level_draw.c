/* Geyser Rock drawn from the game's own camera in the level ELF (AI-assisted), ISC.
 * The render libraries arena_live_render.c draws beach with (catalog, base bank,
 * block cache, draw window, instance package and cells), fed each frame by the
 * original *math-camera* (dc_live_read/dc_live_convert) instead of a recorded
 * path. Level files are read from the disc (/cd/GEYSER, make_level_render_assets.py);
 * every renderer allocation is in the slab (compiled with the slab redirect).
 * Skinned Jak and actors: level_models.c. --dump dc_level_draw_metrics 2400 */
#ifdef DC_LEVEL_DRAW
#include <kos.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>
#include <stdarg.h>
#include "goal_runtime.h"
#include "goal_module.h"
#include "render_base.h"
#include "render_block_cache.h"
#include "instance_cells.h"
#include "arena_live_math.h"
#include "level_models.h"
#include "level_player.h"
#include "level_logic.h"
#include "level_str.h"
#include "level_sound.h"
#include "level_prefetch.h"
#include <inttypes.h>
#include "build/level_render_fixture.h"

#define LEVEL_DRAW_ROOT "/cd/GEYSER"
/* Scenery: 45 m instead of TIE's 60, and the LOD planes halved (LOD_BIAS), so
 * the lowest LOD, about a quarter of the triangles, starts at 30 m instead of
 * never (its plane was the draw distance). Draw 47.6 to 32.3 ms in Flycast. */
enum {TARGET_DISTANCE_M=12,DRAW_DISTANCE_M=45,WINDOW_HALF_M=14,WINDOW_MARGIN_M=4,WINDOW_AHEAD_M=10};
#define LOD_BIAS 2.f
int dc_level_draw_check(uint64_t,uint64_t,const char *);
int dc_level_draw_release_boot(uint32_t *vram_available);
extern volatile uint32_t dc_arena_slab_live,dc_arena_slab_high_water,dc_arena_slab_failures;
volatile struct {
 uint32_t frames,cleared,drawn,slab_high_water,slab_failures,texture_bytes,vram_after_textures,pvr_errors;
 uint32_t max_faces,max_instances,max_triangles,max_vertex_bytes,last_window_faces,last_instances,last_triangles,camera_reads;
 uint32_t walk_frames,walk_travel_cm;
 float last_eye[3],last_yaw,last_pitch,pad;  /* the last converted game camera (meters, radians) */
 float eye[192][3];  /* the converted game camera eye, per draw frame */
} dc_level_draw_metrics;
/* Where a draw frame's time goes, microseconds: 0 samples, 1 cells, 2 terrain
 * window, 3 base frame, 4 terrain prepare, 5 models, 6 PVR ready before the
 * scene, 7 opaque list, 8 punch-through list, 9 translucent list, 10 TA
 * complete, 11 render done, 12 the GOAL frame between two draws, 13 the
 * skinned models' share of 7-9 (counted in both), 14 the kernel-dispatcher
 * call alone (dc_level_time_dispatch, part of 12). frame_us is
 * stages 0-11 of each of the first 96 draw frames, goal_us stage 12.
 * --dump dc_level_frame_timing 1128 */
enum {TIME_STAGES=15,TIME_FRAMES=96};
volatile struct {
 uint32_t frames,pad;uint32_t sum_us[TIME_STAGES],max_us[TIME_STAGES];
 uint32_t frame_us[TIME_FRAMES],goal_us[TIME_FRAMES];
 uint32_t submitted[3],triangles[3];  /* instance triangles per list, summed */
 uint16_t code_walks[TIME_FRAMES];    /* code-registry walks in each GOAL frame (goal_module.c) */
 uint32_t lods[4];                    /* instances drawn at each LOD, summed */
} dc_level_frame_timing;
static uint64_t time_at,time_frame_start,time_last_end;
static void time_stage(unsigned stage){
 uint64_t now=timer_us_gettime64();uint32_t us=(uint32_t)(now-time_at);
 dc_level_frame_timing.sum_us[stage]+=us;
 if(us>dc_level_frame_timing.max_us[stage])dc_level_frame_timing.max_us[stage]=us;
 time_at=now;
}
void dc_level_time_dispatch(uint32_t us){
 dc_level_frame_timing.sum_us[14]+=us;
 if(us>dc_level_frame_timing.max_us[14])dc_level_frame_timing.max_us[14]=us;
}
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
/* The package's KOS block, taken at boot while the heap is still one piece
 * (dc_level_draw_reserve); later, freed temporaries can leave no 652 KB run. */
static unsigned char *package_memory;
volatile uint32_t dc_level_reserve_probe[4];  /* free KOS heap, largest block found, sbrk top, reserved */
void dc_level_draw_reserve(void){
 if(package_memory)return;
 package_memory=dc_level_heap_alloc(FLY_PACKAGE_BYTES);
 dc_level_reserve_probe[0]=dc_level_heap_free();dc_level_reserve_probe[3]=package_memory!=NULL;
 {extern void *sbrk(int);dc_level_reserve_probe[2]=(uint32_t)(uintptr_t)sbrk(0);}
 if(!package_memory){uint32_t size=FLY_PACKAGE_BYTES;void *p;
  while(size>4096u && !(p=malloc(size)))size-=size/16u;
  if(size>4096u){dc_level_reserve_probe[1]=size;free(p);}}
}
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
 data=package_memory?package_memory:dc_level_heap_alloc(FLY_PACKAGE_BYTES);package_memory=NULL;
 CHECK(data!=NULL,1,"level draw package KOS heap allocation");
 file=fopen(LEVEL_DRAW_ROOT "/PACKAGE.DCI","rb");CHECK(file!=NULL,1,"level draw package opens from the disc");
 read=fread(data,1,FLY_PACKAGE_BYTES,file)==FLY_PACKAGE_BYTES&&fgetc(file)==EOF&&!ferror(file);
 read=fclose(file)==0&&read;CHECK(read,1,"level draw package size");
 CHECK(crc32(0,data,FLY_PACKAGE_BYTES),FLY_PACKAGE_CRC,"level draw package CRC");
 error=dc_instance_package_open(data,FLY_PACKAGE_BYTES,&pkg);CHECK(error==NULL,1,error);
 error=dc_instance_cells_make(&cells,LEVEL_DRAW_ROOT,16.0,FLY_INSTANCE_RECORDS+128u);CHECK(error==NULL,1,error);
 /* Every instance cell, read once and kept on the KOS heap (cells.pak, 170 KB):
  * walking opened a cell file or two most frames, 17-51 ms each. */
 {long bytes=0;unsigned char *pack;
  file=fopen(LEVEL_DRAW_ROOT "/CELLS.PAK","rb");CHECK(file!=NULL,1,"level draw cell pack opens from the disc");
  read=!fseek(file,0,SEEK_END)&&(bytes=ftell(file))>8&&bytes<1048576&&!fseek(file,0,SEEK_SET);
  pack=read?dc_level_heap_alloc((size_t)bytes):NULL;
  read=pack&&fread(pack,1,(size_t)bytes,file)==(size_t)bytes;
  read=fclose(file)==0&&read;CHECK(read,1,"level draw cell pack read into the KOS heap");
  error=dc_instance_cells_use_pack(&cells,pack,(size_t)bytes);CHECK(error==NULL,1,error);}
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
/* The camera master's slave cameras (+116, +120: ppointers): the distinct states
 * the first one passes through and the frame each first appears.
 * --dump dc_level_slave_probe 224 */
volatile struct { uint32_t count,pad;struct {char state[24];uint32_t frame,pad;} row[7]; } dc_level_slave_probe;
static void slave_sample(dc_goal_runtime *rt){
 enum dc_goal_error prior=rt->error;uint32_t master=named(rt,"*camera*"),pp=0,slave=0,state=0;const unsigned char *q;const char *name;unsigned i,k;
 if(master && master!=rt->s7 && (q=dc_goal_resolve(rt,master+116u,4)))memcpy(&pp,q,4);
 if(pp && pp!=rt->s7 && (q=dc_goal_resolve(rt,pp,4)))memcpy(&slave,q,4);
 if(slave && slave!=rt->s7 && (q=dc_goal_resolve(rt,slave+52u,4)))memcpy(&state,q,4);
 name=state>4u && state!=rt->s7?dc_goal_symbol_name(rt,*(const uint32_t *)dc_goal_resolve(rt,state,4)):NULL;
 rt->error=prior;
 if(!name)return;
 for(i=0;i<dc_level_slave_probe.count;++i)if(!strncmp((const char *)dc_level_slave_probe.row[i].state,name,23))return;
 if(i>=7)return;
 for(k=0;k<23 && name[k];++k)dc_level_slave_probe.row[i].state[k]=name[k];
 dc_level_slave_probe.row[i].frame=dc_level_draw_metrics.frames;dc_level_slave_probe.count=i+1;
}
int dc_level_draw_frame(dc_goal_runtime *rt){
 static const int lists[]={PVR_LIST_OP_POLY,PVR_LIST_PT_POLY,PVR_LIST_TR_POLY};
 uint32_t camera=named(rt,"*math-camera*"),type=named(rt,"math-camera"),i;int ok=1;
 dc_live_pose pose;dc_game_camera c;const char *error;dc_render_query query;
 dc_render_base_frame frame={0};dc_render_base_stats base={0};dc_instance_stats inst={0};pvr_stats_t stats;
 dc_render_camera rc;dc_instance_camera ic;
 ++dc_level_draw_metrics.frames;
 time_frame_start=time_at=timer_us_gettime64();
 dc_level_player_sample(rt);
 dc_level_actors_sample(rt);
 dc_level_str_sample(rt);
 dc_level_sound_frame(rt);
 slave_sample(rt);
 time_stage(0);
 if(!camera || !type || !dc_live_read(rt,camera,type,&pose) || !dc_live_convert(&pose,TARGET_DISTANCE_M,&c))
  {rt->error=DC_GOAL_OK;return clear();}
 ++dc_level_draw_metrics.camera_reads;
 dc_level_draw_metrics.last_eye[0]=c.eye[0];dc_level_draw_metrics.last_eye[1]=c.eye[1];dc_level_draw_metrics.last_eye[2]=c.eye[2];
 dc_level_draw_metrics.last_yaw=c.yaw;dc_level_draw_metrics.last_pitch=c.pitch;
 if(dc_level_draw_metrics.frames<=192u){unsigned k;for(k=0;k<3;++k)dc_level_draw_metrics.eye[dc_level_draw_metrics.frames-1u][k]=c.eye[k];}
 if(!ready && !initialize())return 0;
 rc=(dc_render_camera){{c.target[0],c.target[1],c.target[2]},c.yaw,c.pitch,c.distance};
 ic=(dc_instance_camera){c.target[0],c.target[1],c.target[2],c.yaw,c.pitch,c.distance,DRAW_DISTANCE_M,LOD_BIAS};
 query=(dc_render_query){(double)c.target[0]-WINDOW_HALF_M,(double)c.target[2]-WINDOW_HALF_M,
                         (double)c.target[0]+WINDOW_HALF_M,(double)c.target[2]+WINDOW_HALF_M};
 time_at=timer_us_gettime64();
 error=dc_instance_cells_update(&cells,&pkg,ic.tx,ic.tz,ic.draw_distance);CHECK(error==NULL,1,error);
 time_stage(1);
 /* The window is built WINDOW_MARGIN_M wider than the 14 m asked for and kept
  * while it still covers that: an exact window was merged again on every
  * frame the target moved (about 18 ms), now once every few meters. */
 if(!(window.valid&&window.coverage.x0<=query.x0&&window.coverage.x1>=query.x1&&
      window.coverage.z0<=query.z0&&window.coverage.z1>=query.z1)){
  dc_render_query wide={query.x0-WINDOW_MARGIN_M,query.z0-WINDOW_MARGIN_M,query.x1+WINDOW_MARGIN_M,query.z1+WINDOW_MARGIN_M};
  error=dc_render_block_cache_window(&cache,&catalog,&wide,&window);
 }else error=NULL;
 CHECK(error==NULL,1,error?error:"level draw terrain window");
 /* Read ahead: the sector files of a window WINDOW_AHEAD_M wider still that
  * the cache does not hold go to the read-ahead thread, each time the target
  * has moved a meter, so the next rebuild finds them in memory. */
 {static float ahead_x=1e30f,ahead_z=1e30f;
  float mx=c.target[0]-ahead_x,mz=c.target[2]-ahead_z;
  dc_level_prefetch_frame();
  if(mx*mx+mz*mz>=1.f){
   const double reach=WINDOW_MARGIN_M+WINDOW_AHEAD_M;uint32_t n=0,k;
   dc_render_query far={query.x0-reach,query.z0-reach,query.x1+reach,query.z1+reach};
   ahead_x=c.target[0];ahead_z=c.target[2];
   /* the catalog's sectors themselves: dc_render_window_entries caps a
    * window at 11 cells an axis, and this one is wider */
   for(k=0;k<catalog.sector_count;++k){dc_render_entry e;double cell=(double)catalog.cell_size,x0,z0;char path[64];
    if(!dc_render_entry_get(&catalog,k,&e))break;
    x0=(double)e.ix*cell;z0=(double)e.iz*cell;
    if(x0>far.x1||x0+cell<far.x0||z0>far.z1||z0+cell<far.z0||dc_render_block_cache_holds(&cache,&e))continue;
    snprintf(path,sizeof path,LEVEL_DRAW_ROOT "/sectors/%" PRId32 "_%" PRId32 ".drx",e.ix,e.iz);
    dc_level_prefetch_request(path);++n;}
  }}
 time_stage(2);
 error=dc_render_base_frame_make_draw(&window,&bank,&frame);CHECK(error==NULL,1,error);
 time_stage(3);
 CHECK(window.count==0 || dc_render_draw_window_prepare(&window,&rc),1,"level draw terrain prepare");
 time_stage(4);
 dc_level_models_frame(rt,&ic);
 time_stage(5);
 CHECK(pvr_wait_ready(),0,"level draw PVR ready between GOAL frames");
 time_stage(6);
 pvr_scene_begin();pvr_set_bg_color(.45f,.62f,.80f);
 for(i=0;i<sizeof lists/sizeof lists[0];++i){
  ok&=pvr_list_begin(lists[i])==0;
  if(window.count)ok&=dc_render_base_draw(&bank,&frame,lists[i],&base);
  {uint32_t s0=inst.submitted,t0=inst.triangles;
   if(lists[i]!=PVR_LIST_TR_POLY)ok&=dc_instance_draw_list(&pkg,&ic,view,&inst,lists[i],materials,FLY_MATERIAL_SLOTS);
   dc_level_frame_timing.submitted[i]+=inst.submitted-s0;
   if(lists[i]==PVR_LIST_OP_POLY){unsigned k;for(k=0;k<4;++k)dc_level_frame_timing.lods[k]+=inst.lod_instances[k];}dc_level_frame_timing.triangles[i]+=inst.triangles-t0;}
  {uint64_t m0=timer_us_gettime64();uint32_t us;
   ok&=dc_level_models_draw(rt,&ic,lists[i]);
   us=(uint32_t)(timer_us_gettime64()-m0);dc_level_frame_timing.sum_us[13]+=us;
   if(us>dc_level_frame_timing.max_us[13])dc_level_frame_timing.max_us[13]=us;}
  ok&=pvr_list_finish()==0;
  time_stage(7u+i);
 }
 ok&=pvr_scene_finish()==0;
 dc_level_models_end_frame();
 CHECK(ok&&base.invalid==0&&inst.invalid==0,1,"level draw frame submitted without invalid geometry");
 /* No wait for the TA or the render here: they finish while the next GOAL
  * frame runs, and the next draw waits for the PVR before its scene (stage
  * 6). Waiting here cost about 13 ms of every frame. */
 time_stage(10);
 time_stage(11);
 {uint32_t n=dc_level_frame_timing.frames++,goal=time_last_end?(uint32_t)(time_frame_start-time_last_end):0;
  if(time_last_end){dc_level_frame_timing.sum_us[12]+=goal;if(goal>dc_level_frame_timing.max_us[12])dc_level_frame_timing.max_us[12]=goal;}
  if(n<TIME_FRAMES){static uint32_t walks;
   dc_level_frame_timing.frame_us[n]=(uint32_t)(time_at-time_frame_start);dc_level_frame_timing.goal_us[n]=goal;
   dc_level_frame_timing.code_walks[n]=(uint16_t)(dc_goal_code_walks-walks);walks=dc_goal_code_walks;}
  time_last_end=time_at;}
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
  * from target-stance to target-walk (a go from a trans hook) along the hilltop,
  * he covers ground, stays within 1.5 m of his start height (the hill rises
  * about 0.7 m that way), is on a surface for most of the walk, and the
  * collision reports ground under him (onground: a floor contact under the
  * foot sphere) for most frames. */
 {dc_level_walk w;dc_level_player_walk(&w);
  dc_level_draw_metrics.walk_frames=w.walk_frames;dc_level_draw_metrics.walk_travel_cm=(uint32_t)(w.travel*100.f);
  CHECK(w.walk_frames>=15u,1,"level Jak walked from the stick (target-walk)");
  CHECK(w.travel>=2.f,1,"level Jak travelled at least 2 m");
  CHECK(w.below<=1.5f && w.above<=1.5f,1,"level Jak stayed on the hilltop (within 1.5 m of his start height)");
  CHECK(w.walk_onsurf*3u>=w.walk_frames*2u,1,"level Jak on a surface for two thirds of his walk");
  CHECK(w.onground_frames*2u>=w.frames,1,"level Jak on the ground (onground) for most frames");}
 /* Streaming: the STR server reads the game text from the disc and the C
  * kernel's link makes it an object; any spooled animation chunk the game asks
  * for (Jak's idle, after 30 game seconds with no hint talking) is read and
  * linked, and a buffer it fills becomes 'active. */
 {dc_level_str_counts s;dc_level_str_summary(&s);
  CHECK(s.files>=1u && s.linked>=1u,1,"level STR server read and linked the game text");
  CHECK(s.link_errors,0,"level every streamed object linked");
  CHECK(s.chunk_served,s.chunk_requests>0u?s.chunk_requests-1u:0u,"level every spooled chunk asked for after the spool heaps moved was served");
  CHECK(s.chunk_requests==0u || s.active_frames>0u,1,"level a served spooled animation became active");}
 /* Dialogue: the sound loader answers version 2.0, a hint's spool- line is
  * found in VAGDIR and played from VAGWAD on the AICA, *sound-iop-info* follows
  * it, and it ends after its length. */
 {dc_level_sound_counts d;dc_level_sound_summary(&d);
  CHECK(d.irx_calls,1,"level sound loader answered the IRX version (2.0)");
  CHECK(d.streams>=1u && d.ended>=1u,1,"level a dialogue line streamed and ended");
  CHECK(d.missing,0,"level every dialogue line asked for is on the disc");
  CHECK(d.errors==0u && d.pcm_bytes>0u,1,"level the dialogue played on the AICA stream");}
 return 1;
}
#undef CHECK

/* Results on the TV for real hardware (AI-assisted): when the run ends, on an
 * unhandled exception, or after 90 s without a check or a frame, the level ELF
 * shows what it measured in the BIOS font, for a photo. t is runtime_main.c's
 * dc_backend: words 2-5 state, total, passed, failed case; 8-11 observed and
 * expected; the description from byte 48. A on a controller turns the page. */
static const volatile uint32_t *results_t;
static kthread_t *results_main;
static volatile int results_shown;
static uint64_t results_start_ms;
static int results_row;
static uint32_t median_of(const volatile uint32_t *v,unsigned from,unsigned to){
 uint32_t a[TIME_FRAMES];unsigned n=0,i,j;
 for(i=from;i<to&&i<TIME_FRAMES&&i<dc_level_frame_timing.frames;++i)a[n++]=v[i];
 if(!n)return 0;
 for(i=1;i<n;++i){uint32_t x=a[i];for(j=i;j&&a[j-1]>x;--j)a[j]=a[j-1];a[j]=x;}
 return n&1?a[n/2]:(a[n/2-1]+a[n/2])/2u;
}
static void line(uint16_t colour,const char *format,...) __attribute__((format(printf,2,3)));
static void line(uint16_t colour,const char *format,...){
 char text[56];va_list args;
 if(results_row>=19)return;
 va_start(args,format);vsnprintf(text,sizeof text,format,args);va_end(args);
 bfont_set_foreground_color(colour);
 bfont_draw_str(vram_s+(6+24*results_row)*640+8,640,1,text);++results_row;
}
static void screen(uint16_t background){
 unsigned i;
 vid_set_mode(DM_640x480,PM_RGB565);
 for(i=0;i<640u*480u;++i)vram_s[i]=background;
 bfont_set_background_color(background);results_row=0;
}
#define MS(us) (unsigned)((us)/1000u),(unsigned)((us)%1000u/100u)
static void page_summary(const char *why,uint16_t background){
 const volatile uint32_t *t=results_t;unsigned frames=dc_level_frame_timing.frames,f=frames?frames:1,k;
 uint32_t goal=median_of(dc_level_frame_timing.goal_us,40,96),draw=median_of(dc_level_frame_timing.frame_us,40,96);
 uint32_t total=goal+draw;
 screen(background);
 line(0xffff,"JAK DC LEVEL TEST %s %s",__DATE__,__TIME__);
 line(0xffe0,"%s: %lu of %lu checks",why,(unsigned long)t[4],(unsigned long)t[3]);
 if(t[5]){line(0xffff,"failed #%lu: %.38s",(unsigned long)t[5],(const char *)(t+12));
  line(0xffff," got %08lx%08lx want %08lx%08lx",(unsigned long)t[9],(unsigned long)t[8],(unsigned long)t[11],(unsigned long)t[10]);}
 else line(0xffff,"%.52s",(const char *)(t+12));
 line(0xffff,"run %lu s  frames %u  camera %lu  PVR errors %lu",
  (unsigned long)((timer_ms_gettime64()-results_start_ms)/1000u),frames,
  (unsigned long)dc_level_draw_metrics.camera_reads,(unsigned long)dc_level_draw_metrics.pvr_errors);
 line(0x07e0,"STEADY FRAMES 40-95, MEDIAN MS");
 line(0xffff," logic %u.%u  draw %u.%u  frame %u.%u  fps %u.%u",MS(goal),MS(draw),MS(total),
  (unsigned)(total?10000000u/total/10u:0),(unsigned)(total?10000000u/total%10u:0));
 line(0xffff," all frames: logic %u.%u  draw %u.%u",
  MS(median_of(dc_level_frame_timing.goal_us,1,96)),MS(median_of(dc_level_frame_timing.frame_us,0,96)));
 line(0x07e0,"DRAW STAGES, AVERAGE MS OVER %u FRAMES",frames);
 {static const char *names[]={"samp","cell","wind","base","prep","modl","rdy","OP","PT","TR","TA","rend","logc","mdrw","disp"};
  char text[3][60];unsigned n[3]={0,0,0};
  for(k=0;k<TIME_STAGES;++k){unsigned r=k/5,us=dc_level_frame_timing.sum_us[k]/f;
   if(n[r]<sizeof text[r])n[r]+=(unsigned)snprintf(text[r]+n[r],sizeof text[r]-n[r]," %s %u.%u",names[k],MS(us));}
  for(k=0;k<3;++k)line(0xffff,"%s",text[k]);}
 line(0xffff," max: modl %u.%u  PT %u.%u  logc %u.%u",MS(dc_level_frame_timing.max_us[5]),
  MS(dc_level_frame_timing.max_us[8]),MS(dc_level_frame_timing.max_us[12]));
 line(0x07e0,"SCENERY PER FRAME");
 line(0xffff," tris visited %lu/%lu  sent %lu/%lu (OP/PT)",
  (unsigned long)(dc_level_frame_timing.triangles[0]/f),(unsigned long)(dc_level_frame_timing.triangles[1]/f),
  (unsigned long)(dc_level_frame_timing.submitted[0]/f),(unsigned long)(dc_level_frame_timing.submitted[1]/f));
 line(0xffff," LOD 0-3: %lu %lu %lu %lu  terrain faces %lu",
  (unsigned long)(dc_level_frame_timing.lods[0]/f),(unsigned long)(dc_level_frame_timing.lods[1]/f),
  (unsigned long)(dc_level_frame_timing.lods[2]/f),(unsigned long)(dc_level_frame_timing.lods[3]/f),
  (unsigned long)dc_level_draw_metrics.last_window_faces);
 line(0xffff," models drawn %lu  skin max %lu us  slab peak %lu K",(unsigned long)dc_level_model_metrics.drawn,
  (unsigned long)dc_level_model_metrics.skin_us_max,(unsigned long)(dc_level_draw_metrics.slab_high_water/1024u));
 line(0x8410,"A: per-frame milliseconds");
}
static void page_frames(void){
 unsigned i,n=dc_level_frame_timing.frames<TIME_FRAMES?dc_level_frame_timing.frames:TIME_FRAMES;
 screen(0x0010);
 line(0xffe0,"PER FRAME MS, LOGIC/DRAW, FRAMES 24-95");
 for(i=24;i<n&&results_row<18;i+=6){char text[60];unsigned j,m=0;
  m+=(unsigned)snprintf(text,sizeof text,"%2u",i);
  for(j=i;j<i+6&&j<n&&m<sizeof text;++j)m+=(unsigned)snprintf(text+m,sizeof text-m," %3u/%2u",
   (unsigned)(dc_level_frame_timing.goal_us[j]/1000u),(unsigned)(dc_level_frame_timing.frame_us[j]/1000u));
  line(0xffff,"%s",text);}
 line(0x8410,"A: summary");
}
/* Pages until power off; A toggles them. Never returns. */
static void results_pages(const char *why,uint16_t background,int threads){
 int page=0,held=0;
 page_summary(why,background);
 for(;;){
  maple_device_t *pad=threads?maple_enum_type(0,MAPLE_FUNC_CONTROLLER):NULL;
  cont_state_t *st=pad?(cont_state_t *)maple_dev_status(pad):NULL;
  int a=st&&(st->buttons&CONT_A);
  if(a&&!held){page^=1;if(page)page_frames();else page_summary(why,background);}
  held=a;
  if(threads)thd_sleep(50);
 }
}
static void results_fault(irq_t code,irq_context_t *c,void *data){
 char why[44];(void)data;
 results_shown=1;
 snprintf(why,sizeof why,"CRASH %03lx pc %08lx pr %08lx",(unsigned long)code,(unsigned long)c->pc,(unsigned long)c->pr);
 results_pages(why,0x8000,0);
}
static void *results_watch(void *unused){
 uint32_t last=0,quiet=0;(void)unused;
 for(;;){
  uint32_t now=results_t[3]+dc_level_draw_metrics.frames;
  thd_sleep(1000);
  if(results_shown)return NULL;
  if(now!=last){last=now;quiet=0;continue;}
  if(++quiet<90)continue;
  results_shown=1;
  {char why[44];snprintf(why,sizeof why,"HANG pc %08lx pr %08lx",
    (unsigned long)(results_main?results_main->context.pc:0),(unsigned long)(results_main?results_main->context.pr:0));
   results_pages(why,0x8200,1);}
 }
}
void dc_results_install(const volatile void *telemetry){
 results_t=telemetry;results_main=thd_get_current();results_start_ms=timer_ms_gettime64();
 irq_set_handler(EXC_UNHANDLED_EXC,results_fault,NULL);
 thd_create(1,results_watch,NULL);
}
void dc_results_show(const volatile void *telemetry){
 results_t=telemetry;
 if(results_shown){for(;;)thd_sleep(1000);}
 results_shown=1;
 pvr_shutdown();
 results_pages(results_t[2]==2u?"PASS":"FAIL",results_t[2]==2u?0x0200:0x8000,1);
}
#endif
