/* Bounded original-terrain native movement prototype (AI-assisted), ISC. */
#include <kos.h>
#include <dc/biosfont.h>
#include <dc/maple/controller.h>
#include <math.h>
#include <malloc.h>
#include <stdlib.h>
#include "textured_scene.h"
#include "scene_telemetry.h"
#include "scene_projection.h"
#include "player_motion.h"
#include "player_anim.h"
#include "player_world_motion.h"
#include "world_manifest.h"
#include "training_layer.h"
#include "camera_rig.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

volatile dc_scene_telemetry dc_telemetry = { .magic = DC_TELEMETRY_MAGIC, .version = 1 };

static void record_error(const char *message) {
  unsigned i;
  for (i = 0; i < sizeof(dc_telemetry.error)-1 && message[i]; ++i)
    dc_telemetry.error[i] = message[i];
  dc_telemetry.error[i] = 0;
  dc_telemetry.state = DC_ERROR;
}

static pvr_poly_hdr_t label_header;
static pvr_ptr_t label_texture;
static dc_training_layer actor_layer,terrain_layer;

static int make_label(int fixture) {
  pvr_poly_cxt_t context;
  uint16_t *pixels = calloc(512 * 64, 2);
  if (!pixels) return -1;
  bfont_draw_str_ex(pixels, 512, 0xffff, 0x8000, 16, true,
                   fixture ? "TEXTURED FIXTURE / Dreamcast" : "JAK NATIVE TRAINING COLLISION");
  bfont_draw_str_ex(pixels + 512 * 30, 512, 0xffff, 0x8000, 16, true,
                   "Stick:move A:jump L/R:camera START:exit");
  label_texture = pvr_mem_malloc(512 * 64 * 2);
  if (!label_texture) { free(pixels); return -1; }
  pvr_txr_load(pixels, label_texture, 512 * 64 * 2);
  free(pixels);
  pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY,
                   PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED,
                   512, 64, label_texture, PVR_FILTER_NONE);
  context.gen.culling = PVR_CULLING_NONE;
  pvr_poly_compile(&label_header, &context);
  return 0;
}

static void draw_label(void) {
  unsigned i;
  const float x[4] = {16, 528, 16, 528}, y[4] = {12, 12, 76, 76};
  pvr_vertex_t v = {0};
  pvr_prim(&label_header, sizeof(label_header));
  for (i = 0; i < 4; ++i) {
    v.flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    v.x = x[i]; v.y = y[i]; v.z = 100.0f;
    v.u = (i & 1) ? 1.0f : 0.0f; v.v = i >= 2 ? 1.0f : 0.0f;
    v.argb = 0xffffffffu;
    pvr_prim(&v, sizeof(v));
  }
}


/* Temporary native playable slice, original GOAL movement is not running.
 * Baked user-owned data remains local (AI-assisted), ISC. */
dc_player dc_player_state;
volatile uint32_t dc_play_clip;
static dc_player_anim animation;
static float *local_pose,*camera_vertices;

static dc_textured_scene terrain;
static dc_collision_world world;
volatile uint32_t dc_world_respawns;
volatile uint32_t dc_training_ticks;
dc_camera_rig dc_camera_state;
static float *terrain_camera;
static float camera_floor;
static dc_world_manifest meta;
static const char *load_terrain(void){
 FILE *f;const char *error;dc_vec3 origin;
 f=fopen("/rd/world.xfm","rb");if(!f)return "world.xfm missing";
 error=dc_world_manifest_load(f,&meta);fclose(f);if(error)return error;
 if(fabsf(meta.spawn.x)>6.5f||fabsf(meta.spawn.z)>6.5f||meta.spawn.y< -4)
  return "spawn outside bounded training map";
 f=fopen("/rd/world.dct","rb");if(!f)return "world.dct missing";
 error=dc_world_manifest_check_file(f,meta.dct_bytes,meta.dct_crc);
 if(!error)error=dc_textured_load(f,&terrain);
 fclose(f);if(error)return error;
 f=fopen("/rd/world.dcc","rb");if(!f)return "world.dcc missing";
 error=dc_world_manifest_check_file(f,meta.dcc_bytes,meta.dcc_crc);
 if(!error)error=dc_collision_load(f,&world,&origin);
 fclose(f);if(error)return error;
 error=dc_world_manifest_match(&meta,&terrain,&world,origin);if(error)return error;
 if(meta.extent.x<=0||meta.extent.y<=0||meta.extent.z<=0)return "camera requires WLD3 collision coverage";
 terrain_camera=malloc(terrain.vertex_count*3*sizeof(float));
 if(!terrain_camera)return "world camera allocation failed";
 return NULL;
}
int main(void){
 dc_textured_scene scene;FILE *f;const char *error=NULL;uint64_t previous;float accumulator=0,yaw=0,landing_time=1;
 unsigned frames=0,last_landings=0,i;int result=1;
 pvr_init_params_t params={.opb_sizes={PVR_BINSIZE_32,0,0,0,PVR_BINSIZE_32},.vertex_buf_size=512*1024,.opb_overflow_count=3};
 dc_telemetry.state=DC_BOOT;dc_telemetry.source=2;
 f=fopen("/rd/player.dct","rb");if(!f){record_error("player.dct missing");return 1;}
 error=dc_textured_load(f,&scene);fclose(f);if(error){record_error(error);return 1;}
 f=fopen("/rd/player.dca","rb");if(!f){dc_textured_free(&scene);record_error("player.dca missing");return 1;}
 error=dc_player_anim_load(f,scene.vertex_count,&animation);fclose(f);
 if(error){dc_textured_free(&scene);record_error(error);return 1;}
 error=load_terrain();if(error){record_error(error);goto release;}
 local_pose=malloc(scene.vertex_count*12);camera_vertices=malloc(scene.vertex_count*12);
 vid_set_mode(DM_640x480,PM_RGB565);
 if(pvr_init(&params)<0){record_error("PVR initialization failed");goto release;}
 pvr_set_bg_color(0.12f,0.20f,0.29f);
 if(!local_pose || !camera_vertices || !dc_training_layer_make(&actor_layer,&scene) || !dc_training_layer_make(&terrain_layer,&terrain) || make_label(0)<0){record_error("render allocation failed");goto shutdown;}
 if(!dc_player_world_place(&dc_player_state,&world,meta.spawn)){record_error("world placement refused");goto shutdown;}camera_floor=dc_player_state.y;
 dc_telemetry.triangles=scene.index_count/3+terrain.index_count/3;
 dc_telemetry.geometry_bytes=scene.vertex_count*24+scene.index_count*2+animation.vertices*animation.frames*6+terrain.vertex_count*24+terrain.index_count*2+world.count*76;
 dc_telemetry.system_ram=_arch_mem_top-0x8c000000u;dc_telemetry.vram_free=pvr_mem_available();
 {struct mallinfo m=mallinfo();dc_telemetry.allocator_live=m.uordblks;dc_telemetry.allocator_arena=m.arena;}
 dc_telemetry.state=DC_READY;previous=timer_us_gettime64();
 while(1){
  uint64_t now=timer_us_gettime64();float dt=(float)(now-previous)/1000000.0f,sx=0,sz=0,cy,sy,c,s;
  unsigned clip;float clip_time;maple_device_t *device=maple_enum_type(0,MAPLE_FUNC_CONTROLLER);
  cont_state_t *input=device?maple_dev_status(device):NULL;
  dc_telemetry.frame_us=(uint32_t)(now-previous);previous=now;if(dt>0.05f)dt=0.05f;
  if(input){if(input->buttons&CONT_START){result=0;break;}
   yaw+=((float)input->rtrig-input->ltrig)/255.0f*dt*1.8f;sx=input->joyx/127.0f;sz=-input->joyy/127.0f;
  }
  cy=cosf(yaw);sy=sinf(yaw);accumulator+=dt;
  while(accumulator>=1.0f/60.0f){dc_collision_result hit;if(!dc_player_world_tick(&dc_player_state,&world,cy*sx-sy*sz,sy*sx+cy*sz,input && (input->buttons&CONT_A),&hit)){record_error("world motion failed");break;}if(hit.event_bits[1] || (hit.event_bits[0]&~UINT32_C(1))){record_error("unsupported world surface event");break;}
 if(dc_player_state.y<-4 || fabsf(dc_player_state.x)>6.8f || fabsf(dc_player_state.z)>6.8f){
  if(!dc_player_world_place(&dc_player_state,&world,meta.spawn)){record_error("world respawn failed");break;}
  ++dc_world_respawns;camera_floor=dc_player_state.y;
 }++dc_training_ticks;accumulator-=1.0f/60.0f;}
  if(dc_telemetry.state==DC_ERROR)break;
  if(dc_player_state.grounded)camera_floor+=(dc_player_state.y-camera_floor)*fminf(1,dt*8);
  if(dc_player_state.landings!=last_landings){landing_time=0;last_landings=dc_player_state.landings;}else landing_time+=dt;
  clip=dc_player_state.mode;clip_time=dc_player_state.anim_time;
  if(clip==DC_PLAYER_AIR){clip=3;if(clip_time>0.4f){clip=4;clip_time-=0.4f;}}
  else if(landing_time<0.18f){clip=5;clip_time=landing_time;}
  dc_play_clip=clip;dc_player_anim_sample(&animation,clip,clip_time,local_pose);
  if(!dc_camera_rig_update(&dc_camera_state,&world,meta.extent,(dc_vec3){dc_player_state.x,dc_player_state.y,dc_player_state.z},camera_floor,yaw,dt)){record_error("camera collision query refused");break;}
  c=cosf(dc_player_state.yaw);s=sinf(dc_player_state.yaw);
  for(i=0;i<scene.vertex_count;++i){float x=local_pose[i*3],z=local_pose[i*3+2];
   camera_vertices[i*3]=c*x+s*z;camera_vertices[i*3+1]=local_pose[i*3+1]+(dc_player_state.y-dc_camera_state.target.y)/4;camera_vertices[i*3+2]=-s*x+c*z;
  }
  for(i=0;i<terrain.vertex_count;++i){
   terrain_camera[i*3]=(terrain.vertices[i].x*meta.scale+meta.shift.x-dc_player_state.x)/4;
   terrain_camera[i*3+1]=(terrain.vertices[i].y*meta.scale+meta.shift.y-dc_camera_state.target.y)/4;
   terrain_camera[i*3+2]=(terrain.vertices[i].z*meta.scale+meta.shift.z-dc_player_state.z)/4;
  }
  /* Preserve view coordinates for clipping; divide only front-side vertices. */
  if(!dc_scene_prepare(camera_vertices,12,actor_layer.vertices,scene.vertex_count,yaw,DC_CAMERA_PITCH,dc_camera_state.distance/4) ||
     !dc_scene_prepare(terrain_camera,12,terrain_layer.vertices,terrain.vertex_count,yaw,DC_CAMERA_PITCH,dc_camera_state.distance/4)){record_error("projection mode rejected");break;}
  pvr_wait_ready();pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);dc_training_layer_draw(&terrain_layer,&terrain,PVR_LIST_OP_POLY);dc_training_layer_draw(&actor_layer,&scene,PVR_LIST_OP_POLY);draw_label();pvr_list_finish();
  pvr_list_begin(PVR_LIST_PT_POLY);dc_training_layer_draw(&terrain_layer,&terrain,PVR_LIST_PT_POLY);dc_training_layer_draw(&actor_layer,&scene,PVR_LIST_PT_POLY);pvr_list_finish();pvr_scene_finish();
  dc_telemetry.frames=++frames;dc_telemetry.yaw=yaw;dc_telemetry.pitch=DC_CAMERA_PITCH;dc_telemetry.distance=dc_camera_state.distance/4;
 }
 pvr_wait_ready();
shutdown:
 if(label_texture){pvr_wait_ready();pvr_mem_free(label_texture);label_texture=NULL;}
 pvr_wait_ready();dc_training_layer_free(&actor_layer);dc_training_layer_free(&terrain_layer);pvr_shutdown();
release:
 dc_textured_free(&terrain);dc_collision_free(&world);free(terrain_camera);
 free(local_pose);free(camera_vertices);dc_player_anim_free(&animation);dc_textured_free(&scene);
 if(!result)dc_telemetry.state=DC_EXIT;
 return result;
}
