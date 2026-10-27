/* Native DGO load-and-link of DCSH objects (AI-assisted), ISC. */
#include "goal_dgo.h"
#include <string.h>
static uint32_t word(const unsigned char *p) {
  return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
/* A 60-byte NUL-terminated name with zero fill, as in the original header. */
static int valid_name(const unsigned char *name) {
  uint32_t i=0;
  while(i<60 && name[i])++i;
  if(i==0 || i==60)return 0;
  while(i<60)if(name[i++])return 0;
  return 1;
}
static int fail(dc_goal_runtime *rt,dc_goal_dgo_result *result,uint32_t index,enum dc_goal_error error) {
  result->failed_object=index;
  if(rt->error==DC_GOAL_OK)rt->error=error;
  return 0;
}
int dc_goal_load_dgo(dc_goal_runtime *rt,FILE *file,dc_goal_cache_sync sync,void *context,
                     dc_goal_dgo_result *result) {
  unsigned char header[64],padding[16];
  uint32_t i;
  if(result)memset(result,0,sizeof *result);
  if(!rt || !file || !result){if(rt)rt->error=DC_GOAL_BAD_ARGUMENT;return 0;}
  result->failed_object=UINT32_MAX;rt->error=DC_GOAL_OK;result->arena_before=rt->used;
  dc_goal_sticky_error=DC_GOAL_OK;
  if(fread(header,1,64,file)!=64 || !memcmp(header,"oZlB",4) || !valid_name(header+4))
    return fail(rt,result,UINT32_MAX,DC_GOAL_BAD_MODULE);
  memcpy(result->name,header+4,60);
  result->objects=word(header);
  if(!result->objects || result->objects>DC_GOAL_DGO_MAX_OBJECTS)
    return fail(rt,result,UINT32_MAX,DC_GOAL_BAD_MODULE);
  for(i=0;i<result->objects;++i) {
    dc_native_file_result object;uint32_t size,pad;uint64_t value;
    if(fread(header,1,64,file)!=64 || !valid_name(header+4))return fail(rt,result,i,DC_GOAL_BAD_MODULE);
    memcpy(result->object_names[i],header+4,60);
    size=word(header);
    if(!dc_goal_load_native_stream(rt,file,size,sync,context,&object))return fail(rt,result,i,DC_GOAL_BAD_MODULE);
    result->code_bytes+=object.module.code_bytes;
    pad=(16u-(size&15u))&15u;
    if(fread(padding,1,pad,file)!=pad)return fail(rt,result,i,DC_GOAL_BAD_MODULE);
    /* Like the original link_and_exec, the object's startup runs before the next
     * object is read, so later objects see what earlier startups defined. */
    if(object.initializer_kind==DC_NATIVE_UINT64_INITIALIZER) {
      if(!dc_goal_run_initializer(rt,object.initializer,&value))return fail(rt,result,i,DC_GOAL_BAD_FUNCTION);
      ++result->initializers;
    } else if(object.initializer_kind==DC_NATIVE_VOID_INITIALIZER) {
      if(!dc_goal_run_initializer_void(rt,object.initializer))return fail(rt,result,i,DC_GOAL_BAD_FUNCTION);
      ++result->initializers;
    }
    if(rt->error!=DC_GOAL_OK)return fail(rt,result,i,rt->error);
    if(dc_goal_sticky_error!=DC_GOAL_OK)return fail(rt,result,i,dc_goal_sticky_error);
    result->arena_after[i]=rt->used;
    ++result->completed;
  }
  if(fgetc(file)!=EOF || ferror(file))return fail(rt,result,UINT32_MAX,DC_GOAL_BAD_MODULE);
  return 1;
}
