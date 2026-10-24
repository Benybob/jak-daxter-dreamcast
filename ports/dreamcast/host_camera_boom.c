/* Camera query oracle host entry (AI-assisted), ISC. */
#include <stdio.h>
unsigned dc_camera_boom_cases(unsigned *);
unsigned dc_camera_rig_cases(unsigned *);
unsigned dc_camera_patch_cases(FILE *,unsigned *);
extern unsigned dc_camera_patch_shortened;
extern float dc_camera_patch_min;
int main(int argc,char **argv){unsigned count,n,bad=dc_camera_boom_cases(&count);
 bad+=dc_camera_rig_cases(&n);count+=n;
 if(argc==2){FILE *f=fopen(argv[1],"rb");if(!f)return 2;
  bad+=dc_camera_patch_cases(f,&n);count+=n;fclose(f);
  printf("original patch shortened=%u/64 min=%.6fm\n",dc_camera_patch_shortened,(double)dc_camera_patch_min);}
 printf("camera boom %u/%u checks\n",count-bad,count);return bad?1:0;
}
