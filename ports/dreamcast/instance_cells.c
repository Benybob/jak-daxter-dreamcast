/* Cells in, cells out, original order kept (AI-assisted), ISC. */
#include "instance_cells.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char instance_record_size[sizeof(dc_instance)==52?1:-1];

const char *dc_instance_cells_make(dc_instance_cells *out,const char *root,double cell,uint32_t capacity) {
 if(!out||out->records)return "instance cells owned output";
 if(!root||strlen(root)>=sizeof out->root||!(cell>0)||!isfinite(cell)||!capacity||capacity>65536u)return "instance cells arguments";
 memset(out,0,sizeof(*out));
 out->records=malloc((size_t)capacity*sizeof(*out->records));
 if(!out->records)return "instance cells allocation";
 out->capacity=capacity;out->cell=cell;strcpy(out->root,root);
 return NULL;
}
void dc_instance_cells_free(dc_instance_cells *cells) {
 if(!cells)return;
 free(cells->records);memset(cells,0,sizeof(*cells));
}
static int touches(const dc_instance_cells *c,int32_t ix,int32_t iz,double x,double z,double draw) {
 double x0=(double)ix*c->cell,x1=x0+c->cell,z0=(double)iz*c->cell,z1=z0+c->cell;
 double dx=x<x0?x0-x:(x>x1?x-x1:0),dz=z<z0?z0-z:(z>z1?z-z1:0);
 return dx*dx+dz*dz<=draw*draw;
}
static int32_t cell_of(const dc_instance_cells *c,float value) {return (int32_t)floor((double)value/c->cell);}
static int by_index(const void *a,const void *b) {
 unsigned x=((const dc_instance *)a)->pad,y=((const dc_instance *)b)->pad;
 return (x>y)-(x<y);
}
static const char *read_cell(dc_instance_cells *c,const dc_instance_package *pkg,int32_t ix,int32_t iz) {
 char path[256];FILE *file;long size;uint32_t n,i,limit=pkg->lods?pkg->lod_count:pkg->proto_count;dc_instance *in=c->records+c->count;
 snprintf(path,sizeof path,"%s/cells/%" PRId32 "_%" PRId32 ".dci",c->root,ix,iz);
 if(c->cell_count>=DC_INSTANCE_CELLS_MAX)return "instance cells table";
 file=fopen(path,"rb");
 if(!file){c->cells[c->cell_count++]=(dc_instance_cell){ix,iz,0};return NULL;}
 if(fseek(file,0,SEEK_END)||(size=ftell(file))<=0||size%52||fseek(file,0,SEEK_SET)){fclose(file);return "instance cell size";}
 n=(uint32_t)(size/52);
 if(n>c->capacity-c->count){fclose(file);return "instance cells capacity";}
 if(fread(in,52,n,file)!=n||fgetc(file)!=EOF){fclose(file);return "instance cell read";}
 fclose(file);
 for(i=0;i<n;++i) {
  unsigned k;
  if(in[i].proto>=limit||(i&&in[i].pad<=in[i-1].pad))return "instance cell record";
  for(k=0;k<12;++k)if(!isfinite(in[i].m[k])||fabsf(in[i].m[k])>1e6f)return "instance cell record";
  if(cell_of(c,in[i].m[9])!=ix||cell_of(c,in[i].m[11])!=iz)return "instance cell placement";
 }
 c->cells[c->cell_count++]=(dc_instance_cell){ix,iz,n};c->count+=n;
 return NULL;
}
const char *dc_instance_cells_update(dc_instance_cells *c,dc_instance_package *pkg,double x,double z,double draw) {
 uint32_t i,kept=0,out=0;int32_t ix,iz,x0,x1,z0,z1;const char *error=NULL;int changed=0;
 if(!c||!c->records||!pkg||!(draw>0)||!isfinite(x)||!isfinite(z))return "instance cells arguments";
 for(i=0;i<c->cell_count;++i) {
  if(touches(c,c->cells[i].ix,c->cells[i].iz,x,z,draw))c->cells[kept++]=c->cells[i];
  else if(c->cells[i].count)changed=1;
 }
 c->cell_count=kept;
 if(changed) {
  for(i=0;i<c->count;++i) {
   int32_t a=cell_of(c,c->records[i].m[9]),b=cell_of(c,c->records[i].m[11]);uint32_t j;
   for(j=0;j<kept;++j)if(c->cells[j].ix==a&&c->cells[j].iz==b)break;
   if(j<kept){if(out!=i)c->records[out]=c->records[i];++out;}
  }
  c->count=out;
 }
 /* One cell wider below: a closed disc can touch a cell only at its far edge. */
 x0=(int32_t)floor((x-draw)/c->cell)-1;x1=(int32_t)floor((x+draw)/c->cell);
 z0=(int32_t)floor((z-draw)/c->cell)-1;z1=(int32_t)floor((z+draw)/c->cell);
 for(ix=x0;ix<=x1&&!error;++ix)for(iz=z0;iz<=z1&&!error;++iz) {
  uint32_t j,before=c->count;
  if(!touches(c,ix,iz,x,z,draw))continue;
  for(j=0;j<c->cell_count;++j)if(c->cells[j].ix==ix&&c->cells[j].iz==iz)break;
  if(j<c->cell_count)continue;
  error=read_cell(c,pkg,ix,iz);
  if(c->count!=before)changed=1;
 }
 if(changed)qsort(c->records,c->count,sizeof(*c->records),by_index);
 pkg->instances=c->records;pkg->instance_count=c->count;
 return error;
}
