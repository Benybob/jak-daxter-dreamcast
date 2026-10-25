/* Strict framing, bounds, allocation and canary checks (AI-assisted), ISC. */
#include "sector_inflate.h"
#include "sector_inflate_cases.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "build/inflate_fixture.h"
void dc_inflate_test_reset(unsigned);
unsigned dc_inflate_test_calls(void);
unsigned dc_inflate_test_live(void);
size_t dc_inflate_test_peak(void);
int dc_inflate_test_pool_limits(void);
static unsigned char buffer[DC_INFLATE_RAW_MAX+32];
#define CHECK(x) do{++r->total;if(x)++r->passed;else {if(!r->failed_line)r->failed_line=__LINE__;printf("inflate failed line %u\n",(unsigned)__LINE__);}}while(0)
static int canaries(unsigned bytes){unsigned i;for(i=0;i<16;++i)if(buffer[i]!=0xa5 || buffer[16+bytes+i]!=0xa5)return 0;return 1;}
static int expected(const unsigned char *p,unsigned bytes,unsigned kind){
 unsigned i;uint32_t state=0x13579bdfu;
 for(i=0;i<bytes;++i){unsigned char want;
  if(kind==0)want=(unsigned char)('1'+i%9);else if(kind==1)want=(unsigned char)i;
  else if(kind==2)want=(unsigned char)(((i*37)^(i>>3))&255);else if(kind==4)want=0;
  else{state=state*1664525u+1013904223u;want=(unsigned char)(state>>24);}
  if(p[i]!=want)return 0;
 }return 1;
}
void dc_sector_inflate_cases(dc_inflate_report *r){
 unsigned i,n,allocations;const char *error;unsigned char altered[sizeof(inflate_fixture_0)*2+1];
 const dc_inflate_fixture *small=&inflate_fixtures[0];memset(r,0,sizeof(*r));
 for(i=0;i<sizeof(inflate_fixtures)/sizeof(inflate_fixtures[0]);++i){const dc_inflate_fixture *f=&inflate_fixtures[i];
  dc_inflate_test_reset(0);memset(buffer,0xa5,sizeof(buffer));
  error=dc_sector_inflate(f->data,f->bytes,buffer+16,f->output,f->crc);
  CHECK(!error);CHECK(expected(buffer+16,f->output,f->kind));CHECK(canaries(f->output));CHECK(!dc_inflate_test_live());
  CHECK(dc_inflate_test_peak()<=DC_INFLATE_WORKSPACE_MAX);
  if(dc_inflate_test_peak()>r->peak_decode_bytes)r->peak_decode_bytes=(uint32_t)dc_inflate_test_peak();
  allocations=dc_inflate_test_calls();
  for(n=1;n<=allocations;++n){dc_inflate_test_reset(n);memset(buffer,0xa5,sizeof(buffer));
   CHECK(dc_sector_inflate(f->data,f->bytes,buffer+16,f->output,f->crc)!=NULL);CHECK(!dc_inflate_test_live());CHECK(canaries(f->output));}
 }
 dc_inflate_test_reset(0);
 for(i=0;i<small->bytes;++i){memset(buffer,0xa5,sizeof(buffer));
  CHECK(dc_sector_inflate(small->data,i,buffer+16,small->output,small->crc)!=NULL);CHECK(canaries(small->output));CHECK(!dc_inflate_test_live());}
 memset(buffer,0xa5,sizeof(buffer));
 CHECK(dc_sector_inflate(small->data,small->bytes,buffer+16,small->output+1,small->crc)!=NULL);CHECK(canaries(small->output+1));
 CHECK(dc_sector_inflate(small->data,small->bytes,buffer+16,small->output,small->crc^1)!=NULL);
 memcpy(altered,small->data,small->bytes);altered[small->bytes]=99;
 CHECK(dc_sector_inflate(altered,small->bytes+1,buffer+16,small->output,small->crc)!=NULL);
 memcpy(altered+small->bytes,small->data,small->bytes);
 CHECK(dc_sector_inflate(altered,small->bytes*2,buffer+16,small->output,small->crc)!=NULL);
 memcpy(altered,small->data,small->bytes);altered[small->bytes-1]^=1;
 CHECK(dc_sector_inflate(altered,small->bytes,buffer+16,small->output,small->crc)!=NULL);
 CHECK(dc_sector_inflate(inflate_gzip,sizeof(inflate_gzip),buffer+16,small->output,small->crc)!=NULL);
 CHECK(dc_sector_inflate(inflate_raw,sizeof(inflate_raw),buffer+16,small->output,small->crc)!=NULL);
 CHECK(dc_sector_inflate(inflate_dictionary,sizeof(inflate_dictionary),buffer+16,small->output,small->crc)!=NULL);
 memset(buffer,0xa5,sizeof(buffer));
 CHECK(dc_sector_inflate(inflate_bomb,sizeof(inflate_bomb),buffer+16,128,0)!=NULL);CHECK(canaries(128));
 CHECK(dc_sector_inflate(small->data,small->bytes,NULL,1,0)!=NULL);
 CHECK(dc_sector_inflate(NULL,1,buffer+16,1,0)!=NULL);
 CHECK(dc_sector_inflate(small->data,DC_INFLATE_INPUT_MAX+1,buffer+16,1,0)!=NULL);
 CHECK(dc_sector_inflate(small->data,small->bytes,buffer+16,DC_INFLATE_RAW_MAX+1,0)!=NULL);
 CHECK(dc_sector_inflate(small->data,small->bytes,buffer+16,0,0)!=NULL);
 CHECK(dc_sector_inflate(small->data,small->bytes,(void *)small->data,small->output,small->crc)!=NULL);
 memcpy(buffer+16,small->data,small->bytes);
 CHECK(dc_sector_inflate(buffer+16,small->bytes,buffer+17,small->output,small->crc)!=NULL);
 CHECK(!dc_inflate_test_live());dc_inflate_test_reset(0);
 CHECK(dc_sector_inflate_empty(inflate_empty,sizeof(inflate_empty))==NULL);CHECK(!dc_inflate_test_live());
 allocations=dc_inflate_test_calls();
 for(n=1;n<=allocations;++n){dc_inflate_test_reset(n);CHECK(dc_sector_inflate_empty(inflate_empty,sizeof(inflate_empty))!=NULL);CHECK(!dc_inflate_test_live());}
 dc_inflate_test_reset(0);CHECK(dc_sector_inflate_empty(inflate_empty_stored,sizeof(inflate_empty_stored))==NULL);
 for(i=0;i<sizeof(inflate_empty);++i)CHECK(dc_sector_inflate_empty(inflate_empty,i)!=NULL);
 CHECK(dc_sector_inflate_empty(small->data,small->bytes)!=NULL);
 CHECK(dc_sector_inflate_empty(NULL,1)!=NULL);CHECK(dc_sector_inflate_empty(inflate_empty,DC_INFLATE_INPUT_MAX+1)!=NULL);
 memcpy(altered,inflate_empty,sizeof(inflate_empty));altered[sizeof(inflate_empty)]=99;
 CHECK(dc_sector_inflate_empty(altered,sizeof(inflate_empty)+1)!=NULL);
 memcpy(altered+sizeof(inflate_empty),inflate_empty,sizeof(inflate_empty));CHECK(dc_sector_inflate_empty(altered,2*sizeof(inflate_empty))!=NULL);
 altered[sizeof(inflate_empty)-1]^=1;CHECK(dc_sector_inflate_empty(altered,sizeof(inflate_empty))!=NULL);
 CHECK(!dc_inflate_test_live());dc_inflate_test_reset(0);CHECK(dc_inflate_test_pool_limits());CHECK(!dc_inflate_test_live());
 CHECK(dc_inflate_test_peak()==DC_INFLATE_WORKSPACE_MAX);dc_inflate_test_reset(0);
}
