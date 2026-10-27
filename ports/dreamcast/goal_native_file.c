/* Bounded DCSH v1 decoder feeding the native transactional linker (AI-assisted), ISC. */
#include "goal_native_file.h"
#include <stdlib.h>
#include <string.h>
static uint32_t word(const unsigned char *p) {
  return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t crc32(const unsigned char *p,uint32_t bytes) {
  uint32_t crc=UINT32_MAX,i; unsigned bit;
  for (i=0;i<bytes;++i) {
    crc^=p[i];
    for (bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1u)?UINT32_C(0xedb88320):0);
  }
  return crc^UINT32_MAX;
}
static const char *name(const unsigned char *strings,uint32_t bytes,uint32_t offset) {
  if (offset>=bytes || !strings[offset] || (offset && strings[offset-1])) return NULL;
  return memchr(strings+offset,0,bytes-offset)?(const char*)strings+offset:NULL;
}
int dc_goal_load_native_module(dc_goal_runtime *rt,const void *source,uint32_t bytes,
                                dc_goal_cache_sync sync,void *context,dc_native_file_result *result) {
  const unsigned char *input=source,*rows,*relocations,*strings,*payload;
  uint32_t count,data_count,reloc_count,string_bytes,payload_bytes,init_offset,init_kind;
  uint32_t expected,metadata_bytes,i,next=0,init_found=0;
  unsigned char *storage=NULL;
  dc_goal_code_export *functions;
  dc_goal_data_export *data;
  dc_goal_code_relocation *decoded;
  const char *initializer=NULL;
  dc_goal_module_result linked;
  if (result) memset(result,0,sizeof(*result));
  if (!rt || !source || !result) { if (rt) rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  rt->error=DC_GOAL_BAD_MODULE;
  if (bytes<64 || bytes>8u*1024u*1024u || memcmp(input,"DCSH",4) || word(input+4)!=1 ||
      word(input+8)!=1 || word(input+12)!=bytes || word(input+48) || word(input+52) || word(input+56) || word(input+60)) return 0;
  count=word(input+16); data_count=word(input+20); reloc_count=word(input+24);
  string_bytes=word(input+28); payload_bytes=word(input+32);
  init_offset=word(input+36); init_kind=word(input+40);
  if (!count || count>4096 || data_count>8192 || reloc_count>262144 || !string_bytes || string_bytes>1048576 ||
      !payload_bytes || payload_bytes>4u*1048576u || init_kind>2 ||
      ((init_kind==0)!=(init_offset==UINT32_MAX))) return 0;
  expected=64u+(count+data_count)*24u+reloc_count*8u+string_bytes+payload_bytes;
  if (expected!=bytes || word(input+44)!=crc32(input+64,bytes-64)) return 0;
  rows=input+64; relocations=rows+(count+data_count)*24u;
  strings=relocations+reloc_count*8u; payload=strings+string_bytes;
  if (strings[string_bytes-1]) return 0;
  if (init_kind) {
    initializer=name(strings,string_bytes,init_offset);
    if (!initializer || strlen(initializer)>=sizeof(result->initializer)) return 0;
  }
  metadata_bytes=count*sizeof(*functions)+data_count*sizeof(*data)+reloc_count*sizeof(*decoded);
  storage=calloc(metadata_bytes,1);
  if (!storage) { rt->error=DC_GOAL_OUT_OF_MEMORY; return 0; }
  functions=(dc_goal_code_export*)storage;
  data=(dc_goal_data_export*)(storage+count*sizeof(*functions));
  decoded=(dc_goal_code_relocation*)((unsigned char*)data+data_count*sizeof(*data));
  for (i=0;i<reloc_count;++i) {
    decoded[i].offset=word(relocations+i*8u);
    decoded[i].symbol=name(strings,string_bytes,word(relocations+i*8u+4u));
    if (!decoded[i].symbol) goto invalid;
  }
  for (i=0;i<count+data_count;++i) {
    const unsigned char *r=rows+i*24u;
    const char *export_name=name(strings,string_bytes,word(r));
    uint32_t offset=word(r+4),length=word(r+8),first,n;
    int code=i<count;
    first=word(r+(code?12:16)); n=word(r+(code?16:20));
    if (!export_name || !length || (offset&3u) || offset>payload_bytes || length>payload_bytes-offset ||
        first!=next || n>4096 || first>reloc_count || n>reloc_count-first) goto invalid;
    next+=n;
    if (code) {
      if (length<2 || (length&1u) || word(r+20)) goto invalid;
      functions[i].name=export_name; functions[i].code=payload+offset; functions[i].bytes=length;
      functions[i].relocations=n?decoded+first:NULL; functions[i].relocation_count=n;
      if (initializer && !strcmp(export_name,initializer)) init_found=1;
    } else {
      uint32_t index=i-count,address_offset=word(r+12);
      if (address_offset>=length) goto invalid;
      data[index].name=export_name; data[index].data=payload+offset; data[index].bytes=length;
      data[index].address_offset=address_offset; data[index].relocations=n?decoded+first:NULL;
      data[index].relocation_count=n;
    }
  }
  if (next!=reloc_count || (init_kind && !init_found)) goto invalid;
  if (!dc_goal_load_module_data(rt,functions,count,data,data_count,sync,context,&linked)) {
    free(storage); return 0;
  }
  result->module=linked; result->decoder_bytes=metadata_bytes; result->initializer_kind=init_kind;
  if (initializer) memcpy(result->initializer,initializer,strlen(initializer)+1);
  free(storage); return 1;
invalid:
  free(storage); rt->error=DC_GOAL_BAD_MODULE; return 0;
}
int dc_goal_load_native_stream(dc_goal_runtime *rt,FILE *file,uint32_t bytes,
                              dc_goal_cache_sync sync,void *context,dc_native_file_result *result) {
  unsigned char *source; int loaded;
  if (result) memset(result,0,sizeof(*result));
  if (!rt || !file || !result) { if (rt) rt->error=DC_GOAL_BAD_ARGUMENT; return 0; }
  rt->error=DC_GOAL_BAD_MODULE;
  if (bytes<64 || bytes>8u*1024u*1024u) return 0;
  source=malloc(bytes);
  if (!source) { rt->error=DC_GOAL_OUT_OF_MEMORY; return 0; }
  if (fread(source,1,bytes,file)!=bytes) { free(source); return 0; }
  loaded=dc_goal_load_native_module(rt,source,bytes,sync,context,result);
  free(source); return loaded;
}
