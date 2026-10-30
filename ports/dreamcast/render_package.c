/* Exact bounded storage reader for DRM2/DRS2 (AI-assisted), ISC. */
#include "render_package.h"
#include "sector_inflate.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* Newlib may inline ferror, hiding a command-line function-name substitution.
 * Select explicit test IO names after stdio headers, so injected EOF errors
 * exercise the same production checks on SH-4 as they do on the host. */
#ifdef DC_RENDER_TEST_IO
extern size_t dc_render_test_fread(void *,size_t,size_t,FILE *);
extern int dc_render_test_fgetc(FILE *);
extern int dc_render_test_ferror(FILE *);
#define RENDER_READ dc_render_test_fread
#define RENDER_GETC dc_render_test_fgetc
#define RENDER_ERROR dc_render_test_ferror
#else
#define RENDER_READ fread
#define RENDER_GETC fgetc
#define RENDER_ERROR ferror
#endif

typedef char render_float_width[(sizeof(float)==4 && sizeof(uint32_t)==4 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128)?1:-1];
enum { HEADER=256, SOURCE_MAX=1000000, SECTORS_MAX=8192, REPLICAS_MAX=2000000,
       MATERIAL_MAX=256,TEXTURE_MAX=256,TEXTURE_BYTE_MAX=2097152,PACKAGE_MAX=8388608,
       RAW_MAX=475136,COMPRESSED_MAX=476160,WRAP_FLAGS=188,BASE_FLAGS=511 };
static uint32_t u32(const unsigned char *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t u16(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8; }
static int32_t i32(const unsigned char *p) { uint32_t v=u32(p);int32_t r;memcpy(&r,&v,4);return r; }
static float f32(const unsigned char *p) { uint32_t v=u32(p);float r;memcpy(&r,&v,4);return r; }
static uint32_t crc(const unsigned char *p,uint32_t n) { return (uint32_t)crc32(0L,p,n); }
static int bounded(float x,float limit) { return isfinite(x)&&fabsf(x)<=limit; }
static int unit(float x) { return isfinite(x)&&x>=0.f&&x<=1.f; }
static int flags_ok(uint32_t x,uint32_t allowed) {
 return !(x&~allowed)&&!((x&4u)&&(x&16u))&&!((x&8u)&&(x&32u));
}
static int render_eof(FILE *f) { int c=RENDER_GETC(f);return c==EOF&&!RENDER_ERROR(f); }
static int read_exact(FILE *f,void *p,uint32_t n) { return RENDER_READ(p,1,n,f)==n&&!RENDER_ERROR(f); }
static int same_entry(const dc_render_entry *a,const dc_render_entry *b) {
 return a->ix==b->ix&&a->iz==b->iz&&a->count==b->count&&a->raw_bytes==b->raw_bytes
     &&a->file_bytes==b->file_bytes&&a->payload_crc==b->payload_crc&&a->compressed_crc==b->compressed_crc;
}
static void entry_decode(const unsigned char *p,dc_render_entry *e) {
 e->ix=i32(p);e->iz=i32(p+4);e->count=u32(p+8);e->raw_bytes=u32(p+12);
 e->file_bytes=u32(p+16);e->payload_crc=u32(p+20);e->compressed_crc=u32(p+24);
}
int dc_render_entry_get(const dc_render_catalog *c,uint32_t n,dc_render_entry *out) {
 if(!c||!c->storage||!out||n>=c->sector_count)return 0;
 entry_decode(c->storage+c->entry_offset+n*32u,out);return 1;
}
int dc_render_global_get(const dc_render_catalog *c,dc_render_entry *out) {
 const unsigned char *p;
 if(!c||!c->storage||!out)return 0;
 p=c->storage;out->ix=out->iz=0;out->count=c->global_count;out->raw_bytes=u32(p+148);
 out->file_bytes=u32(p+152);out->payload_crc=u32(p+156);out->compressed_crc=u32(p+160);return 1;
}
int dc_render_texture_get(const dc_render_catalog *c,uint32_t n,dc_render_texture *out) {
 const unsigned char *p;
 if(!c||!c->storage||!out||n>=c->texture_count)return 0;
 p=c->storage+HEADER+n*16u;out->pixels=c->storage+c->pixel_offset+u32(p);
 out->bytes=u32(p+4);out->width=u16(p+8);out->height=u16(p+10);out->format=u32(p+12);return 1;
}
int dc_render_material_get(const dc_render_catalog *c,uint32_t n,dc_render_material *out) {
 const unsigned char *p;uint32_t i;
 if(!c||!c->storage||!out||n>=c->material_count)return 0;
 p=c->storage+c->material_offset+n*64u;out->base_texture=u32(p);out->base_flags=u32(p+4);
 out->cutoff=f32(p+8);out->alpha_mode=u32(p+12);out->strength_texture=u32(p+16);out->strength_flags=u32(p+20);
 out->metallic=f32(p+24);out->roughness=f32(p+28);out->spec_texture=u32(p+32);out->spec_flags=u32(p+36);
 out->specular=f32(p+40);for(i=0;i<3u;++i)out->spec_color[i]=f32(p+44+i*4u);return 1;
}
static int texture_role(const dc_render_catalog *c,uint32_t slot,uint32_t format) {
 dc_render_texture t;return dc_render_texture_get(c,slot,&t)&&t.format==format;
}
static const char *header_check(const unsigned char *p,dc_render_catalog *c) {
 static const uint32_t limits[][2]={{56,4096},{60,4096},{64,8192},{68,36},{72,SOURCE_MAX},
 {76,SECTORS_MAX},{80,REPLICAS_MAX},{84,MATERIAL_MAX},{88,TEXTURE_MAX},{92,TEXTURE_BYTE_MAX},
 {96,PACKAGE_MAX},{100,116},{104,36},{208,64},{212,1}};
 uint32_t i,blob,tables,local;
 if(memcmp(p,"DRM2",4)||u32(p+4)!=2u||u32(p+8)!=HEADER||u32(p+12)!=1u||f32(p+52)!=64.f)return "render manifest contract";
 for(i=0;i<sizeof(limits)/sizeof(limits[0]);++i)if(u32(p+limits[i][0])!=limits[i][1])return "render manifest limits";
 for(i=216;i<HEADER;i+=4)if(u32(p+i))return "render manifest reserved";
 c->source_count=u32(p+16);c->sector_count=u32(p+20);c->replica_count=u32(p+24);c->global_count=u32(p+28);
 c->material_count=u32(p+32);c->texture_count=u32(p+36);c->texture_bytes=u32(p+40);c->package_bytes=u32(p+44);c->cell_size=f32(p+48);
 if(!c->source_count||c->source_count>SOURCE_MAX||c->sector_count>SECTORS_MAX||c->replica_count>REPLICAS_MAX
    ||c->global_count>4096u||c->global_count>c->source_count||!c->material_count||c->material_count>MATERIAL_MAX
    ||!c->texture_count||c->texture_count>TEXTURE_MAX||c->texture_bytes>TEXTURE_BYTE_MAX||c->package_bytes>PACKAGE_MAX
    ||!isfinite(c->cell_size)||c->cell_size<4.f||c->cell_size>64.f)return "render manifest counts";
 local=c->source_count-c->global_count;
 if(c->replica_count<local||c->replica_count>local*36u||c->sector_count>c->replica_count)return "render manifest coverage counts";
 for(i=124;i<148;i+=4)if(!bounded(f32(p+i),1000000.f))return "render manifest bounds";
 for(i=0;i<3u;++i)if(f32(p+124+i*4u)>f32(p+136+i*4u))return "render manifest bounds";
 if(u32(p+148)!=c->global_count*116u||u32(p+152)<48u||u32(p+152)-40u>u32(p+148)+1024u)return "render global counts";
 blob=u32(p+196);
 if(blob<c->texture_bytes||blob-c->texture_bytes>3u)return "render pixel bytes";
 tables=c->texture_count*16u+blob+c->material_count*64u+c->sector_count*32u;
 if(u32(p+200)!=tables)return "render table bytes";
 c->bytes=HEADER+tables+4u;c->pixel_offset=HEADER+c->texture_count*16u;
 c->material_offset=c->pixel_offset+blob;c->entry_offset=c->material_offset+c->material_count*64u;
 return NULL;
}
static const char *tables_check(const dc_render_catalog *c) {
 uint32_t i,running=0,sum=0,max=0,package=c->bytes+u32(c->storage+152);
 int32_t xmin=0,xmax=0,zmin=0,zmax=0,prevx=0,prevz=0;
 const unsigned char *p=c->storage;
 for(i=0;i<c->texture_count;++i) {
  dc_render_texture t;const unsigned char *d=p+HEADER+i*16u;uint32_t size;
  t.bytes=u32(d+4);t.width=u16(d+8);t.height=u16(d+10);t.format=u32(d+12);
  if((t.format<1u||t.format>3u)||t.width<8u||t.width>64u||t.height<8u||t.height>64u
    ||(t.width&(t.width-1u))||(t.height&(t.height-1u)))return "render texture dimensions";
  size=t.width*t.height*(t.format==2u?1u:2u);
  if(u32(d)!=running||t.bytes!=size||size>c->texture_bytes-running)return "render texture bounds";
  running+=size;
 }
 if(running!=c->texture_bytes)return "render pixel coverage";
 for(i=running;i<u32(p+196);++i)if(p[c->pixel_offset+i])return "render pixel padding";
 for(i=0;i<c->material_count;++i) {
  dc_render_material m;const unsigned char *raw=p+c->material_offset+i*64u;
  dc_render_material_get(c,i,&m);
  if(!flags_ok(m.base_flags,BASE_FLAGS)||m.alpha_mode>2u||m.alpha_mode!=((m.base_flags&256u)?2u:(m.base_flags&1u))||((m.base_flags&256u)&&(m.base_flags&1u))||!unit(m.cutoff)
    ||!texture_role(c,m.base_texture,(m.base_flags&256u)?3u:1u)||!unit(m.metallic)||!unit(m.roughness)||!unit(m.specular)
    ||!unit(m.spec_color[0])||!unit(m.spec_color[1])||!unit(m.spec_color[2])||u32(raw+56)||u32(raw+60))return "render material";
  if(m.strength_texture==DC_RENDER_ABSENT_TEXTURE) { if(m.strength_flags)return "render absent strength"; }
  else if(!texture_role(c,m.strength_texture,2u)||!flags_ok(m.strength_flags,WRAP_FLAGS))return "render strength";
  if(m.spec_texture==DC_RENDER_ABSENT_TEXTURE) { if(m.spec_flags)return "render absent specular"; }
  else if(!texture_role(c,m.spec_texture,1u)||!flags_ok(m.spec_flags,WRAP_FLAGS))return "render specular";
  if((m.base_flags&2u)&&(m.strength_texture!=DC_RENDER_ABSENT_TEXTURE||m.spec_texture!=DC_RENDER_ABSENT_TEXTURE
    ||m.specular!=1.f||m.spec_color[0]!=1.f||m.spec_color[1]!=1.f||m.spec_color[2]!=1.f))return "render unlit";
 }
 for(i=0;i<c->sector_count;++i) {
  dc_render_entry e;dc_render_entry_get(c,i,&e);
  if((i&&(e.ix<prevx||(e.ix==prevx&&e.iz<=prevz)))||!e.count||e.count>4096u||e.count>c->source_count-c->global_count||e.raw_bytes!=e.count*116u
    ||e.file_bytes<48u||e.file_bytes-40u>e.raw_bytes+1024u||u32(p+c->entry_offset+i*32u+28))return "render entry";
  if(i==0){xmin=xmax=e.ix;zmin=zmax=e.iz;}
  if(e.ix<xmin)xmin=e.ix;
  if(e.ix>xmax)xmax=e.ix;
  if(e.iz<zmin)zmin=e.iz;
  if(e.iz>zmax)zmax=e.iz;
  prevx=e.ix;prevz=e.iz;sum+=e.count;package+=e.file_bytes;if(e.count>max)max=e.count;
  if(package>PACKAGE_MAX)return "render package bytes";
 }
 if(sum!=c->replica_count||max!=u32(p+204)||package!=c->package_bytes||xmin!=i32(p+108)||xmax!=i32(p+112)
    ||zmin!=i32(p+116)||zmax!=i32(p+120))return "render index totals";
 return NULL;
}
const char *dc_render_catalog_load(FILE *file,dc_render_catalog *out) {
 unsigned char header[HEADER];dc_render_catalog stage;const char *error;
 if(!out)return "render output";
 if(out->storage)return "render output owned";
 memset(out,0,sizeof(*out));memset(&stage,0,sizeof(stage));
 if(!file||!read_exact(file,header,HEADER))return "render short header";
 error=header_check(header,&stage);if(error)return error;
 stage.storage=(unsigned char *)malloc(stage.bytes);if(!stage.storage)return "render allocation";
 memcpy(stage.storage,header,HEADER);
 if(!read_exact(file,stage.storage+HEADER,stage.bytes-HEADER)||!render_eof(file)){error="render manifest EOF";goto fail;}
 stage.crc=u32(stage.storage+stage.bytes-4u);
 if(crc(stage.storage,stage.bytes-4u)!=stage.crc){error="render manifest CRC";goto fail;}
 error=tables_check(&stage);if(error)goto fail;
 *out=stage;return NULL;
fail:free(stage.storage);return error;
}
void dc_render_catalog_free(dc_render_catalog *c) { if(c){free(c->storage);memset(c,0,sizeof(*c));} }
void dc_render_block_free(dc_render_block *b) { if(b){free(b->records);memset(b,0,sizeof(*b));} }
static int member(const dc_render_catalog *c,const dc_render_entry *e,int global) {
 dc_render_entry item;uint32_t i;
 if(global){dc_render_global_get(c,&item);return same_entry(&item,e);}
 for(i=0;i<c->sector_count;++i){dc_render_entry_get(c,i,&item);if(same_entry(&item,e))return 1;}
 return 0;
}
static const char *records_check(const dc_render_catalog *c,const unsigned char *p,uint32_t count) {
 uint32_t i,j,k,previous=0;
 for(i=0;i<count;++i,p+=116u) {
  uint32_t id=u32(p);
  if(id>=c->source_count||(i&&id<=previous)||u32(p+4)>=c->material_count)return "render face id/order";
  previous=id;
  for(j=0;j<3u;++j) {
   const unsigned char *v=p+8+j*36u;float nx=f32(v+20),ny=f32(v+24),nz=f32(v+28),length;
   for(k=0;k<3u;++k)if(!bounded(f32(v+k*4u),1000000.f))return "render position";
   if(!bounded(f32(v+12),65536.f)||!bounded(f32(v+16),65536.f))return "render UV";
   if(!bounded(nx,1.00001f)||!bounded(ny,1.00001f)||!bounded(nz,1.00001f))return "render normal";
   length=sqrtf(nx*nx+ny*ny+nz*nz);
   if(((u32(v+20)|u32(v+24)|u32(v+28))&0x7fffffffu)&&fabsf(length-1.f)>1e-5f)return "render normal";
  }
 }
 return NULL;
}
const char *dc_render_block_load(FILE *file,const dc_render_catalog *c,const dc_render_entry *e,int global,dc_render_block *out) {
 unsigned char header[32],footer[8],*compressed=NULL,*raw=NULL;uint32_t n;const char *error;
 if(!out)return "render block output";
 if(out->records)return "render block owned";
 memset(out,0,sizeof(*out));
 if(!file||!c||!c->storage||!e||(global!=0&&global!=1)||!member(c,e,global))return "render block arguments";
 if(!read_exact(file,header,32))return "render short sector";
 n=u32(header+28);
 if(memcmp(header,"DRS2",4)||u32(header+4)!=2u||u32(header+8)!=(uint32_t)global||i32(header+12)!=e->ix
   ||i32(header+16)!=e->iz||u32(header+20)!=e->count||u32(header+24)!=e->raw_bytes||n>COMPRESSED_MAX
   ||n+40u!=e->file_bytes||e->raw_bytes>RAW_MAX||(!e->raw_bytes&&e->payload_crc))return "render sector header";
 compressed=(unsigned char *)malloc(n);if(!compressed)return "render allocation";
 if(!read_exact(file,compressed,n)||!read_exact(file,footer,8)||!render_eof(file)){error="render sector EOF";goto fail;}
 if(u32(footer)!=e->payload_crc||u32(footer+4)!=e->compressed_crc||crc(compressed,n)!=e->compressed_crc){error="render sector CRC";goto fail;}
 if(e->raw_bytes) {
  raw=(unsigned char *)malloc(e->raw_bytes);if(!raw){error="render allocation";goto fail;}
  error=dc_sector_inflate(compressed,n,raw,e->raw_bytes,e->payload_crc);
 } else error=dc_sector_inflate_empty(compressed,n);
 if(error)goto fail;
 error=records_check(c,raw,e->count);if(error)goto fail;
 free(compressed);out->records=raw;out->count=e->count;return NULL;
fail:free(compressed);free(raw);return error;
}
