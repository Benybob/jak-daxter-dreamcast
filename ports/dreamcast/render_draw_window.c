/* Fixed-buffer indexed window: ownership, view cache, record form (AI-assisted), ISC. */
#include "render_draw_window.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef DC_RENDER_BLOCK_CACHE_TEST
extern void *dc_rbc_test_malloc(size_t);
extern void dc_rbc_test_free(void *);
#define ALLOC dc_rbc_test_malloc
#define FREE dc_rbc_test_free
#else
#define ALLOC malloc
#define FREE free
#endif

static int finite32(float value) {
 uint32_t bits;__builtin_memcpy(&bits,&value,4);
 return (bits&UINT32_C(0x7fffffff))<UINT32_C(0x7f800000);
}
static int target32(float value) {
 uint32_t bits;__builtin_memcpy(&bits,&value,4);
 return (bits&UINT32_C(0x7fffffff))<=UINT32_C(0x49742400); /* 1,000,000 */
}
const char *dc_render_draw_window_make(dc_render_draw_window *out,uint32_t face_cap,
    uint32_t vertex_cap,uint32_t remap_cap) {
 dc_render_draw_window made;
 if(!out||out->faces||out->vertices||out->view||out->order||out->remap)return "draw window owned output";
 if(!face_cap||face_cap>65535u||!vertex_cap||vertex_cap>65535u||!remap_cap||remap_cap>65535u)return "draw window capacity";
 memset(&made,0,sizeof made);
 made.face_cap=face_cap;made.vertex_cap=vertex_cap;made.remap_cap=remap_cap;
 made.faces=ALLOC((size_t)face_cap*sizeof(*made.faces));
 made.vertices=ALLOC((size_t)vertex_cap*DC_RENDER_DRAW_VERTEX_BYTES);
 made.view=ALLOC((size_t)vertex_cap*sizeof(*made.view));
 made.order=ALLOC((size_t)face_cap*sizeof(*made.order));
 made.remap=ALLOC((size_t)remap_cap*sizeof(*made.remap));
 if(!made.faces||!made.vertices||!made.view||!made.order||!made.remap) {
  dc_render_draw_window_free(&made);return "draw window allocation";
 }
 *out=made;return NULL;
}
void dc_render_draw_window_free(dc_render_draw_window *window) {
 if(!window)return;
 FREE(window->faces);FREE(window->vertices);FREE(window->view);FREE(window->order);FREE(window->remap);
 memset(window,0,sizeof(*window));
}
int dc_render_draw_window_prepare(dc_render_draw_window *window,const dc_render_camera *camera) {
 uint32_t i;float cy,sy,cp,sp,m[12];const float scale=DC_RENDER_WORLD_SCALE;const unsigned char *point;
 if(!window)return 0;
 window->prepared=0;
 if(!camera||!window->valid)return 0;
 if(!target32(camera->target.x)||!target32(camera->target.y)||!target32(camera->target.z)||
    !finite32(camera->yaw)||!finite32(camera->pitch)||!finite32(camera->distance)||
    camera->distance<0.f||camera->distance>1000000.f)return 0;
 cy=cosf(camera->yaw);sy=sinf(camera->yaw);cp=cosf(camera->pitch);sp=sinf(camera->pitch);
 m[0]=cy*scale;m[1]=0;m[2]=sy*scale;m[3]=0;
 m[4]=sp*sy*scale;m[5]=cp*scale;m[6]=-sp*cy*scale;m[7]=0;
 m[8]=-cp*sy*scale;m[9]=sp*scale;m[10]=cp*cy*scale;m[11]=camera->distance*scale;
 point=window->vertices;
 for(i=0;i<window->vertex_count;++i,point+=DC_RENDER_DRAW_VERTEX_BYTES) {
  float p[3],x,y,z;dc_view_vertex *out=&window->view[i];
  __builtin_memcpy(p,point,sizeof p);
  x=p[0]-camera->target.x;y=p[1]-camera->target.y;z=p[2]-camera->target.z;
  out->x=m[0]*x+m[1]*y+m[2]*z+m[3];out->y=m[4]*x+m[5]*y+m[6]*z+m[7];out->z=m[8]*x+m[9]*y+m[10]*z+m[11];
 }
 window->prepared=1;return 1;
}
void dc_render_draw_window_face(const dc_render_draw_window *window,uint32_t index,unsigned char *out) {
 const dc_render_draw_face *f=&window->faces[index];uint32_t material=f->material;unsigned j;
 memset(out,0,116);
 out[0]=(unsigned char)f->id;out[1]=(unsigned char)(f->id>>8);out[2]=(unsigned char)(f->id>>16);out[3]=(unsigned char)(f->id>>24);
 out[4]=(unsigned char)material;out[5]=(unsigned char)(material>>8);
 for(j=0;j<3;++j) {
  const unsigned char *v=window->vertices+(size_t)f->v[j]*DC_RENDER_DRAW_VERTEX_BYTES;
  memcpy(out+8+j*36,v,20);memcpy(out+8+j*36+32,v+20,4);
 }
}
