/* Cells in, cells out, original order kept (AI-assisted), ISC. */
#include "instance_cells.h"
#include <errno.h>
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
const char *dc_instance_cells_palette_make(dc_instance_cells *c,const char *root,double cell,
                                          uint32_t capacity,dc_instance_palette *p,uint32_t bytes) {
 const char *error;
 if(!p||!bytes)return "instance palette arguments";
 error=dc_instance_cells_make(c,root,cell,capacity);if(error)return error;
 c->lighting=malloc(bytes);c->tables=malloc((size_t)capacity*sizeof(*c->tables));
 if(!c->lighting||!c->tables){dc_instance_cells_free(c);return "instance palette allocation";}
 c->palette=p;c->lighting_capacity=bytes;p->tables=c->tables;return NULL;
}
void dc_instance_cells_free(dc_instance_cells *cells) {
 if(!cells)return;
 if(cells->palette)cells->palette->tables=NULL;
 free(cells->lighting);free(cells->tables);free(cells->records);memset(cells,0,sizeof(*cells));
}
/* instance_pass.c accepts on single-precision distances, which round: a record a
 * hair past the exact radius can still be drawn. Far more than that rounding. */
#define DC_INSTANCE_CELLS_MARGIN 0.125
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
static uint32_t rd32(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static const char *read_palette_cell(dc_instance_cells *c,const dc_instance_package *pkg,FILE *file,
                                    long size,int32_t ix,int32_t iz) {
 unsigned char *data=c->lighting+c->lighting_used;uint32_t n,i;dc_instance *in=c->records+c->count;
 if(size<16||(size&3)||(unsigned long)size>c->lighting_capacity-c->lighting_used)return "instance palette capacity";
 if(fread(data,1,(size_t)size,file)!=(size_t)size||fgetc(file)!=EOF)return "instance palette read";
 n=rd32(data+8);
 if(memcmp(data,"DCL4",4)||rd32(data+4)!=(uint32_t)size||rd32(data+12)||!n||n>c->capacity-c->count
    ||56u*(size_t)n>(size_t)size-16)return "instance palette header";
 for(i=0;i<n;++i){uint32_t k,offset=rd32(data+16+56*i+52);const char *error;
  memcpy(&in[i],data+16+56*i,52);
  if(in[i].proto>=pkg->lod_count||(i&&in[i].pad<=in[i-1].pad))return "instance cell record";
  for(k=0;k<c->count;++k)if(c->records[k].pad==in[i].pad)return "instance cell duplicate";
  for(k=0;k<12;++k)if(!isfinite(in[i].m[k])||fabsf(in[i].m[k])>1e6f)return "instance cell record";
  if(cell_of(c,in[i].m[9])!=ix||cell_of(c,in[i].m[11])!=iz)return "instance cell placement";
  if((offset&3)||offset<16u+56u*n||offset>(uint32_t)size)return "instance palette offset";
  error=dc_instance_palette_table(c->palette,in[i].proto,data+offset,(size_t)size-offset);if(error)return error;
 }
 c->cells[c->cell_count++]=(dc_instance_cell){ix,iz,n,c->lighting_used,(uint32_t)size};
 c->lighting_used+=(uint32_t)size;c->count+=n;return NULL;
}
static const char *read_cell(dc_instance_cells *c,const dc_instance_package *pkg,int32_t ix,int32_t iz) {
 char path[256];FILE *file;long size;uint32_t n,i,limit=pkg->lods?pkg->lod_count:pkg->proto_count;dc_instance *in=c->records+c->count;
 snprintf(path,sizeof path,"%s/cells/%" PRId32 "_%" PRId32 ".dci",c->root,ix,iz);
 if(c->cell_count>=DC_INSTANCE_CELLS_MAX)return "instance cells table";
 errno=0;file=fopen(path,"rb");
 if(!file) {
  if(errno!=ENOENT)return "instance cell open";
  c->cells[c->cell_count++]=(dc_instance_cell){ix,iz,0,0,0};return NULL;
 }
 if(fseek(file,0,SEEK_END)||(size=ftell(file))<=0||(!c->palette&&size%52)||fseek(file,0,SEEK_SET)){fclose(file);return "instance cell size";}
 if(c->palette){const char *error=read_palette_cell(c,pkg,file,size,ix,iz);fclose(file);return error;}
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
 c->cells[c->cell_count++]=(dc_instance_cell){ix,iz,n,0,0};c->count+=n;
 return NULL;
}
const char *dc_instance_cells_update(dc_instance_cells *c,dc_instance_package *pkg,double x,double z,double draw) {
 uint32_t i,kept=0,out=0,bytes=0;int32_t ix,iz,x0,x1,z0,z1;const char *error=NULL;int changed=0;
 if(!c||!c->records||!pkg||!(draw>0)||!isfinite(draw)||!isfinite(x)||!isfinite(z))return "instance cells arguments";
 if((fabs(x)+draw)/c->cell>=1073741824.0||(fabs(z)+draw)/c->cell>=1073741824.0)return "instance cells arguments";
 draw+=DC_INSTANCE_CELLS_MARGIN;
 for(i=0;i<c->cell_count;++i) {
  if(touches(c,c->cells[i].ix,c->cells[i].iz,x,z,draw)){
   dc_instance_cell cell=c->cells[i];
   if(cell.bytes){memmove(c->lighting+bytes,c->lighting+cell.offset,cell.bytes);cell.offset=bytes;bytes+=cell.bytes;}
   c->cells[kept++]=cell;
  }
  else if(c->cells[i].count)changed=1;
 }
 c->cell_count=kept;c->lighting_used=bytes;
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
 if(c->palette){
  for(i=0;i<c->cell_count;++i){const dc_instance_cell *cell=&c->cells[i];uint32_t k;
   for(k=0;k<cell->count;++k){const unsigned char *row=c->lighting+cell->offset+16+56*k;
    uint16_t id;uint32_t lo=0,hi=c->count;memcpy(&id,row+2,2);
    while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(c->records[mid].pad<id)lo=mid+1;else hi=mid;}
    c->tables[lo]=c->lighting+cell->offset+rd32(row+52);
   }
  }
 }
 pkg->instances=c->records;pkg->instance_count=c->count;
 return error;
}
