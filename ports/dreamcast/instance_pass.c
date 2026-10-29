/* Instanced TIE/shrub pass over a DCI1 package (AI-assisted), ISC. */
#include "instance_pass.h"
#include <math.h>
#include <string.h>
#ifdef _arch_dreamcast
#include <kos.h>
#include "render_frustum.h"
#endif

static uint32_t rd32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}

const char *dc_instance_package_open(const void *data,size_t size,dc_instance_package *out) {
 const unsigned char *p=data;dc_instance_package made;uint32_t np,ni,i,nv=0,nx=0,maxv=0;size_t at,need;
 if(!data||!out||((uintptr_t)data&3))return "DCI1 arguments";
 if(size<12||memcmp(p,"DCI1",4))return "DCI1 magic";
 np=rd32(p+4);ni=rd32(p+8);
 if(np==0||np>65535u||ni>1000000u)return "DCI1 counts";
 if(size<12u+52u*(size_t)np)return "DCI1 prototype table";
 for(i=0;i<np;++i){
  const unsigned char *q=p+12+52*i;uint32_t vc=rd32(q),ic=rd32(q+4),fv=rd32(q+8),fi=rd32(q+12);
  if(fv!=nv||fi!=nx||vc==0||vc>65536u||ic==0||ic%3)return "DCI1 prototype ranges";
  if(q[20+31])return "DCI1 prototype name";
  nv+=vc;nx+=ic;if(vc>maxv)maxv=vc;
  if(nv>(1u<<24)||nx>(1u<<26))return "DCI1 prototype ranges";
 }
 at=12u+52u*np;need=at+24u*(size_t)nv+2u*(size_t)nx;
 need=(need+3u)&~(size_t)3u;/* indices pad to 4 bytes */
 if(size<need||size-need!=52u*(size_t)ni)return "DCI1 size";
 made.protos=(const dc_instance_proto *)(p+12);
 made.verts=(const dc_instance_vertex *)(p+at);
 made.indices=(const uint16_t *)(p+at+24u*nv);
 made.instances=(const dc_instance *)(p+need);
 for(i=0;i<nv;++i){const dc_instance_vertex *v=&made.verts[i];
  if(!isfinite(v->x)||!isfinite(v->y)||!isfinite(v->z)||!isfinite(v->u)||!isfinite(v->v))return "DCI1 vertex value";}
 for(i=0;i<np;++i){const dc_instance_proto *q=&made.protos[i];uint32_t k;
  for(k=0;k<q->indices;++k)if(made.indices[q->first_index+k]>=q->verts)return "DCI1 index range";}
 for(i=0;i<ni;++i){const dc_instance *q=&made.instances[i];unsigned k;
  if(q->proto>=np)return "DCI1 instance prototype";
  for(k=0;k<12;++k)if(!isfinite(q->m[k])||fabsf(q->m[k])>1e6f)return "DCI1 instance matrix";}
 made.proto_count=np;made.instance_count=ni;made.vert_count=nv;made.index_count=nx;made.max_proto_verts=maxv;
 *out=made;
 return NULL;
}

void dc_instance_view(const dc_instance_camera *c,float b[10]) {
 const float s=0.25f;/* DC_RENDER_WORLD_SCALE */
 float cy=cosf(c->yaw),sy=sinf(c->yaw),cp=cosf(c->pitch),sp=sinf(c->pitch);
 b[0]=cy*s;b[1]=0;b[2]=sy*s;
 b[3]=sp*sy*s;b[4]=cp*s;b[5]=-sp*cy*s;
 b[6]=-cp*sy*s;b[7]=sp*s;b[8]=cp*cy*s;b[9]=c->distance*s;
}
void dc_instance_to_view(const float b[10],const dc_instance_camera *c,const float w[3],float v[3]) {
 float x=w[0]-c->tx,y=w[1]-c->ty,z=w[2]-c->tz;
 v[0]=b[0]*x+b[1]*y+b[2]*z;v[1]=b[3]*x+b[4]*y+b[5]*z;v[2]=b[6]*x+b[7]*y+b[8]*z+b[9];
}

#ifdef _arch_dreamcast
static pvr_vertex_t commands[96] __attribute__((aligned(32)));
static void flush(unsigned *n) {if(*n){pvr_prim(commands,*n*sizeof(*commands));*n=0;}}
int dc_instance_draw(const dc_instance_package *pkg,const dc_instance_camera *cam,float (*view)[3],dc_instance_stats *st) {
 float b[10];uint32_t i;unsigned queued=0;pvr_poly_cxt_t cxt;static pvr_poly_hdr_t hdr __attribute__((aligned(32)));
 if(!pkg||!cam||!view||!st||!(cam->distance>=0)||!(cam->draw_distance>0))return 0;
 dc_instance_view(cam,b);
 pvr_poly_cxt_col(&cxt,PVR_LIST_OP_POLY);cxt.gen.culling=PVR_CULLING_NONE;pvr_poly_compile(&hdr,&cxt);
 pvr_prim(&hdr,sizeof hdr);
 for(i=0;i<pkg->instance_count;++i){
  const dc_instance *in=&pkg->instances[i];const dc_instance_proto *p=&pkg->protos[in->proto];
  const float *m=in->m;float dx=m[9]-cam->tx,dz=m[11]-cam->tz;uint32_t k;
  ++st->instances;
  if(dx*dx+dz*dz>cam->draw_distance*cam->draw_distance)continue;
  ++st->drawn_instances;
  for(k=0;k<p->verts;++k){const dc_instance_vertex *v=&pkg->verts[p->first_vert+k];float w[3];
   w[0]=v->x*m[0]+v->y*m[3]+v->z*m[6]+m[9];w[1]=v->x*m[1]+v->y*m[4]+v->z*m[7]+m[10];w[2]=v->x*m[2]+v->y*m[5]+v->z*m[8]+m[11];
   dc_instance_to_view(b,cam,w,view[k]);}
  for(k=0;k<p->indices;k+=3){
   const uint16_t *ix=&pkg->indices[p->first_index+k];dc_clip_vertex input[3],poly[DC_RENDER_CLIP_CAP];
   unsigned j,front=0;int n,fan;float px[DC_RENDER_CLIP_CAP],py[DC_RENDER_CLIP_CAP],pz[DC_RENDER_CLIP_CAP];
   ++st->triangles;
   for(j=0;j<3;++j){const float *v=view[ix[j]];front+=v[2]>=DC_SCENE_NEAR;
    input[j]=(dc_clip_vertex){v[0],v[1],v[2],0,0,pkg->verts[p->first_vert+ix[j]].argb};}
   n=dc_render_clip_viewport(input,poly);
   if(n<0){++st->invalid;flush(&queued);return 0;}
   if(n<3){++st->culled;continue;}
   if(front<3)++st->near_clipped;
   for(j=0;j<(unsigned)n;++j){float inv=1.f/poly[j].z;px[j]=320.f+390.f*poly[j].x*inv;py[j]=260.f-390.f*poly[j].y*inv;pz[j]=inv;}
   for(fan=1;fan<n-1;++fan){
    unsigned ids[3]={0,(unsigned)fan,(unsigned)fan+1};
    if((px[fan]-px[0])*(py[fan+1]-py[0])-(py[fan]-py[0])*(px[fan+1]-px[0])==0){++st->culled;continue;}
    for(j=0;j<3;++j){unsigned q=ids[j];pvr_vertex_t *v=&commands[queued++];
     v->flags=j==2?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;v->x=px[q];v->y=py[q];v->z=pz[q];
     v->u=v->v=0;v->argb=poly[q].argb;v->oargb=0;}
    ++st->submitted;if(queued==96)flush(&queued);
   }
  }
 }
 flush(&queued);
 return 1;
}
#endif
