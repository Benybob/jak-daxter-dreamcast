/* AI-assisted, ISC. */
#include <stdio.h>
unsigned dc_player_motion_cases(unsigned *);
int main(void){unsigned n,bad=dc_player_motion_cases(&n);printf("player motion %u/%u checks\n",n-bad,n);return bad?1:0;}
