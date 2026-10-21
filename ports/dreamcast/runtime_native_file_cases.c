/* Execute code/private data loaded from exact DCSH file bytes (AI-assisted), ISC. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "goal_native_file.h"
#include "runtime_cases.h"
#include "build/native_file_blob.h"
typedef uint64_t (*zero)(void);
typedef uint64_t (*one)(uint64_t);
static uint32_t entry(dc_goal_runtime *rt,const char *name) {
  return dc_goal_symbol_value(rt,dc_goal_find(rt,name));
}
static uint32_t word(dc_goal_runtime *rt,uint32_t at) {
  uint32_t value=0; void *p=dc_goal_resolve(rt,at,4); if (p) memcpy(&value,p,4); return value;
}
uint32_t dc_runtime_native_file_cases(dc_goal_runtime *rt,dc_runtime_check check,void *context,
                                      unsigned *id,dc_goal_cache_sync sync) {
  dc_native_file_result result;
  unsigned char *transport=malloc(sizeof(native_fixture_file));
  uint32_t used=rt->used,symbols=rt->symbol_count,string,pair,next,code_bytes;
  zero read,text,list; one add;
  FILE *file;
  const char marker[4]={'D','G','O','!'};
  char trailer[4];
#define CHECK(a,b,label) check((uint64_t)(a),(uint64_t)(b),++*id,label,context)
  CHECK(transport!=NULL,1,"native module file transport allocation");
  if (!transport) return 0;
  memcpy(transport,native_fixture_file,sizeof(native_fixture_file)); transport[0]='M';
  CHECK(dc_goal_load_native_module(rt,transport,sizeof(native_fixture_file),sync,NULL,&result),0,"foreign file magic rejected before native linking");
  CHECK(rt->used,used,"bad native file consumes no arena");
  CHECK(rt->symbol_count,symbols,"bad native file creates no names");
  free(transport);
  file=fopen("/ram/native-span.dcn","wb");
  CHECK(file!=NULL,1,"KOS native module span file opens for writing");
  if (!file) return 0;
  CHECK(fwrite(marker,1,4,file),4,"archive prefix writes");
  CHECK(fwrite(native_fixture_file,1,sizeof(native_fixture_file),file),sizeof(native_fixture_file),"exact native file bytes write");
  CHECK(fwrite(marker,1,4,file),4,"archive trailer writes");
  CHECK(fclose(file),0,"native transport file flushes and closes");
  file=fopen("/ram/native-span.dcn","rb");
  CHECK(file!=NULL,1,"KOS native module file reopens for reading");
  if (!file) return 0;
  CHECK(fseek(file,4,SEEK_SET),0,"native module seeks past enclosing prefix");
  CHECK(dc_goal_load_native_stream(rt,file,sizeof(native_fixture_file)+8,sync,NULL,&result),0,"short native stream fails before linking");
  CHECK(rt->used,used,"short stream leaves arena unchanged");
  CHECK(rt->symbol_count,symbols,"short stream creates no symbols");
  CHECK(fseek(file,4,SEEK_SET),0,"native stream caller can retry from same span");
  CHECK(dc_goal_load_native_stream(rt,file,sizeof(native_fixture_file),sync,NULL,&result),1,"serialized native code/data file links");
  CHECK(ftell(file),sizeof(native_fixture_file)+4,"native stream stops at exact declared span");
  CHECK(fread(trailer,1,4,file),4,"enclosing archive trailer remains readable");
  CHECK(memcmp(trailer,marker,4),0,"native stream leaves enclosing trailer intact");
  CHECK(fclose(file),0,"caller retains ownership of native input stream");
  CHECK(remove("/ram/native-span.dcn"),0,"temporary KOS module file removed before execution");
  if (!result.module.functions) return 0;
  CHECK(result.module.functions,5,"native file exports four functions and startup");
  CHECK(result.module.data_objects>=3 && result.module.data_objects<=4,1,"native file preserves private string/pair objects");
  CHECK(result.decoder_bytes>0,1,"native file decoder accounts for transient descriptors");
  CHECK(result.initializer_kind,DC_NATIVE_VOID_INITIALIZER,"native file records explicit void startup ABI");
  CHECK(strcmp(result.initializer,"dc-file-top-level"),0,"startup name survives source-buffer release");
  code_bytes=result.module.code_bytes;
  read=(zero)(uintptr_t)entry(rt,"dc-file-read"); add=(one)(uintptr_t)entry(rt,"dc-file-add");
  text=(zero)(uintptr_t)entry(rt,"dc-file-string"); list=(zero)(uintptr_t)entry(rt,"dc-file-list");
  CHECK(read(),0,"serialized module linking does not run startup");
  CHECK(dc_goal_type_of(rt,entry(rt,"dc-file-read")),rt->function_type,"serialized code uses native boxed function tag");
  CHECK(dc_goal_run_initializer_void(rt,result.initializer),1,"file startup executes after source/descriptors freed");
  CHECK(read(),37,"file-linked original symbol initialization");
  CHECK(entry(rt,"dc-file-applied"),42,"serialized startup calls another native export");
  CHECK(add(7),44,"file-linked native argument/call execution");
  string=(uint32_t)text(); pair=(uint32_t)list();
  CHECK(dc_goal_type_of(rt,string),rt->string_type,"file-linked private string native type");
  CHECK(word(rt,string),11,"file-linked private string length");
  CHECK(memcmp(dc_goal_resolve(rt,string+4,12),"native-file",12),0,"private string bytes survive file release");
  CHECK(dc_goal_type_of(rt,pair),rt->pair_type,"file-linked quoted graph native pair tag");
  CHECK(word(rt,pair-2),rt->s7+8u,"file-linked graph imports canonical true symbol");
  next=word(rt,pair+2);
  CHECK(dc_goal_type_of(rt,next),rt->pair_type,"file-linked graph internal data relocation");
  string=word(rt,next-2);
  CHECK(dc_goal_type_of(rt,string),rt->string_type,"quoted graph contains native string reference");
  CHECK(memcmp(dc_goal_resolve(rt,string+4,12),"native-file",12),0,"nested serialized private data bytes");
  CHECK(word(rt,next+2),rt->s7-10u,"file-linked graph retains empty-list sentinel");
  used=rt->used; dc_goal_set_symbol(rt,dc_goal_find(rt,"dc-file-value"),77);
  CHECK(read(),77,"file-linked code observes subsequent native mutation");
  CHECK(dc_goal_run_initializer_void(rt,result.initializer),1,"serialized startup can rerun explicitly");
  CHECK(read(),37,"serialized startup restores source initial value");
  CHECK(rt->used,used,"serialized startup reuses linked private objects");
  transport=malloc(sizeof(native_fixture_file));
  CHECK(transport!=NULL,1,"native corruption test transport allocation");
  if (!transport) return code_bytes;
  memcpy(transport,native_fixture_file,sizeof(native_fixture_file)); transport[sizeof(native_fixture_file)-1]^=1;
  CHECK(dc_goal_load_native_module(rt,transport,sizeof(native_fixture_file),sync,NULL,&result),0,"native module CRC rejects corrupted payload");
  CHECK(rt->used,used,"CRC rejection leaves native arena unchanged");
  CHECK(result.module.functions,0,"CRC rejection leaves result empty");
  CHECK(read(),37,"CRC rejection preserves prior executable bindings");
  free(transport);
#undef CHECK
  return code_bytes;
}
