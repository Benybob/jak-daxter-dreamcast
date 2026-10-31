/* C-scripted GOAL camera readback drives the bounded renderer (AI-assisted), ISC. */
#include "arena_game.h"
#include <math.h>
#include <string.h>

/* Bit check survives the renderer's fast-math flags, including NaNs. */
static int bounded(float f,float limit){
 uint32_t bits;memcpy(&bits,&f,4);
 return (bits&0x7fffffffu)<0x7f800000u && f>=-limit && f<=limit;
}
static void rotation(float yaw,float pitch,float m[16]){
 float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
 const float r[16]={cy,0,sy,0,sp*sy,cp,-sp*cy,0,-cp*sy,sp,cp*cy,0,0,0,0,1};
 memcpy(m,r,sizeof r);
}
int dc_arena_game_convert(const dc_game_camera_pose *p,float distance,dc_game_camera *out){
 dc_game_camera c;float horizontal,m[16];unsigned i;
 if(!p||!out||!bounded(distance,1000.f)||distance<0)return 0;
 for(i=0;i<3;++i)if(!bounded(p->trans[i],4096000000.f))return 0;
 if(!bounded(p->trans[3],1.f)||p->trans[3]!=1.f)return 0;
 for(i=0;i<16;++i)if(!bounded(p->inv[i],1.001f))return 0;
 /* No roll or pole ambiguity in the current yaw/pitch renderer. Reject scale,
  * shear, reflection and malformed homogeneous lanes by reconstructing it. */
 horizontal=sqrtf(p->inv[8]*p->inv[8]+p->inv[10]*p->inv[10]);
 if(horizontal<.001f)return 0;
 c.yaw=atan2f(-p->inv[8],p->inv[10]);c.pitch=atan2f(p->inv[9],horizontal);
 rotation(c.yaw,c.pitch,m);
 for(i=0;i<16;++i)if(fabsf(p->inv[i]-m[i])>.0001f)return 0;
 c.distance=distance;
 for(i=0;i<3;++i){
  c.eye[i]=p->trans[i]*(1.f/4096.f);
  c.target[i]=c.eye[i]+m[8+i]*distance;
  if(!bounded(c.target[i],1000000.f))return 0;
 }
 *out=c;return 1;
}
/* Only allocated main-arena bytes. Never dereference a GOAL integer as a C
 * pointer, accept an extension/VRAM alias, or add a field offset before bounds. */
static unsigned char *span(dc_goal_runtime *rt,uint32_t address,uint32_t bytes){
 uint32_t off;
 if(!rt||!rt->memory||rt->used>rt->capacity||rt->base>UINT32_MAX-rt->capacity||address<rt->base)return NULL;
 off=address-rt->base;
 if(off>rt->used||bytes>rt->used-off)return NULL;
 return rt->memory+off;
}
static unsigned char *camera_bytes(dc_goal_runtime *rt,uint32_t camera,uint32_t type){
 unsigned char *p,*t;uint32_t tag;uint16_t size;
 if(!rt||rt->error!=DC_GOAL_OK||(camera&15u)!=4u||(type&15u)!=4u||camera<4u||type<4u)return NULL;
 t=span(rt,type-4u,20);p=span(rt,camera-4u,DC_GAME_CAMERA_BYTES);
 if(!t||!p)return NULL;
 memcpy(&tag,t,4);if(tag!=rt->type_type)return NULL;
 memcpy(&size,t+12,2);if(size!=DC_GAME_CAMERA_BYTES)return NULL;
 memcpy(&tag,p,4);if(tag!=type)return NULL;
 return p+4;
}
int dc_arena_game_read(dc_goal_runtime *rt,uint32_t camera,uint32_t type,dc_game_camera_pose *out){
 dc_game_camera_pose p;dc_game_camera c;unsigned char *b;unsigned i;float rot[16];
 if(!out||(b=camera_bytes(rt,camera,type))==NULL)return 0;
 memcpy(p.trans,b+DC_GAME_CAMERA_TRANS,sizeof p.trans);memcpy(p.inv,b+DC_GAME_CAMERA_INV,sizeof p.inv);
 if(!dc_arena_game_convert(&p,0,&c))return 0;
 /* C-script layout: pure rotation. Real final GOAL affine matrices need the
  * separate adapter described in GAME_CAMERA_ROUTE.md. */
 memcpy(rot,b+DC_GAME_CAMERA_ROT,sizeof rot);
 for(i=0;i<16;++i)
  if(!bounded(rot[i],1.001f)||rot[i]!=p.inv[(i%4)*4+i/4])return 0;
 *out=p;return 1;
}
int dc_arena_game_write(dc_goal_runtime *rt,uint32_t camera,uint32_t type,const dc_game_camera_pose *p){
 dc_game_camera c;unsigned char *b;float transpose[16];unsigned i,j;
 if(!dc_arena_game_convert(p,0,&c)||(b=camera_bytes(rt,camera,type))==NULL)return 0;
 for(i=0;i<4;++i)for(j=0;j<4;++j)transpose[i*4+j]=p->inv[j*4+i];
 memcpy(b+DC_GAME_CAMERA_TRANS,p->trans,sizeof p->trans);memcpy(b+DC_GAME_CAMERA_INV,p->inv,sizeof p->inv);
 memcpy(b+DC_GAME_CAMERA_ROT,transpose,sizeof transpose);return 1;
}

/* Uses the readback basis and eye directly, without yaw/pitch or orbit anchor.
 * Checking real off-axis geometry makes a flipped draw orientation observable. */
int dc_arena_game_terrain_matches(const dc_game_camera_pose *p,const dc_render_draw_window *w){
 dc_game_camera c;uint32_t i;unsigned axis;int off_axis=0;
 if(!w||!w->valid||!w->prepared||!w->vertices||!w->view||w->vertex_count<3||
    !dc_arena_game_convert(p,0,&c))return 0;
 for(i=0;i<w->vertex_count;++i){
  float point[3],delta[3],actual[3]={w->view[i].x,w->view[i].y,w->view[i].z};
  memcpy(point,w->vertices+(size_t)i*DC_RENDER_DRAW_VERTEX_BYTES,sizeof point);
  for(axis=0;axis<3;++axis){
   if(!bounded(point[axis],1000000.f))return 0;
   delta[axis]=point[axis]-p->trans[axis]/4096.f;
  }
  for(axis=0;axis<3;++axis){
   const float *basis=p->inv+4*axis;
   float expected=(basis[0]*delta[0]+basis[1]*delta[1]+basis[2]*delta[2])*.25f;
   if(!bounded(actual[axis],2000000.f)||fabsf(actual[axis]-expected)>.001f+fabsf(expected)*.00001f)return 0;
   if(axis<2&&fabsf(expected)>1.f)off_axis=1;
  }
 }
 return off_axis;
}

#ifdef DC_ARENA_GAME
#include <kos.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "render_base.h"
#include "render_block_cache.h"
#include "instance_cells.h"
#include "goal_fiber.h"
#include "build/arena_fly_fixture.h"

/* Level files on the VRAM romdisk mounted at /rd after the boot image is released.
 * verify_backend.py launches this ELF with no disc, so /cd is empty. */
#ifndef DC_ARENA_FLY_ROOT
#define DC_ARENA_FLY_ROOT "/rd"
#endif
enum {VERTEX_BYTES=DC_ARENA_FLY_VERTEX_BYTES,SLAB_BYTES=2621440};
typedef char arena_fly_path[FLY_FRAMES==16?1:-1];
extern const int romdisk_size;
extern volatile uint32_t dc_arena_slab_live,dc_arena_slab_high_water,dc_arena_slab_failures;
int dc_arena_fly_check(uint64_t,uint64_t,const char *);
int dc_arena_fly_release_boot(uint32_t *);
/* verify_backend.py --dump dc_arena_game_metrics 80, all words in this order. */
volatile struct {
 uint32_t frames,checks,passed,last_frame,slab_high_water,slab_live,slab_failures,
 vram_after_boot,vram_after_textures,texture_bytes,boot_bytes,dma_bytes,
 max_faces,max_vertices,max_instances,max_triangles,max_vertex_bytes,pvr_errors,
 cache_peak_bytes,cache_evictions;
} dc_arena_game_metrics;
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
 ++dc_arena_game_metrics.checks;
 if(!dc_arena_fly_check(actual,expected,name))return 0;
 ++dc_arena_game_metrics.passed;return 1;
}
#define CHECK(a,b,name) do{if(!check((uint64_t)(a),(uint64_t)(b),name))return 0;}while(0)
static void sample(void){
 dc_arena_game_metrics.slab_high_water=dc_arena_slab_high_water;
 dc_arena_game_metrics.slab_live=dc_arena_slab_live;
 dc_arena_game_metrics.slab_failures=dc_arena_slab_failures;
}
/* No libc or PVR work in the error IRQ. Publish failure even if a wait hangs. */
static void pvr_fault(uint32_t code,void *context){
 static const char why[]="arena game PVR overflow/input error (observed = ASIC event)";
 unsigned i;(void)context;++dc_arena_game_metrics.pvr_errors;
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
 dc_arena_game_metrics.boot_bytes=(uint32_t)romdisk_size;
 /* EXTENSION_BYTES in arena_coexist.c: DMA payload 3506176 plus the 64-byte type pad. */
 dc_arena_game_metrics.dma_bytes=3506240u;
 if(!dc_arena_fly_release_boot((uint32_t *)&dc_arena_game_metrics.vram_after_boot))return 0;
 for(slot=0;slot<sizeof events/sizeof events[0];++slot){
  asic_evt_set_handler(events[slot],pvr_fault,NULL);asic_evt_enable(events[slot],ASIC_IRQ_DEFAULT);
 }
 file=fopen(DC_ARENA_FLY_ROOT "/manifest.drm","rb");
 CHECK(file!=NULL,1,"arena game romdisk manifest opens");
 error=dc_render_catalog_load(file,&catalog);read=fclose(file);
 CHECK(error==NULL && read==0,1,error?error:"arena game manifest closes");
 for(slot=0;slot<catalog.texture_count;++slot){
  dc_render_texture t;
  if(!dc_render_texture_get(&catalog,slot,&t)){textures=0;break;}
  /* KOS PVR dlmalloc rounds each request+4 to 32 bytes. Include headroom. */
  if(t.format!=2u)needed+=(t.bytes+35u)&~31u;
 }
 CHECK(textures,1,"arena game texture entries");
 CHECK(pvr_mem_available()>=needed+4096u,1,"arena game VRAM fits DMA and rounded textures");
 error=dc_render_gpu_make(&catalog,&gpu);CHECK(error==NULL,1,error);
 dc_arena_game_metrics.texture_bytes=gpu.bytes;
 dc_arena_game_metrics.vram_after_textures=pvr_mem_available();
 error=dc_render_base_bank_make(&catalog,&gpu,&bank);CHECK(error==NULL,1,error);
 error=dc_render_block_cache_make_draw(&catalog,DC_ARENA_FLY_ROOT,fly_source_sha,FLY_CACHE_BUDGET+65536u,&cache);
 CHECK(error==NULL,1,error);dc_render_block_cache_probe=sample;
 error=dc_render_draw_window_make(&window,FLY_WINDOW_FACES+64u,FLY_WINDOW_VERTICES+128u,FLY_WINDOW_REMAP+256u);
 CHECK(error==NULL,1,error);
 data=memalign(32,FLY_PACKAGE_BYTES);CHECK(data!=NULL,1,"arena game package slab allocation");
 file=fopen(DC_ARENA_FLY_ROOT "/package.dci","rb");CHECK(file!=NULL,1,"arena game romdisk package opens");
 read=fread(data,1,FLY_PACKAGE_BYTES,file)==FLY_PACKAGE_BYTES&&fgetc(file)==EOF&&!ferror(file);
 read=fclose(file)==0&&read;CHECK(read,1,"arena game package size");
 CHECK(crc32(0,data,FLY_PACKAGE_BYTES),FLY_PACKAGE_CRC,"arena game package CRC");
 error=dc_instance_package_open(data,FLY_PACKAGE_BYTES,&pkg);CHECK(error==NULL,1,error);
 error=dc_instance_cells_make(&cells,DC_ARENA_FLY_ROOT,16.0,FLY_INSTANCE_RECORDS+32u);CHECK(error==NULL,1,error);
 view=memalign(32,(size_t)pkg.max_proto_verts*sizeof *view);CHECK(view!=NULL,1,"arena game view slab allocation");
 bounds=memalign(32,(size_t)pkg.proto_count*sizeof *bounds);CHECK(bounds!=NULL,1,"arena game bounds slab allocation");
 dc_instance_bounds(&pkg,bounds);pkg.bounds=(const float (*)[4])bounds;
 for(slot=0;slot<FLY_MATERIAL_SLOTS;++slot){
  uint16_t c=fly_material_map[slot];if(c==0xffffu)continue;
  if(c>=bank.count){mapped=0;break;}
  materials[slot]=(dc_instance_material){&bank.headers[c],bank.materials[c].list,!(bank.materials[c].flags&64u)};
 }
 CHECK(mapped,1,"arena game material map inside catalog");
 pvr_set_bg_color(.45f,.62f,.80f);ready=1;return 1;
}
static dc_goal_runtime *game_rt;
static dc_game_camera_pose previous;
static uint32_t camera_symbol,camera_type;
volatile struct {uint32_t camera,c_scripted,c_script_reads,c_script_moved,c_script_turned;float eye[3],yaw,pitch;} dc_arena_game_script_camera_metrics;
void dc_arena_game_bind(dc_goal_runtime *rt){game_rt=rt;}
static int pose_equal(const dc_game_camera_pose *a,const dc_game_camera_pose *b){
 unsigned i;for(i=0;i<4;++i)if(a->trans[i]!=b->trans[i])return 0;
 for(i=0;i<16;++i)if(a->inv[i]!=b->inv[i])return 0;
 return 1;
}
static int game_camera(dc_render_camera *rc,dc_instance_camera *ic,dc_render_query *query){
 uint32_t f=dc_arena_game_metrics.frames%FLY_FRAMES,camera,symbol;unsigned i;
 dc_game_camera_pose original,script;dc_game_camera c;float yaw,pitch,half;
 dc_game_camera_pose initial={{0,0,0,1},{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
 CHECK(game_rt && game_rt->error==DC_GOAL_OK && !dc_goal_fiber_current(),1,"arena game camera on root stack");
 if(!camera_symbol){
  camera_symbol=dc_goal_find(game_rt,"*math-camera*");
  CHECK(camera_symbol && game_rt->error==DC_GOAL_OK,1,"arena game original camera symbol");
  symbol=dc_goal_find(game_rt,"math-camera");
  CHECK(symbol && game_rt->error==DC_GOAL_OK,1,"arena game original camera type symbol");
  camera_type=dc_goal_symbol_value(game_rt,symbol);
 }
 camera=dc_goal_symbol_value(game_rt,camera_symbol);
 CHECK(game_rt->error==DC_GOAL_OK && dc_arena_game_read(game_rt,camera,camera_type,&original),1,
       "arena game validates native camera before scripting");
 /* update-camera may copy the idle combiner's origin, or retain the last seed.
  * Any other pose refuses this scripted headless demonstration. */
 CHECK(pose_equal(&original,&initial) ||
       (dc_arena_game_metrics.frames && pose_equal(&original,&previous)),1,
       "arena game pose is idle or previous C-script");
 yaw=fly_yaw+.02f*(float)f/(FLY_FRAMES-1u);
 pitch=fly_pitch+.01f*(float)f/(FLY_FRAMES-1u);rotation(yaw,pitch,script.inv);
 for(i=0;i<3;++i)script.trans[i]=(fly_frames[f].target[i]-script.inv[8+i]*fly_distance)*4096.f;
 script.trans[3]=1;
 CHECK(dc_arena_game_write(game_rt,camera,camera_type,&script),1,"arena game scripted original field layout");
 CHECK(dc_arena_game_read(game_rt,camera,camera_type,&original),1,"arena game reads original fields back");
 CHECK(!memcmp(&original,&script,sizeof script),1,"arena game original fields match script");
 CHECK(dc_arena_game_convert(&original,fly_distance,&c),1,"arena game camera converted to world meters");
 *rc=(dc_render_camera){{c.target[0],c.target[1],c.target[2]},c.yaw,c.pitch,c.distance};
 *ic=(dc_instance_camera){c.target[0],c.target[1],c.target[2],c.yaw,c.pitch,c.distance,fly_draw_distance};
 half=(float)((fly_frames[0].query.x1-fly_frames[0].query.x0)*.5);
 CHECK(bounded(half,128.f) && half>0 && bounded(fly_draw_distance,128.f) && fly_draw_distance>0,1,
       "arena game bounded streaming radius");
 *query=(dc_render_query){(double)c.target[0]-half,(double)c.target[2]-half,
                         (double)c.target[0]+half,(double)c.target[2]+half};
 if(dc_arena_game_metrics.frames){
  dc_arena_game_script_camera_metrics.c_script_moved+=memcmp(previous.trans,original.trans,sizeof original.trans)!=0;
  dc_arena_game_script_camera_metrics.c_script_turned+=memcmp(previous.inv,original.inv,sizeof original.inv)!=0;
 }
 previous=original;dc_arena_game_script_camera_metrics.camera=camera;
 dc_arena_game_script_camera_metrics.c_scripted=1;++dc_arena_game_script_camera_metrics.c_script_reads;
 for(i=0;i<3;++i)dc_arena_game_script_camera_metrics.eye[i]=c.eye[i];
 dc_arena_game_script_camera_metrics.yaw=c.yaw;dc_arena_game_script_camera_metrics.pitch=c.pitch;
 return 1;
}
int dc_arena_fly_frame(void){
 uint32_t f=dc_arena_game_metrics.frames%FLY_FRAMES,i;int ok=1;
 const char *error;dc_render_query query;
 dc_render_base_frame frame={0};dc_render_base_stats base={0};dc_instance_stats inst={0};pvr_stats_t stats;
 dc_render_camera rc;dc_instance_camera ic;
 static const int lists[]={PVR_LIST_OP_POLY,PVR_LIST_PT_POLY,PVR_LIST_TR_POLY};
 if(!game_camera(&rc,&ic,&query))return 0;
 if(!ready&&!initialize()){sample();return 0;}
 error=dc_instance_cells_update(&cells,&pkg,ic.tx,ic.tz,ic.draw_distance);
 CHECK(error==NULL,1,error);
 error=dc_render_block_cache_window(&cache,&catalog,&query,&window);CHECK(error==NULL,1,error);
 CHECK(window.count>0,1,"arena game camera window contains terrain");
 error=dc_render_base_frame_make_draw(&window,&bank,&frame);CHECK(error==NULL,1,error);
 CHECK(dc_render_draw_window_prepare(&window,&rc),1,"arena game frame terrain prepare");
 CHECK(dc_arena_game_terrain_matches(&previous,&window),1,"arena game terrain view matches GOAL readback basis and eye");
 CHECK(pvr_wait_ready(),0,"arena game PVR ready between GOAL frames");
 pvr_scene_begin();
 for(i=0;i<sizeof lists/sizeof lists[0];++i){
  ok&=pvr_list_begin(lists[i])==0;
  ok&=dc_render_base_draw(&bank,&frame,lists[i],&base);
  if(lists[i]!=PVR_LIST_TR_POLY)ok&=dc_instance_draw_list(&pkg,&ic,view,&inst,lists[i],materials,FLY_MATERIAL_SLOTS);
  ok&=pvr_list_finish()==0;
 }
 ok&=pvr_scene_finish()==0;
 CHECK(ok&&base.invalid==0&&inst.invalid==0,1,"arena game frame submitted without invalid geometry");
 CHECK(inst.drawn_instances>0,1,"arena game camera cells contain instances");
 CHECK(inst.triangles>0,1,"arena game camera draws instance triangles");
 CHECK(dc_arena_game_metrics.pvr_errors,0,"arena game frame PVR errors");
 CHECK(pvr_wait_ready(),0,"arena game frame TA complete");
 CHECK(pvr_wait_render_done(),0,"arena game frame rendered");
 CHECK(pvr_get_stats(&stats),0,"arena game frame PVR stats");
 dc_arena_game_metrics.max_vertex_bytes=(uint32_t)stats.vtx_buffer_used_max;
 CHECK(stats.vtx_buffer_used_max<=VERTEX_BYTES,1,"arena game frame fits reserved vertex buffer");
 CHECK(dc_arena_game_metrics.pvr_errors,0,"arena game rendered frame PVR errors");
 sample();CHECK(dc_arena_slab_failures,0,"arena game frame slab has no failed requests");
 if(window.count>dc_arena_game_metrics.max_faces)dc_arena_game_metrics.max_faces=window.count;
 if(window.vertex_count>dc_arena_game_metrics.max_vertices)dc_arena_game_metrics.max_vertices=window.vertex_count;
 if(inst.drawn_instances>dc_arena_game_metrics.max_instances)dc_arena_game_metrics.max_instances=inst.drawn_instances;
 if(inst.triangles>dc_arena_game_metrics.max_triangles)dc_arena_game_metrics.max_triangles=inst.triangles;
 dc_arena_game_metrics.last_frame=f;++dc_arena_game_metrics.frames;return 1;
}
int dc_arena_fly_finish(void){
 dc_render_block_cache_stats stats;sample();dc_render_block_cache_measure(&cache,&stats);
 dc_arena_game_metrics.cache_peak_bytes=stats.peak_bytes;dc_arena_game_metrics.cache_evictions=stats.evictions;
 CHECK(dc_arena_game_metrics.frames,64,"arena game all dispatches draw");
 CHECK(dc_arena_game_script_camera_metrics.c_script_moved,63,"arena game C-script position readback changes each frame");
 CHECK(dc_arena_game_script_camera_metrics.c_script_turned,63,"arena game C-script orientation readback changes each frame");
 CHECK(dc_arena_slab_high_water>0&&dc_arena_slab_high_water<=SLAB_BYTES,1,"arena game slab high-water within reserve");
 CHECK(dc_arena_slab_failures,0,"arena game all renderer allocations stayed in slab");
 CHECK(dc_arena_game_metrics.pvr_errors,0,"arena game no PVR overflow");
 return 1;
}
void dc_arena_game_report(char *description,size_t bytes,uint32_t used,uint32_t capacity){
 snprintf(description,bytes,"BOOT C-script=%lu/%lu frames=%lu slab=%lu arena=%lu/%lu",
  (unsigned long)dc_arena_game_metrics.passed,(unsigned long)dc_arena_game_metrics.checks,
  (unsigned long)dc_arena_game_metrics.frames,(unsigned long)dc_arena_slab_high_water,
  (unsigned long)used,(unsigned long)capacity);
}
#undef CHECK

#endif
