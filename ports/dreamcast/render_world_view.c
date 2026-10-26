/* One matrix borrow for the complete packed face batch (AI-assisted), ISC. */
#include "render_world_view.h"
#include <sh4zam/shz_matrix.h>
#include <sh4zam/shz_xmtrx.h>
#include <math.h>
#include <string.h>

static int finite32(float value) {
 uint32_t bits;__builtin_memcpy(&bits,&value,4);
 return (bits&UINT32_C(0x7fffffff))<UINT32_C(0x7f800000);
}
static int target32(float value) {
 uint32_t bits;__builtin_memcpy(&bits,&value,4);
 return (bits&UINT32_C(0x7fffffff))<=UINT32_C(0x49742400); /* 1,000,000 */
}
int dc_render_world_prepare_sh4zam(const dc_render_window *window,dc_render_vertex *out,
                                  uint32_t capacity,const dc_render_camera *camera) {
 uint32_t face,vertex;shz_mat4x4_t saved,matrix;
 float cy,sy,cp,sp;const float scale=DC_RENDER_WORLD_SCALE;
 if(!window||!camera||window->valid!=1||window->ready!=1||window->count>DC_RENDER_WINDOW_CAP||
    (window->count?(!window->faces||!out||((uintptr_t)out&3u)):window->faces!=NULL)||
    capacity<window->count*3u)return 0;
 if(!target32(camera->target.x)||!target32(camera->target.y)||!target32(camera->target.z)||
    !finite32(camera->yaw)||!finite32(camera->pitch)||!finite32(camera->distance)||
    camera->distance<0.f||camera->distance>1000000.f)return 0;
 if(!window->count)return 1;
 /* Include library setup inside the preserved-state interval. */
 shz_xmtrx_store_4x4(&saved);
 cy=cosf(camera->yaw);sy=sinf(camera->yaw);cp=cosf(camera->pitch);sp=sinf(camera->pitch);
 matrix.col[0]=shz_vec4_init(cy*scale,sp*sy*scale,-cp*sy*scale,0);
 matrix.col[1]=shz_vec4_init(0,cp*scale,sp*scale,0);
 matrix.col[2]=shz_vec4_init(sy*scale,-sp*cy*scale,cp*cy*scale,0);
 matrix.col[3]=shz_vec4_init(0,0,camera->distance*scale,1);
 shz_xmtrx_load_4x4(&matrix);
 for(face=0;face<window->count;++face) {
  const unsigned char *record=window->faces+(size_t)face*DC_RENDER_FACE_BYTES+8u;
  for(vertex=0;vertex<3u;++vertex,record+=DC_RENDER_VERTEX_BYTES,++out) {
   float point[3];shz_vec4_t r;
   __builtin_memcpy(point,__builtin_assume_aligned(record,4),sizeof point);
   r=shz_xmtrx_transform_vec4(shz_vec4_init(point[0]-camera->target.x,
       point[1]-camera->target.y,point[2]-camera->target.z,1));
   out->view=(dc_view_vertex){r.x,r.y,r.z};
   if(r.z>=DC_SCENE_NEAR) {
    float inv=1.f/r.z;
    out->projected=(dc_projected_vertex){320.f+390.f*r.x*inv,260.f-390.f*r.y*inv,inv};
   }else out->projected=(dc_projected_vertex){0,0,0};
  }
 }
 shz_xmtrx_load_4x4(&saved);return 1;
}
