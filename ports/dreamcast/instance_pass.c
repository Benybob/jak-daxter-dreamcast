/* Instanced TIE/shrub pass over a DCI1 package (AI-assisted), ISC. */
#include "instance_pass.h"
#include "scene_projection.h"
#include <math.h>
#include <string.h>
#ifdef _arch_dreamcast
#include <kos.h>
#include "render_frustum.h"
#endif

static const char *palette_read(const unsigned char *,size_t,const dc_instance_package *,dc_instance_palette *);
static const dc_instance_package *palette_package;
static const dc_instance_palette *palette_active;

static uint32_t rd32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}

const char *dc_instance_package_open(const void *data,size_t size,dc_instance_package *out) {
 const unsigned char *p=data;dc_instance_package made;uint32_t np,ng,ni,i,nv=0,nx=0,maxv=0;size_t head,at,need,stride;int v2,v3,v4;
 if(!data||!out||((uintptr_t)data&3))return "DCI1 arguments";
 if(size<12)return "DCI1 magic";
 v4=!memcmp(p,"DCI4",4);v3=v4||!memcmp(p,"DCI3",4);v2=v3||!memcmp(p,"DCI2",4);stride=v3?16u:24u;
 if(!v2&&memcmp(p,"DCI1",4))return "DCI1 magic";
 if(v2&&size<16)return "DCI1 magic";
 np=rd32(p+4);ng=v2?rd32(p+8):np;ni=rd32(p+(v2?12:8));head=v2?16u:12u;
 if(np==0||np>65535u||ng==0||ng>65535u||ni>1000000u)return "DCI1 counts";
 if(size<head+52u*(size_t)ng+(v2?24u*(size_t)np:0u))return "DCI1 prototype table";
 for(i=0;i<ng;++i){
  const unsigned char *q=p+head+52*i;uint32_t vc=rd32(q),ic=rd32(q+4),fv=rd32(q+8),fi=rd32(q+12);
  if(fv!=nv||fi!=nx||vc==0||vc>65536u||ic==0||ic%3)return "DCI1 prototype ranges";
  if(q[20+31])return "DCI1 prototype name";
  nv+=vc;nx+=ic;if(vc>maxv)maxv=vc;
  if(nv>(1u<<24)||nx>(1u<<26))return "DCI1 prototype ranges";
 }
 at=head+52u*ng+(v2?24u*np:0u)+(v3?16u*ng:0u);need=at+stride*(size_t)nv+2u*(size_t)nx;
 need=(need+3u)&~(size_t)3u;/* indices pad to 4 bytes */
 if(v4){uint32_t bytes;
  if(size<need||size-need<16||memcmp(p+need,"DCP4",4))return "DCI4 extension";
  bytes=rd32(p+need+4);if(bytes<16||(bytes&3)||bytes>size-need)return "DCI4 extension";
  need+=bytes;
 }
 if(size<need||size-need!=52u*(size_t)ni)return "DCI1 size";
 made.protos=(const dc_instance_proto *)(p+head);
 made.lods=v2?(const dc_instance_lod *)(p+head+52u*ng):NULL;made.lod_count=v2?np:0;made.bounds=NULL;
 made.verts=v3?NULL:(const dc_instance_vertex *)(p+at);
 made.cverts=v3?(const dc_instance_cvertex *)(p+at):NULL;
 made.quant=v3?(const float (*)[4])(p+at-16u*ng):NULL;
 made.indices=(const uint16_t *)(p+at+stride*nv);
 made.instances=(const dc_instance *)(p+need);
 if(v3){
  for(i=0;i<ng;++i){const float *q=made.quant[i];
   if(!isfinite(q[0])||!isfinite(q[1])||!isfinite(q[2])||fabsf(q[0])>1e6f||fabsf(q[1])>1e6f||fabsf(q[2])>1e6f
      ||!(q[3]>0)||q[3]>100.f)return "DCI3 quantization";}
  for(i=0;!v4&&i<nv;++i)if(made.cverts[i].pad)return "DCI1 vertex value";
 }else for(i=0;i<nv;++i){const dc_instance_vertex *v=&made.verts[i];
  if(!isfinite(v->x)||!isfinite(v->y)||!isfinite(v->z)||!isfinite(v->u)||!isfinite(v->v))return "DCI1 vertex value";}
 for(i=0;i<ng;++i){const dc_instance_proto *q=&made.protos[i];uint32_t k;
  for(k=0;k<q->indices;++k)if(made.indices[q->first_index+k]>=q->verts)return "DCI1 index range";}
 for(i=0;v2&&i<np;++i){const dc_instance_lod *l=&made.lods[i];unsigned k;
  if(l->geometry[0]>=ng)return "DCI2 prototype LODs";
  for(k=0;k<4;++k){
   if(l->geometry[k]==0xffffu){if(l->count[k])return "DCI2 prototype LODs";continue;}
   if((uint32_t)l->geometry[k]+l->count[k]>=ng)return "DCI2 prototype LODs";}
  for(k=0;k<3;++k)if(!isfinite(l->plane[k])||!(l->plane[k]>=0)||l->plane[k]>1e18f||(k&&l->plane[k]<l->plane[k-1]))return "DCI2 prototype planes";}
 for(i=0;i<ni;++i){const dc_instance *q=&made.instances[i];unsigned k;
  if(q->proto>=np)return "DCI1 instance prototype";
  for(k=0;k<12;++k)if(!isfinite(q->m[k])||fabsf(q->m[k])>1e6f)return "DCI1 instance matrix";}
 made.proto_count=ng;made.instance_count=ni;made.vert_count=nv;made.index_count=nx;made.max_proto_verts=maxv;
 if(v4){dc_instance_palette lighting;const char *error=palette_read(p,size,&made,&lighting);if(error)return error;}
 *out=made;
 return NULL;
}

/* Palette parsing and lookups (AI-assisted), ISC. No heap or vertex scratch. */
const char *dc_instance_palette_table(const dc_instance_palette *p,uint32_t proto,const void *data,size_t size) {
 const unsigned char *t=data;uint32_t i,n,width,entries;
 if(!p||proto>=p->proto_count||!data)return "DCI4 table arguments";
 n=p->protos[proto].slots;entries=p->protos[proto].entries;width=entries<=256?1u:2u;
 if(size<(size_t)n*width)return "DCI4 table range";
 for(i=0;i<n;++i){uint32_t index=t[i*width];if(width==2)index|=(uint32_t)t[i*width+1]<<8;
  if(index>=entries)return "DCI4 palette index";}
 return NULL;
}
static const char *palette_read(const unsigned char *data,size_t size,const dc_instance_package *pkg,dc_instance_palette *out) {
 dc_instance_palette p;uint32_t i,k;size_t at=(const unsigned char *)pkg->indices-data+2u*pkg->index_count,minimum;
 at=(at+3u)&~(size_t)3u;
 if(size<at||size-at<16||memcmp(data+at,"DCP4",4))return "DCI4 extension";
 memset(&p,0,sizeof p);p.data=data+at;p.size=rd32(p.data+4);p.resident=rd32(p.data+8);p.count=rd32(p.data+12);
 p.proto_count=pkg->lod_count;p.geometry_count=pkg->proto_count;
 minimum=16u+8u*(size_t)p.proto_count+16u*(size_t)p.geometry_count;
 if(p.size>size-at||p.size<minimum||p.resident<minimum||p.resident>p.size||(p.size&3)||(p.resident&3)
    ||p.count!=pkg->instance_count||4u*(size_t)p.count>p.size-p.resident)return "DCI4 ranges";
 p.protos=(const dc_instance_palette_proto *)(p.data+16);
 p.geometries=(const dc_instance_palette_geometry *)(p.data+16+8u*p.proto_count);
 for(i=0;i<p.proto_count;++i){const dc_instance_palette_proto *r=&p.protos[i];
  if(!r->slots||r->slots>65536||!r->entries||r->entries>65536)return "DCI4 counts";
  for(k=0;k<4;++k){const dc_instance_lod *l=&pkg->lods[i];uint32_t g;
   if(l->geometry[k]==0xffffu)continue;
   for(g=l->geometry[k];g<=l->geometry[k]+l->count[k];++g){const dc_instance_palette_geometry *q=&p.geometries[g];
    if(q->slots!=r->slots||q->entries!=r->entries)return "DCI4 prototype palette";}}}
 for(i=0;i<p.geometry_count;++i){const dc_instance_palette_geometry *q=&p.geometries[i];const dc_instance_proto *g=&pkg->protos[i];
  if(!q->entries||q->entries>65536||!q->slots||q->slots>65536||q->width!=(q->entries<=256?1u:2u)
     ||q->offset<minimum||(q->offset&3)||q->offset>p.resident||4u*(size_t)q->entries>p.resident-q->offset)return "DCI4 palette range";
  for(k=0;k<q->entries;++k)if(rd32(p.data+q->offset+4*k)&0xff000000u)return "DCI4 RGB";
  for(k=0;k<g->verts;++k)if(pkg->cverts[g->first_vert+k].pad>=q->slots)return "DCI4 slot";
 }
 for(i=0;i<p.count;++i){uint32_t offset=rd32(p.data+p.resident+4*i);const char *error;
  if((offset&3)||offset<p.resident+4u*p.count||offset>p.size)return "DCI4 table offset";
  error=dc_instance_palette_table(&p,pkg->instances[i].proto,p.data+offset,p.size-offset);if(error)return error;
 }
 *out=p;return NULL;
}
const char *dc_instance_palette_open(const void *data,size_t size,dc_instance_package *pkg,dc_instance_palette *out) {
 dc_instance_package made;dc_instance_palette p;const char *error;
 if(!data||size<4||memcmp(data,"DCI4",4)||!pkg||!out)return "DCI4 arguments";
 error=dc_instance_package_open(data,size,&made);if(error)return error;
 error=palette_read(data,size,&made,&p);if(error)return error;
 *pkg=made;*out=p;return NULL;
}
uint32_t dc_instance_palette_colour(const dc_instance_package *pkg,const dc_instance_palette *p,
                                  const unsigned char *table,uint32_t geometry,uint32_t vertex) {
 const dc_instance_palette_geometry *g=&p->geometries[geometry];
 const dc_instance_cvertex *v=&pkg->cverts[pkg->protos[geometry].first_vert+vertex];
 uint32_t index=table[(uint32_t)v->pad*g->width];
 if(g->width==2)index|=(uint32_t)table[(uint32_t)v->pad*2+1]<<8;
 return (v->argb&0xff000000u)|rd32(p->data+g->offset+4*index);
}
void dc_instance_palette_bind(const dc_instance_package *pkg,const dc_instance_palette *p) {
 palette_package=pkg;palette_active=p;
}

void dc_instance_vertex_get(const dc_instance_package *pkg,uint32_t g,uint32_t k,float o[5],uint32_t *argb) {
 const dc_instance_proto *p=&pkg->protos[g];
 if(pkg->cverts){const dc_instance_cvertex *v=&pkg->cverts[p->first_vert+k];const float *q=pkg->quant[g];
  o[0]=q[0]+v->x*q[3];o[1]=q[1]+v->y*q[3];o[2]=q[2]+v->z*q[3];o[3]=v->u*(1.f/2048.f);o[4]=v->v*(1.f/2048.f);*argb=v->argb;}
 else{const dc_instance_vertex *v=&pkg->verts[p->first_vert+k];
  o[0]=v->x;o[1]=v->y;o[2]=v->z;o[3]=v->u;o[4]=v->v;*argb=v->argb;}
}

void dc_instance_bounds(const dc_instance_package *pkg,float (*out)[4]) {
 uint32_t g,k,argb;
 for(g=0;g<pkg->proto_count;++g){
  const dc_instance_proto *p=&pkg->protos[g];float v[5],lo[3],hi[3],c[3],r2=0;
  dc_instance_vertex_get(pkg,g,0,v,&argb);memcpy(lo,v,sizeof lo);memcpy(hi,v,sizeof hi);
  for(k=1;k<p->verts;++k){unsigned a;dc_instance_vertex_get(pkg,g,k,v,&argb);
   for(a=0;a<3;++a){if(v[a]<lo[a])lo[a]=v[a];if(v[a]>hi[a])hi[a]=v[a];}}
  for(k=0;k<3;++k)c[k]=.5f*(lo[k]+hi[k]);
  for(k=0;k<p->verts;++k){float dx,dy,dz,d;dc_instance_vertex_get(pkg,g,k,v,&argb);
   dx=v[0]-c[0];dy=v[1]-c[1];dz=v[2]-c[2];d=dx*dx+dy*dy+dz*dz;if(d>r2)r2=d;}
  out[g][0]=c[0];out[g][1]=c[1];out[g][2]=c[2];out[g][3]=sqrtf(r2)*1.0001f+1e-4f;
 }
}

/* A sphere (view space, view units) wholly behind the near plane or outside one
 * of the planes through the eye and the viewport edges of x=320+390x/z,
 * y=260-390y/z on 640x480. */
int dc_instance_sphere_outside(const float c[3],float r) {
 if(c[2]+r<DC_SCENE_NEAR)return 1;
 if(390.f*c[0]-320.f*c[2]>r*504.48f)return 1;   /* right: |(390,0,-320)| */
 if(-390.f*c[0]-320.f*c[2]>r*504.48f)return 1;  /* left */
 if(390.f*c[1]-260.f*c[2]>r*468.72f)return 1;   /* top: |(390,-260)| */
 if(-390.f*c[1]-220.f*c[2]>r*447.77f)return 1;  /* bottom: |(390,220)| */
 return 0;
}

uint32_t dc_instance_run(const dc_instance_package *pkg,const dc_instance *in,float d2,unsigned *lod,unsigned *count) {
 const dc_instance_lod *l;unsigned n=0,k;
 if(!pkg->lods){*lod=0;*count=1;return in->proto;}
 l=&pkg->lods[in->proto];
 for(k=0;k<3;++k){float p=l->plane[k]*l->plane[k];if(d2>p)++n;}
 while(l->geometry[n]==0xffffu)--n;
 *lod=n;*count=1u+l->count[n];return l->geometry[n];
}
uint32_t dc_instance_geometry(const dc_instance_package *pkg,const dc_instance *in,float d2,unsigned *lod) {
 unsigned count;return dc_instance_run(pkg,in,d2,lod,&count);
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
static pvr_poly_hdr_t colour_hdr __attribute__((aligned(32)));
/* The instance matrix and the camera as one 4x4 for XMTRX (column-major,
 * matrix_t[column][row]): view = B (M p + t - target) + (0, 0, distance), with
 * the translation taken relative to the target before the rotation. */
static void compose(const float b[10],const dc_instance_camera *cam,const float *m,matrix_t out) {
 const float t[3]={m[9]-cam->tx,m[10]-cam->ty,m[11]-cam->tz};unsigned c,r;
 for(c=0;c<3;++c)for(r=0;r<3;++r)out[c][r]=b[3*r]*m[3*c]+b[3*r+1]*m[3*c+1]+b[3*r+2]*m[3*c+2];
 for(r=0;r<3;++r)out[3][r]=b[3*r]*t[0]+b[3*r+1]*t[1]+b[3*r+2]*t[2];
 out[3][2]+=b[9];
 out[0][3]=out[1][3]=out[2][3]=0;out[3][3]=1;
}
static int draw(const dc_instance_package *pkg,const dc_instance_camera *cam,float (*view)[3],dc_instance_stats *st,
                int list,const dc_instance_material *materials,uint32_t material_count) {
 float b[10];uint32_t i;unsigned queued=0;const void *current=NULL;pvr_poly_cxt_t cxt;int counting=list==PVR_LIST_OP_POLY;
 if(!pkg||!cam||!view||!st||!(cam->distance>=0)||!(cam->draw_distance>0))return 0;
 dc_instance_view(cam,b);
 pvr_poly_cxt_col(&cxt,PVR_LIST_OP_POLY);cxt.gen.culling=PVR_CULLING_NONE;pvr_poly_compile(&colour_hdr,&cxt);
 for(i=0;i<pkg->instance_count;++i){
  const dc_instance *in=&pkg->instances[i];
  const dc_instance_palette *lighting=palette_package==pkg?palette_active:NULL;
  const unsigned char *table=NULL;
  const float *m=in->m;float dx=m[9]-cam->tx,dz=m[11]-cam->tz,dx2=dx*dx,dz2=dz*dz,d2=dx2+dz2;
  uint32_t first,g;unsigned lod,count;
  if(counting)++st->instances;
  if(d2>cam->draw_distance*cam->draw_distance)continue;
  if(lighting)table=lighting->tables?lighting->tables[i]:lighting->data+rd32(lighting->data+lighting->resident+4*i);
  first=dc_instance_run(pkg,in,d2,&lod,&count);
  if(counting){++st->drawn_instances;++st->lod_instances[lod];}
  static matrix_t mv __attribute__((aligned(32)));compose(b,cam,m,mv);
  if(!pkg->quant)mat_load(&mv);
  for(g=first;g<first+count;++g){
   const dc_instance_proto *p=&pkg->protos[g];const void *header=NULL;uint32_t k;int textured=0,single=0;
   if(materials&&p->material<material_count&&materials[p->material].header){
    if(materials[p->material].list!=(uint32_t)list)continue;
    header=materials[p->material].header;textured=1;single=materials[p->material].single_sided!=0;
   }else{
    if(list!=PVR_LIST_OP_POLY)continue;
    header=&colour_hdr;
   }
   if(pkg->bounds){const float *s=pkg->bounds[g];float w[3],c[3],r0,r1,r2;
    w[0]=s[0]*m[0]+s[1]*m[3]+s[2]*m[6]+m[9];w[1]=s[0]*m[1]+s[1]*m[4]+s[2]*m[7]+m[10];w[2]=s[0]*m[2]+s[1]*m[5]+s[2]*m[8]+m[11];
    dc_instance_to_view(b,cam,w,c);
    r0=m[0]*m[0]+m[1]*m[1]+m[2]*m[2];r1=m[3]*m[3]+m[4]*m[4]+m[5]*m[5];r2=m[6]*m[6]+m[7]*m[7]+m[8]*m[8];
    if(r1>r0){r0=r1;}
    if(r2>r0){r0=r2;}
    if(dc_instance_sphere_outside(c,s[3]*sqrtf(r0)*0.25f/* DC_RENDER_WORLD_SCALE */)){st->triangles+=p->indices/3u;++st->sphere_culled;continue;}}
   if(header!=current){flush(&queued);pvr_prim(header,sizeof(pvr_poly_hdr_t));current=header;}
   /* every vertex through XMTRX: one ftrv each. DCI3 folds the geometry's
    * centre and scale into the matrix, leaving the 16-bit steps to convert. */
   if(pkg->quant){static matrix_t mq __attribute__((aligned(32)));const float *q=pkg->quant[g];unsigned c,r;
    for(c=0;c<3;++c)for(r=0;r<4;++r)mq[c][r]=mv[c][r]*q[3];
    for(r=0;r<4;++r)mq[3][r]=mv[0][r]*q[0]+mv[1][r]*q[1]+mv[2][r]*q[2]+mv[3][r];
    mat_load(&mq);
    for(k=0;k<p->verts;++k){const dc_instance_cvertex *v=&pkg->cverts[p->first_vert+k];float x=v->x,y=v->y,z=v->z;
     mat_trans_single3_nodiv(x,y,z);view[k][0]=x;view[k][1]=y;view[k][2]=z;}
   }else
   for(k=0;k<p->verts;++k){const dc_instance_vertex *v=&pkg->verts[p->first_vert+k];float x=v->x,y=v->y,z=v->z;
    mat_trans_single3_nodiv(x,y,z);view[k][0]=x;view[k][1]=y;view[k][2]=z;}
   for(k=0;k<p->indices;k+=3){
    const uint16_t *ix=&pkg->indices[p->first_index+k];dc_clip_vertex input[3],poly[DC_RENDER_CLIP_CAP];
    unsigned j,front=0;int n,fan;float px[DC_RENDER_CLIP_CAP],py[DC_RENDER_CLIP_CAP],pz[DC_RENDER_CLIP_CAP];
    ++st->triangles;
    for(j=0;j<3;++j){const float *v=view[ix[j]];float u=0,w=0;uint32_t argb;
     if(pkg->cverts){const dc_instance_cvertex *s=&pkg->cverts[p->first_vert+ix[j]];argb=lighting?dc_instance_palette_colour(pkg,lighting,table,g,ix[j]):s->argb;
      if(textured){u=s->u*(1.f/2048.f);w=s->v*(1.f/2048.f);}}
     else{const dc_instance_vertex *s=&pkg->verts[p->first_vert+ix[j]];argb=s->argb;if(textured){u=s->u;w=s->v;}}
     front+=v[2]>=DC_SCENE_NEAR;
     input[j]=(dc_clip_vertex){v[0],v[1],v[2],u,w,argb};}
    /* wholly inside the near plane and the viewport, which dc_render_clip_viewport
     * returns unchanged: no clip call. The planes are pulled in by 0.1 pixel so
     * float rounding cannot accept what the clipper's exact test would cut. */
    {int inside=1;
     for(j=0;j<3&&inside;++j){const float x=input[j].x,y=input[j].y,z=input[j].z;
      inside=z>=DC_SCENE_NEAR&&z<=4e6f&&390.f*x<=319.9f*z&&-390.f*x<=319.9f*z&&390.f*y<=259.9f*z&&-390.f*y<=219.9f*z;}
     if(inside){memcpy(poly,input,sizeof input);n=3;}
     else n=dc_render_clip_viewport(input,poly);}
    if(n<0){++st->invalid;flush(&queued);return 0;}
    if(n<3){++st->culled;continue;}
    if(front<3)++st->near_clipped;
    for(j=0;j<(unsigned)n;++j){float inv=1.f/poly[j].z;px[j]=320.f+390.f*poly[j].x*inv;py[j]=260.f-390.f*poly[j].y*inv;pz[j]=inv;}
    for(fan=1;fan<n-1;++fan){
     unsigned ids[3]={0,(unsigned)fan,(unsigned)fan+1};
     {float area=(px[fan]-px[0])*(py[fan+1]-py[0])-(py[fan]-py[0])*(px[fan+1]-px[0]);
      if(area==0){++st->culled;continue;}
      if(single&&area<0){++st->culled;++st->back_faces;continue;}}
     for(j=0;j<3;++j){unsigned q=ids[j];pvr_vertex_t *v=&commands[queued++];
      v->flags=j==2?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX;v->x=px[q];v->y=py[q];v->z=pz[q];
      v->u=poly[q].u;v->v=poly[q].v;v->argb=poly[q].argb;v->oargb=0;}
     ++st->submitted;if(queued==96)flush(&queued);
    }
   }
  }
 }
 flush(&queued);
 return 1;
}
int dc_instance_draw(const dc_instance_package *pkg,const dc_instance_camera *cam,float (*view)[3],dc_instance_stats *st) {
 return draw(pkg,cam,view,st,PVR_LIST_OP_POLY,NULL,0);
}
int dc_instance_draw_list(const dc_instance_package *pkg,const dc_instance_camera *cam,float (*view)[3],dc_instance_stats *st,
                          int list,const dc_instance_material *materials,uint32_t material_count) {
 if(!materials||(list!=PVR_LIST_OP_POLY&&list!=PVR_LIST_PT_POLY))return 0;
 return draw(pkg,cam,view,st,list,materials,material_count);
}
#endif
