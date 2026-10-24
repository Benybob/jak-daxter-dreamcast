/* Independent scalar viewer projection (AI-assisted), ISC. */
#include "scene_projection.h"
#include <math.h>
#include <string.h>
int dc_scene_project_reference(const void *input,size_t stride,dc_projected_vertex *out,
                              uint32_t count,float yaw,float pitch,float distance){
 uint32_t i;const unsigned char *p=input;
 float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
 for(i=0;i<count;++i,p+=stride){
  float v[3],x,y,z,inv;__builtin_memcpy(v,__builtin_assume_aligned(p,4),sizeof v);
  x=cy*v[0]+sy*v[2];z=-sy*v[0]+cy*v[2];y=cp*v[1]-sp*z;
  z=sp*v[1]+cp*z+distance;inv=1.0f/z;
  out[i].x=320.0f+390.0f*x*inv;out[i].y=260.0f-390.0f*y*inv;out[i].z=inv;
 }
 return 1;
}



