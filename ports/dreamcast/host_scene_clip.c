/* Portable clipping oracle entry (AI-assisted), ISC. */
#include <stdio.h>
unsigned dc_scene_clip_cases(unsigned *);
int main(void){unsigned count,bad=dc_scene_clip_cases(&count);
 printf("scene clip %u/%u checks\n",count-bad,count);return bad?1:0;}
