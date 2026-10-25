/* Independent allocation-failure/geometry bridge (AI-assisted), ISC. */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
static unsigned calls, fail_at, live, peak;
static size_t bytes_live, bytes_peak;
static unsigned read_calls, read_fail_at, open_files, close_fail, io_error;
FILE *dc_test_fopen(const char *path,const char *mode){FILE *f=fopen(path,mode);if(f)++open_files;return f;}
int dc_test_fclose(FILE *fp){int n=fclose(fp);--open_files;io_error=0;return close_fail?EOF:n;}
size_t dc_test_fread(void *p,size_t size,size_t n,FILE *f){
 ++read_calls;if(read_fail_at && read_calls==read_fail_at){io_error=1;return 0;}return fread(p,size,n,f);
}
int dc_test_fgetc(FILE *fp){return fgetc(fp);}
int dc_test_ferror(FILE *fp){return io_error || ferror(fp);}
void dc_test_io_reset(unsigned n,unsigned close_error){read_calls=0;read_fail_at=n;close_fail=close_error;io_error=0;}
unsigned dc_test_open_files(void){return open_files;}
typedef union { long double align; struct {size_t bytes; uint32_t magic;} h;} allocation;
void *dc_test_malloc(size_t bytes) {
 allocation *p; ++calls;if(fail_at && calls==fail_at)return NULL;
 p=malloc(sizeof(*p)+bytes);if(!p)return NULL;p->h.bytes=bytes;p->h.magic=0x53454354;
 ++live;bytes_live+=bytes;if(live>peak)peak=live;if(bytes_live>bytes_peak)bytes_peak=bytes_live;return p+1;
}
void dc_test_free(void *ptr) {
 allocation *p;if(!ptr)return;p=(allocation *)ptr-1;
 if(p->h.magic!=0x53454354)abort();
 p->h.magic=0;--live;bytes_live-=p->h.bytes;free(p);
}
#define malloc dc_test_malloc
#define free dc_test_free
#include "../world_collision.c"
#undef malloc
#undef free
#include "../sector_cache.h"
void dc_test_reset(unsigned n){calls=0;fail_at=n;peak=live;bytes_peak=bytes_live;}
unsigned dc_test_calls(void){return calls;}
unsigned dc_test_live(void){return live;}
size_t dc_test_bytes(void){return bytes_live;}
size_t dc_test_peak(void){return bytes_peak;}
const char *dc_test_index(const char *path,dc_sector_index *out){
 const char *e;FILE *fp=fopen(path,"rb");if(!fp)return "test manifest missing";
 e=dc_sector_index_load(fp,out);fclose(fp);return e;
}
int dc_test_row(dc_sector_cache *c,uint32_t i,dc_world_triangle *out){
 if(i>=c->world.count)return 0;
 *out=c->world.triangles[i].t;return 1;
}
uint32_t dc_test_cache_crc(const dc_sector_cache *c){
 uint32_t crc=0xffffffffu,i;unsigned j,k;
 for(i=0;i<c->world.count;++i){const unsigned char *p=(const unsigned char *)&c->world.triangles[i].t;
  for(j=0;j<40;++j){crc^=p[j];for(k=0;k<8;++k)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}}
 return crc^0xffffffffu;
}
