#include "scene_io.h"
#include <stdio.h>

int main(int argc, char **argv) {
  FILE *file;
  dc_scene scene;
  const char *error;
  if (argc != 2) { fprintf(stderr, "usage: %s SCENE.DCM\n", argv[0]); return 2; }
  file = fopen(argv[1], "rb");
  if (!file) { perror(argv[1]); return 2; }
  error = dc_scene_load(file, &scene);
  fclose(file);
  if (error) { fprintf(stderr, "%s\n", error); return 1; }
  printf("{\"vertices\":%lu,\"triangles\":%lu,\"geometry_bytes\":%lu}\n",
         (unsigned long)scene.count, (unsigned long)(scene.count/3),
         (unsigned long)(scene.count*sizeof(dc_scene_vertex)));
  dc_scene_free(&scene);
  return 0;
}
