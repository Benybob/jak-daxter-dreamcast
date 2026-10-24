/* SH4ZAM viewer batch transform (AI-assisted), ISC. Uses upstream library. */
#include "scene_projection.h"
#include <sh4zam/shz_matrix.h>
#include <sh4zam/shz_xmtrx.h>
#include <math.h>
#include <string.h>
int dc_scene_project_sh4zam(const void *input,size_t stride,dc_projected_vertex *out,
                     uint32_t count,float yaw,float pitch,float distance){
 uint32_t i;const unsigned char *p=input;shz_mat4x4_t saved,matrix;
 float cy,sy,cp,sp;
 cy=cosf(yaw);sy=sinf(yaw);cp=cosf(pitch);sp=sinf(pitch);
 matrix.col[0]=shz_vec4_init(cy,sp*sy,-cp*sy,0);
 matrix.col[1]=shz_vec4_init(0,cp,sp,0);
 matrix.col[2]=shz_vec4_init(sy,-sp*cy,cp*cy,0);
 matrix.col[3]=shz_vec4_init(0,0,distance,1);
 shz_xmtrx_store_4x4(&saved);shz_xmtrx_load_4x4(&matrix);
 for(i=0;i<count;++i,p+=stride){
  float v[3],inv;shz_vec4_t r;__builtin_memcpy(v,__builtin_assume_aligned(p,4),sizeof v);
  r=shz_xmtrx_transform_vec4(shz_vec4_init(v[0],v[1],v[2],1));inv=1.0f/r.z;
  out[i].x=320.0f+390.0f*r.x*inv;out[i].y=260.0f-390.0f*r.y*inv;out[i].z=inv;
 }
 shz_xmtrx_load_4x4(&saved);return 1;
}




