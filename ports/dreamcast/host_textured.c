/* Native validation CLI for the bounded textured format (AI-assisted). */
#include "textured_scene.h"
int main(int argc,char **argv) {
  FILE *file;
  dc_textured_scene scene;
  const char *error;
  if (argc!=2) return 2;
  file=fopen(argv[1],"rb");
  if (!file) return 2;
  error=dc_textured_load(file,&scene);
  fclose(file);
  if (error) { fprintf(stderr,"%s\n",error); return 1; }
  printf("{\"vertices\":%lu,\"triangles\":%lu,\"batches\":%lu,\"textures\":%lu,\"texture_bytes\":%lu}\n",
         (unsigned long)scene.vertex_count,(unsigned long)(scene.index_count/3),
         (unsigned long)scene.batch_count,(unsigned long)scene.texture_count,(unsigned long)scene.texture_bytes);
  dc_textured_free(&scene);
  return 0;
}
