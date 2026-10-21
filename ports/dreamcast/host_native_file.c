/* Host native-module decode/link validation; never executes SH-4 (AI-assisted), ISC. */
#include "goal_native_file.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
  FILE *file;
  void *allocation,*memory;
  long length;
  dc_goal_runtime rt; dc_native_file_result result;
  int loaded;
  if (argc!=2 || !(file=fopen(argv[1],"rb"))) return 2;
  if (fseek(file,0,SEEK_END) || (length=ftell(file))<0 || length>2097152 || fseek(file,0,SEEK_SET)) {
    fclose(file); return 2;
  }
  allocation=malloc(1048608);
  memory=(void*)(((uintptr_t)allocation+15u)&~(uintptr_t)15u);
  if (!allocation) {
    fclose(file); return 2;
  }
  if (!dc_goal_init(&rt,memory,1048576,0x8c100000u)) { fclose(file); free(allocation); return 2; }
  loaded=dc_goal_load_native_stream(&rt,file,(uint32_t)length,NULL,NULL,&result);
  fclose(file);
  printf("{\"loaded\":%u,\"error\":%u,\"used\":%u,\"symbols\":%u,\"functions\":%u,\"code_bytes\":%u,\"data_objects\":%u,\"initializer_kind\":%u}\n",
         (unsigned)loaded,(unsigned)rt.error,rt.used,rt.symbol_count,result.module.functions,
         result.module.code_bytes,result.module.data_objects,result.initializer_kind);
  free(allocation); return loaded?0:1;
}
