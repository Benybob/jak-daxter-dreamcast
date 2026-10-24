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
static pvr_poly_hdr_t *batch_headers;
static pvr_ptr_t *gpu_textures;
static unsigned gpu_texture_count;
static dc_projected_vertex *projected;

static void release_geometry(void) {
  unsigned i;
  if (gpu_textures)
    for (i=0;i<gpu_texture_count;++i) if (gpu_textures[i]) pvr_mem_free(gpu_textures[i]);
  free(gpu_textures); free(batch_headers); free(projected);
}

static int make_geometry(dc_textured_scene *scene) {
  uint32_t i;
  batch_headers=memalign(32,scene->batch_count*sizeof(*batch_headers));
  gpu_textures=calloc(scene->texture_count,sizeof(*gpu_textures));
  projected=malloc(scene->vertex_count*sizeof(*projected));
  gpu_texture_count=scene->texture_count;
  if (!batch_headers || !gpu_textures || !projected) return -1;
  for (i=0;i<scene->texture_count;++i) {
    dc_texture *t=&scene->textures[i];
    gpu_textures[i]=pvr_mem_malloc(t->bytes);
    if (!gpu_textures[i]) return -1;
    pvr_txr_load(t->pixels,gpu_textures[i],t->bytes);
    free(t->pixels); t->pixels=NULL;
  }
  for (i=0;i<scene->batch_count;++i) {
    const dc_textured_batch *b=&scene->batches[i];
    const dc_texture *t=&scene->textures[b->texture];
    pvr_poly_cxt_t context;
    pvr_poly_cxt_txr(&context,(b->flags&1)?PVR_LIST_PT_POLY:PVR_LIST_OP_POLY,
                    PVR_TXRFMT_ARGB1555|PVR_TXRFMT_TWIDDLED|
                    (t->format==DC_TEXTURE_VQ_ARGB1555?PVR_TXRFMT_VQ_ENABLE:0),t->width,t->height,
                    gpu_textures[b->texture],PVR_FILTER_BILINEAR);
    context.gen.culling=PVR_CULLING_NONE;
    context.gen.shading=PVR_SHADE_GOURAUD;
    context.txr.uv_clamp=((b->flags&4)?PVR_UVCLAMP_U:0)|((b->flags&8)?PVR_UVCLAMP_V:0);
    context.txr.uv_flip=((b->flags&16)?PVR_UVFLIP_U:0)|((b->flags&32)?PVR_UVFLIP_V:0);
    pvr_poly_compile(&batch_headers[i],&context);
  }
  return 0;
}

static int make_label(int fixture) {
  pvr_poly_cxt_t context;
  uint16_t *pixels = calloc(512 * 64, 2);
  if (!pixels) return -1;
  bfont_draw_str_ex(pixels, 512, 0xffff, 0x8000, 16, true,
                   fixture ? "TEXTURED FIXTURE / Dreamcast" : "JAK NATIVE MOVEMENT PROTOTYPE");
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
static struct {float x,y,z;uint32_t color;} map_vertices[2700];
static float map_camera[2700][3];
static dc_projected_vertex map_projected[2700];
static unsigned map_count;
static pvr_poly_hdr_t map_header;
static pvr_vertex_t commands[96] __attribute__((aligned(32)));
static void map_vertex(float x,float y,float z,uint32_t c){
 map_vertices[map_count].x=x;map_vertices[map_count].y=y;map_vertices[map_count].z=z;
 map_vertices[map_count++].color=c;
}
static void quad(float a[3],float b[3],float c[3],float d[3],uint32_t color){
 const float *v[6]={a,b,c,a,c,d};unsigned i;
 for(i=0;i<6;++i)map_vertex(v[i][0],v[i][1],v[i][2],color);
}
static void box(float x0,float x1,float z0,float z1,float h,uint32_t color){
 float v[8][3]={{x0,0,z0},{x1,0,z0},{x1,0,z1},{x0,0,z1},
                {x0,h,z0},{x1,h,z0},{x1,h,z1},{x0,h,z1}};
 static const unsigned faces[5][4]={{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
 unsigned i;for(i=0;i<5;++i)quad(v[faces[i][0]],v[faces[i][1]],v[faces[i][2]],v[faces[i][3]],color);
}
static void make_map(void){
 int x,z;unsigned i;pvr_poly_cxt_t c;
 for(x=-10;x<10;++x)for(z=-10;z<10;++z){
  float a[3]={(float)x,0,(float)z},b[3]={(float)x+1,0,(float)z},
        d[3]={(float)x,0,(float)z+1},e[3]={(float)x+1,0,(float)z+1};
  quad(a,b,e,d,((x+z)&1)?0xff52675fu:0xff6c8177u);
 }
 for(i=0;i<2;++i){const dc_test_platform *b=&dc_test_platforms[i];box(b->x0,b->x1,b->z0,b->z1,b->top,i?0xffc79357u:0xff658cb3u);}
 box(-10.3f,-10,-10,10,0.4f,0xffb2a588u);box(10,10.3f,-10,10,0.4f,0xffb2a588u);
 box(-10,10,-10.3f,-10,0.4f,0xffb2a588u);box(-10,10,10,10.3f,0.4f,0xffb2a588u);
 pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);c.gen.culling=PVR_CULLING_NONE;pvr_poly_compile(&map_header,&c);
}
static void flush_commands(unsigned *n){if(*n){pvr_prim(commands,*n*sizeof(*commands));*n=0;}}
static void draw_map(void){
 unsigned i,j,n=0;pvr_prim(&map_header,sizeof map_header);
 for(i=0;i<map_count;i+=3){
  if(!(map_projected[i].z>0 && map_projected[i].z<20 &&
       map_projected[i+1].z>0 && map_projected[i+1].z<20 &&
       map_projected[i+2].z>0 && map_projected[i+2].z<20))continue;
  for(j=0;j<3;++j){pvr_vertex_t *v=&commands[n++];unsigned k=i+j;
   memset(v,0,sizeof(*v));v->flags=j==2?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;
   v->x=map_projected[k].x;v->y=map_projected[k].y;v->z=map_projected[k].z;v->argb=map_vertices[k].color;
  }if(n==96)flush_commands(&n);
 }flush_commands(&n);
}
static void draw_player(const dc_textured_scene *scene,int list){
 unsigned b,i,j,n;
 for(b=0;b<scene->batch_count;++b){const dc_textured_batch *batch=&scene->batches[b];
  if(((batch->flags&1)?PVR_LIST_PT_POLY:PVR_LIST_OP_POLY)!=list)continue;
  n=0;pvr_prim(&batch_headers[b],sizeof batch_headers[b]);
  for(i=0;i<batch->count;i+=3){
   unsigned ids[3]={scene->indices[batch->first+i],scene->indices[batch->first+i+1],scene->indices[batch->first+i+2]};
   if(projected[ids[0]].z<=0 || projected[ids[1]].z<=0 || projected[ids[2]].z<=0)continue;
   for(j=0;j<3;++j){unsigned k=ids[j];pvr_vertex_t *v=&commands[n++];const dc_textured_vertex *src=&scene->vertices[k];
    memset(v,0,sizeof(*v));v->flags=j==2?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;
    v->x=projected[k].x;v->y=projected[k].y;v->z=projected[k].z;v->u=src->u;v->v=src->v;v->argb=src->argb;
   }if(n==96)flush_commands(&n);
  }flush_commands(&n);
 }
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
 local_pose=malloc(scene.vertex_count*12);camera_vertices=malloc(scene.vertex_count*12);
 vid_set_mode(DM_640x480,PM_RGB565);
 if(pvr_init(&params)<0){record_error("PVR initialization failed");goto release;}
 pvr_set_bg_color(0.12f,0.20f,0.29f);
 if(!local_pose || !camera_vertices || make_geometry(&scene)<0 || make_label(0)<0){record_error("render allocation failed");goto shutdown;}
 make_map();dc_player_init(&dc_player_state);
 dc_telemetry.triangles=scene.index_count/3+map_count/3;
 dc_telemetry.geometry_bytes=scene.vertex_count*24+scene.index_count*2+animation.vertices*animation.frames*6;
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
  while(accumulator>=1.0f/60.0f){dc_player_tick(&dc_player_state,cy*sx-sy*sz,sy*sx+cy*sz,input && (input->buttons&CONT_A));accumulator-=1.0f/60.0f;}
  if(dc_player_state.landings!=last_landings){landing_time=0;last_landings=dc_player_state.landings;}else landing_time+=dt;
  clip=dc_player_state.mode;clip_time=dc_player_state.anim_time;
  if(clip==DC_PLAYER_AIR){clip=3;if(clip_time>0.4f){clip=4;clip_time-=0.4f;}}
  else if(landing_time<0.18f){clip=5;clip_time=landing_time;}
  dc_play_clip=clip;dc_player_anim_sample(&animation,clip,clip_time,local_pose);
  c=cosf(dc_player_state.yaw);s=sinf(dc_player_state.yaw);
  for(i=0;i<scene.vertex_count;++i){float x=local_pose[i*3],z=local_pose[i*3+2];
   camera_vertices[i*3]=c*x+s*z;camera_vertices[i*3+1]=local_pose[i*3+1]+dc_player_state.y/4-0.1875f;camera_vertices[i*3+2]=-s*x+c*z;
  }
  for(i=0;i<map_count;++i){map_camera[i][0]=(map_vertices[i].x-dc_player_state.x)/4;
   map_camera[i][1]=(map_vertices[i].y-0.75f)/4;map_camera[i][2]=(map_vertices[i].z-dc_player_state.z)/4;}
  /* Unit-sphere character; map caller guarantees finite points. Triangles with
   * nonpositive camera depth are omitted in this deliberately bounded map. */
  if(!dc_scene_project(camera_vertices,12,projected,scene.vertex_count,yaw,-0.5f,2.6f) ||
     !dc_scene_project(map_camera,12,map_projected,map_count,yaw,-0.5f,2.6f)){record_error("projection mode rejected");break;}
  pvr_wait_ready();pvr_scene_begin();pvr_list_begin(PVR_LIST_OP_POLY);draw_map();draw_player(&scene,PVR_LIST_OP_POLY);draw_label();pvr_list_finish();
  pvr_list_begin(PVR_LIST_PT_POLY);draw_player(&scene,PVR_LIST_PT_POLY);pvr_list_finish();pvr_scene_finish();
  dc_telemetry.frames=++frames;dc_telemetry.yaw=yaw;dc_telemetry.pitch=-0.5f;dc_telemetry.distance=2.6f;
 }
 pvr_wait_ready();pvr_mem_free(label_texture);
shutdown:
 release_geometry();pvr_shutdown();
release:
 free(local_pose);free(camera_vertices);dc_player_anim_free(&animation);dc_textured_free(&scene);
 if(!result)dc_telemetry.state=DC_EXIT;
 return result;
}
