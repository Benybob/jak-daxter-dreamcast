/* Inspect one original data object without executing it (AI-assisted), ISC. */
#include "goal_object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int dump_relocation(const dc_goal_object_relocation *r,void *context) {
  uint32_t i;
  (void)context;
  printf("{\"kind\":%u,\"offset\":%u,\"target\":%u,\"method_hint\":%u,\"name\":",
         (unsigned)r->kind,r->offset,r->target,(unsigned)r->method_hint);
  if (!r->name) fputs("null",stdout);
  else {
    putchar('"');
    for (i=0;i<r->name_bytes;++i) {
      unsigned c=(unsigned char)r->name[i];
      if (c>=32 && c<127 && c!='"' && c!='\\') putchar((int)c);
      else printf("\\u%04x",c);
    }
    putchar('"');
  }
  puts("}"); return 1;
}
int main(int argc,char **argv) {
  FILE *file; long bytes; unsigned char *data;
  dc_goal_object_layout layout;
  const char *error;
  int dump=argc==3 && !strcmp(argv[1],"--relocations");
  if ((!dump && argc!=2) || !(file=fopen(argv[dump?2:1],"rb"))) return 2;
  if (fseek(file,0,SEEK_END) || (bytes=ftell(file))<0 || bytes>(long)DC_GOAL_OBJECT_MAX_BYTES ||
      fseek(file,0,SEEK_SET)) { fclose(file); return 2; }
  data=malloc((size_t)bytes+1);
  if (!data || fread(data,1,(size_t)bytes,file)!=(size_t)bytes) { fclose(file); free(data); return 2; }
  fclose(file);
  error=dc_goal_object_scan(data,(uint32_t)bytes,dump?dump_relocation:NULL,NULL,&layout);
  if (error) fprintf(stderr,"%s\n",error);
  else printf("{\"version\":%u,\"data_offset\":%u,\"data_bytes\":%u,\"link_offset\":%u,\"link_bytes\":%u,"
              "\"bitmap_bytes\":%u,\"internal\":%u,\"symbols\":%u,\"types\":%u,\"empty\":%u,\"imports\":%u}\n",
              layout.version,layout.data_offset,layout.data_bytes,layout.link_offset,layout.link_bytes,
              layout.bitmap_bytes,layout.internal_relocations,layout.symbol_relocations,
              layout.type_relocations,layout.empty_relocations,layout.imports);
  free(data); return error?1:0;
}
