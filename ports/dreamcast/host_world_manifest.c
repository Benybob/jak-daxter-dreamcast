/* Host bundle checker (AI-assisted), ISC. */
#include "world_manifest.h"
#include <stdio.h>
#include <string.h>
unsigned dc_world_manifest_cases(const char *,unsigned *);
int main(int argc,char **argv){
 dc_world_manifest m;dc_collision_world world={0};dc_textured_scene scene={0};dc_vec3 origin;
 FILE *f;const char *error=NULL;unsigned i;int result=1;
 if(argc==3&&!strcmp(argv[1],"--self-test")){unsigned n,bad=dc_world_manifest_cases(argv[2],&n);
  printf("world manifest %u/%u checks\n",n-bad,n);return bad?1:0;}
 if(argc!=4)return 2;
 f=fopen(argv[1],"rb");if(!f)return 2;error=dc_world_manifest_load(f,&m);fclose(f);if(error)goto done;
 for(i=0;i<2;++i){
  f=fopen(argv[i+2],"rb");if(!f){error="missing bundle file";goto done;}
  error=dc_world_manifest_check_file(f,i?m.dcc_bytes:m.dct_bytes,i?m.dcc_crc:m.dct_crc);
  if(!error)error=i?dc_collision_load(f,&world,&origin):dc_textured_load(f,&scene);
  fclose(f);if(error)goto done;
 }
 error=dc_world_manifest_match(&m,&scene,&world,origin);if(error)goto done;
 printf("{\"result\":\"PASS\",\"triangles\":%lu,\"collision_triangles\":%lu}\n",
        (unsigned long)(scene.index_count/3),(unsigned long)world.count);result=0;
done:
 if(error)fprintf(stderr,"%s\n",error);
 dc_textured_free(&scene);dc_collision_free(&world);return result;
}
