/* Exact rebased payload + transactional cache oracle (AI-assisted), ISC. */
#include "sector_cache_cases.h"
#include "tests/sector_cache_bridge.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do{++out->total;if(x)++out->passed;else if(!out->failed_case)out->failed_case=(uint32_t)__LINE__;}while(0)
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float f32(const unsigned char *p){uint32_t bits=u32(p);float f;memcpy(&f,&bits,4);return f;}
void dc_sector_cache_oracle(const char *root,dc_sector_oracle_result *out){
 dc_sector_index index={0};dc_sector_cache cache={0},prior;dc_sector_bounds query,swept;
 dc_vec3 origin;FILE *fp=NULL,*oracle=NULL;unsigned char h[20],r[36];char path[512];
 uint32_t count=0,i,n;size_t live;const char *error;
 memset(out,0,sizeof(*out));dc_test_reset(0);dc_test_io_reset(0,0);
 snprintf(path,sizeof(path),"%s/manifest.dcs",root);fp=fopen(path,"rb");CHECK(fp!=NULL);if(!fp)goto done;
 error=dc_sector_index_load(fp,&index);fclose(fp);fp=NULL;CHECK(error==NULL);if(error)goto done;
 snprintf(path,sizeof(path),"%s/oracle.bin",root);oracle=fopen(path,"rb");CHECK(oracle!=NULL);if(!oracle)goto done;
 CHECK(fread(h,1,20,oracle)==20 && !memcmp(h,"SCO1",4) && u32(h+4)==1);if(out->failed_case)goto done;
 count=u32(h+8);CHECK(count>0 && count<=4096 && u32(h+12)==index.source_count && u32(h+16)==index.package_crc);if(out->failed_case)goto done;
 for(i=0;i<count;++i){
  CHECK(fread(r,1,36,oracle)==36);if(out->failed_case)break;
  query=(dc_sector_bounds){f32(r),f32(r+4),f32(r+8),f32(r+12)};
  origin=(dc_vec3){f32(r+16),f32(r+20),f32(r+24)};
  cache.package_crc=index.package_crc^1u;dc_test_reset(0);dc_test_io_reset(0,0);
  error=dc_sector_retarget(&index,root,query,origin,&cache);CHECK(error==NULL);if(error){printf("sector oracle %u: %s\n",(unsigned)i,error);break;}
  CHECK(cache.world.count==u32(r+28));CHECK(dc_test_cache_crc(&cache)==u32(r+32));CHECK(dc_sector_covers(&cache,query));
  {dc_vec3 a={(query.min_x+query.max_x)*.5f,origin.y,(query.min_z+query.max_z)*.5f},b=a;
   b.x+=.125f;b.z+=.125f;CHECK(dc_sector_swept_bounds(a,b,.28f,&swept));CHECK(dc_sector_covers(&cache,swept));}
  if(cache.world.count>out->max_faces)out->max_faces=cache.world.count;
  if(dc_test_peak()>out->peak_geometry_bytes)out->peak_geometry_bytes=(uint32_t)dc_test_peak();
  prior=cache;dc_test_reset(1);dc_test_io_reset(1,0);origin.x+=1;
  error=dc_sector_retarget(&index,"/missing-sector-root",query,origin,&cache);
  CHECK(!error && !memcmp(&cache,&prior,sizeof(cache)) && dc_test_calls()==0);
  query.min_x=NAN;error=dc_sector_retarget(&index,root,query,origin,&cache);
  CHECK(error && !memcmp(&cache,&prior,sizeof(cache)));query.min_x=f32(r);
  if(i==0){
   for(n=1;n<=8;++n){
    cache.package_crc=index.package_crc^1u;prior=cache;live=dc_test_bytes();
    dc_test_reset(n<=4?n:0);dc_test_io_reset(n>=5&&n<=7?n-4:0,n==8);
    error=dc_sector_retarget(&index,root,query,origin,&cache);
    CHECK(error && !memcmp(&cache,&prior,sizeof(cache)) && dc_test_bytes()==live && dc_test_open_files()==0);
   }
  }
  ++out->windows;
 }
 CHECK(fgetc(oracle)==EOF && !ferror(oracle));
done:
 if(fp)fclose(fp);
 if(oracle)fclose(oracle);
 dc_test_reset(0);dc_test_io_reset(0,0);dc_sector_cache_free(&cache);dc_sector_index_free(&index);
 CHECK(dc_test_live()==0 && dc_test_open_files()==0);CHECK(out->peak_geometry_bytes<2u*1024u*1024u);
}
