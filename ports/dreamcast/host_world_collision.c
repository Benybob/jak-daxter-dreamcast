/* AI-assisted native capsule host oracle, ISC. */
#include <stdio.h>
unsigned dc_world_collision_cases(unsigned *);
unsigned dc_world_player_cases(unsigned *);
unsigned dc_world_patch_cases(FILE *,unsigned *);
int main(int argc,char **argv){unsigned count,n,bad;
 bad=dc_world_collision_cases(&count);bad+=dc_world_player_cases(&n);count+=n;
 if(argc==2){FILE *file=fopen(argv[1],"rb");if(!file)return 2;bad+=dc_world_patch_cases(file,&n);count+=n;fclose(file);}
 printf("world collision %u/%u checks\n",count-bad,count);return bad?1:0;
}
