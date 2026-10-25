/* Native reader oracle; private data only in explicit fixture (AI-assisted), ISC. */
#include <kos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "render_package.h"
KOS_INIT_FLAGS(INIT_DEFAULT);
typedef struct{uint32_t magic,version,state,total,passed,failed_case,system_ram,code_bytes;
 uint32_t observed_lo,observed_hi,expected_lo,expected_hi;char description[80];} telemetry;
volatile telemetry dc_backend={.magic=0x34485347u,.version=1};
extern const char *dc_render_test_check(const char *,const char *,uint32_t,int,uint32_t,uint32_t,uint32_t *);
extern int dc_render_test_begin(uint32_t,uint32_t);
extern void dc_render_test_stats(uint32_t *);
struct render_case {const char *manifest,*sector;int global,good;uint32_t af,rf;};
#include "build/render_fixture.h"
static void check(int ok,uint32_t line) {
 ++dc_backend.total;if(ok)++dc_backend.passed;else if(!dc_backend.failed_case)dc_backend.failed_case=line;
}
#define CHECK(ok) check(!!(ok),__LINE__)
#if DC_RENDER_HAS_REAL
static uint32_t rd32(const unsigned char *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
#endif
static void original(void) {
#if DC_RENDER_HAS_REAL
 FILE *file;unsigned char oracle[48],*seen=NULL;uint32_t *hashes=NULL;
 dc_render_catalog c={0};uint32_t i,unique=0,replicas=0,hashxor=0,hashsum=0,stats[4];const char *error;
 CHECK(dc_render_test_begin(0,0));file=fopen("/rd/oracle.bin","rb");CHECK(file!=NULL);if(!file)return;
 {int ok=fread(oracle,1,48,file)==48&&fgetc(file)==EOF&&!ferror(file);if(fclose(file))ok=0;CHECK(ok);if(!ok)return;}
 CHECK(rd32(oracle)==0x3244524fu&&rd32(oracle+4)==2u);
 file=fopen("/rd/real/manifest.drm","rb");CHECK(file!=NULL);if(!file)return;
 error=dc_render_catalog_load(file,&c);if(fclose(file)&&!error)error="close";CHECK(error==NULL);if(error)goto done;
 CHECK(c.source_count==rd32(oracle+8)&&c.sector_count==rd32(oracle+12)&&c.global_count==rd32(oracle+16));
 CHECK(c.material_count==rd32(oracle+20)&&c.texture_count==rd32(oracle+24)&&c.texture_bytes==rd32(oracle+28)&&c.package_bytes==rd32(oracle+32));
 CHECK((uint32_t)crc32(0L,c.storage,c.bytes)==rd32(oracle+36));
 seen=(unsigned char *)calloc((c.source_count+7u)/8u,1);hashes=(uint32_t *)malloc(c.source_count*sizeof(*hashes));
 CHECK(seen!=NULL&&hashes!=NULL);if(!seen||!hashes)goto done;
 for(i=0;i<=c.sector_count;++i) {
  int global=i==c.sector_count;dc_render_entry e;dc_render_block b={0};char path[96];uint32_t j;int consistent=1;
  if(global){dc_render_global_get(&c,&e);snprintf(path,sizeof(path),"/rd/real/global.drs");}
  else{dc_render_entry_get(&c,i,&e);snprintf(path,sizeof(path),"/rd/real/sectors/%ld_%ld.drs",(long)e.ix,(long)e.iz);}
  file=fopen(path,"rb");CHECK(file!=NULL);if(!file)goto done;
  error=dc_render_block_load(file,&c,&e,global,&b);if(fclose(file)&&!error)error="close";CHECK(error==NULL);
  if(error){dc_render_block_free(&b);goto done;}
  for(j=0;j<b.count;++j) {
   const unsigned char *raw=b.records+j*116u;uint32_t sid=rd32(raw),value=(uint32_t)crc32(0L,raw,116);
   if(sid>=c.source_count){consistent=0;break;}
   if(seen[sid/8u]&(1u<<(sid%8u))){if(hashes[sid]!=value)consistent=0;}
   else{seen[sid/8u]|=(unsigned char)(1u<<(sid%8u));hashes[sid]=value;++unique;hashxor^=value;hashsum+=value;}
  }
  CHECK(consistent);if(!global)replicas+=b.count;dc_render_block_free(&b);
 }
 CHECK(unique==c.source_count&&replicas==c.replica_count);
 CHECK(hashxor==rd32(oracle+40)&&hashsum==rd32(oracle+44));
 dc_backend.observed_lo=unique;dc_backend.observed_hi=c.sector_count;
done:
 free(seen);free(hashes);dc_render_catalog_free(&c);dc_render_test_stats(stats);
 CHECK(stats[2]==0);dc_backend.expected_lo=stats[3];
#endif
}
int main(void) {
 uint32_t i;dc_backend.state=1;dc_backend.system_ram=_arch_mem_top-0x8c000000u;
 for(i=0;i<sizeof(render_cases)/sizeof(render_cases[0]);++i) {
  const struct render_case *c=&render_cases[i];uint32_t words[16];
  const char *e=dc_render_test_check(c->manifest,c->sector,0,c->global,c->af,c->rf,words);
  if((e==NULL)!=c->good){if(!dc_backend.expected_hi)dc_backend.expected_hi=i+1u;
   printf("RENDER_CASE %u expected=%d got=%s\n",(unsigned)i,c->good,e?e:"accepted");}
  CHECK((e==NULL)==c->good);CHECK(words[12]==0);
 }
 original();snprintf((char *)dc_backend.description,80,"DRM2 CPU IO: sources=%u sectors=%u peakreader=%u",
    (unsigned)dc_backend.observed_lo,(unsigned)dc_backend.observed_hi,(unsigned)dc_backend.expected_lo);
 dc_backend.state=dc_backend.failed_case?3:2;for(;;)thd_sleep(100);return 0;
}
