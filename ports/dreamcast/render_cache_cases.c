/* Shared host/SH4 acceptance, payload and rollback oracle (AI-assisted), ISC. */
#include "render_cache_cases.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
extern void *dc_rc_new(const char *);
extern void dc_rc_close(void *);
extern void dc_rc_arm(uint32_t,uint32_t,uint32_t,uint32_t);
extern const char *dc_rc_load(void *,const char *,const double *,const char *,const char *);
extern void dc_rc_stats(void *,uint32_t *);
struct cache_case {const char *name;double q[4];uint32_t good,count,crc;};
struct cache_real {double q[4];uint32_t count,crc;};
#include "build/render_cache_fixture.h"
static void check(dc_render_cache_report *r,int ok,uint32_t line) {
 ++r->total;if(ok)++r->passed;else if(!r->failed_line)r->failed_line=line;
}
#define CHECK(ok) check(r,!!(ok),__LINE__)
static int paths(char *root,char *manifest,const char *base,const char *name) {
 int a=snprintf(root,512,"%s/%s",base,name);int b=snprintf(manifest,512,"%s/manifest.drm",root);
 return a>0&&a<512&&b>0&&b<512;
}
static void faults(const char *base,dc_render_cache_report *r) {
 char root[512],manifest[512];void *ctx;const char *error;uint32_t counts[12],before[12],s[12],kind,n;
 const double initial[4]={0,0,1,1},target[4]={0,0,2,2};
 if(!paths(root,manifest,base,"global-long-intersect")){CHECK(0);return;}
 ctx=dc_rc_new(manifest);CHECK(ctx!=NULL);if(!ctx)return;
 dc_rc_arm(0,0,0,0);error=dc_rc_load(ctx,root,initial,NULL,NULL);CHECK(error==NULL);
 dc_rc_arm(0,0,0,0);error=dc_rc_load(ctx,root,target,NULL,NULL);CHECK(error==NULL);dc_rc_stats(ctx,counts);
 dc_rc_close(ctx);dc_rc_stats(NULL,s);CHECK(s[4]==0&&s[5]==0);
 ctx=dc_rc_new(manifest);CHECK(ctx!=NULL);if(!ctx)return;
 dc_rc_arm(0,0,0,0);error=dc_rc_load(ctx,root,initial,NULL,NULL);CHECK(error==NULL);dc_rc_stats(ctx,before);
 for(kind=0;kind<4u;++kind)for(n=1;n<=counts[kind];++n) {
  uint32_t fail[4]={0,0,0,0};fail[kind]=n;dc_rc_arm(fail[0],fail[1],fail[2],fail[3]);
  error=dc_rc_load(ctx,root,target,NULL,NULL);dc_rc_stats(ctx,s);CHECK(error!=NULL);
  CHECK(error&&strcmp(error,"CACHE ROLLBACK MUTATED")&&s[8]==before[8]&&s[11]==before[11]&&s[9]==1u&&s[10]==1u);
  CHECK(s[4]==0u&&s[5]==before[5]&&s[7]==0u);
 }
 dc_rc_close(ctx);dc_rc_stats(NULL,s);CHECK(s[4]==0u&&s[5]==0u&&s[7]==0u);
}
void dc_render_cache_cases(const char *base,dc_render_cache_report *r) {
 uint32_t i,s[12];char root[512],manifest[512];memset(r,0,sizeof(*r));
 (void)cache_real_rows;
 for(i=0;i<sizeof(cache_cases)/sizeof(cache_cases[0]);++i) {
  const struct cache_case *c=&cache_cases[i];void *ctx;const char *error;
  if(!paths(root,manifest,base,c->name)){CHECK(0);return;}
  ctx=dc_rc_new(manifest);CHECK(ctx!=NULL);if(!ctx)return;
  dc_rc_arm(0,0,0,0);error=dc_rc_load(ctx,root,c->q,NULL,NULL);dc_rc_stats(ctx,s);
  CHECK((error==NULL)==c->good);
  CHECK(c->good?(s[8]==c->count&&s[11]==c->crc&&s[9]==1u&&s[10]==1u):(s[8]==0&&s[9]==0&&s[10]==0));
  CHECK(s[4]==0u&&s[7]==0u);if(s[6]>r->peak_bytes)r->peak_bytes=s[6];
  dc_rc_close(ctx);dc_rc_stats(NULL,s);CHECK(s[4]==0u&&s[5]==0u&&s[7]==0u);
 }
 faults(base,r);
#if CACHE_REAL_WINDOWS
 {
  void *ctx;const char *error;
  if(!paths(root,manifest,base,"real")){CHECK(0);return;}
  ctx=dc_rc_new(manifest);CHECK(ctx!=NULL);if(!ctx)return;
  for(i=0;i<CACHE_REAL_WINDOWS;++i) {
   const struct cache_real *row=&cache_real_rows[i];dc_rc_arm(0,0,0,0);
   error=dc_rc_load(ctx,root,row->q,NULL,NULL);dc_rc_stats(ctx,s);CHECK(error==NULL);
   CHECK(s[8]==row->count);CHECK(s[11]==row->crc);CHECK(s[4]==0u&&s[7]==0u);
   if(s[8]>r->max_faces)r->max_faces=s[8];
   if(s[6]>r->peak_bytes)r->peak_bytes=s[6];
   ++r->windows;
  }
  dc_rc_close(ctx);dc_rc_stats(NULL,s);CHECK(s[4]==0u&&s[5]==0u&&s[7]==0u);
 }
#endif
}
