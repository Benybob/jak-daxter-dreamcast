/* Committed-air host entry (AI-assisted), ISC. */
#include <stdio.h>
unsigned dc_sector_air_cases(unsigned *);
int main(void){unsigned n,bad=dc_sector_air_cases(&n);printf("sector air %u/%u checks\n",n-bad,n);return bad?1:0;}
