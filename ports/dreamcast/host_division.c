/* Actual x86 IDIV/DIV valid-domain oracle (AI-assisted), ISC. */
#include "division_vectors.h"
#include <stdio.h>
#if defined(__x86_64__)
static uint64_t actual(unsigned kind,uint64_t a,uint64_t b) {
  uint32_t q=(uint32_t)a,r,divisor=(uint32_t)b;
  if (kind<2) __asm__ volatile("cdq\n\tidivl %2" : "+a"(q),"=&d"(r) : "r"(divisor) : "cc");
  else __asm__ volatile("xorl %%edx,%%edx\n\tdivl %2" : "+a"(q),"=&d"(r) : "r"(divisor) : "cc");
  return dc_div_sign_extend(kind&1u?r:q);
}
int main(void) {
  unsigned i,j,kind,total=0,passed=0;
  for (i=0;i<DC_DIV_VALUE_COUNT;++i) for (j=0;j<DC_DIV_VALUE_COUNT;++j) for (kind=0;kind<4;++kind)
    if (dc_div_valid(kind,dc_div_values[i],dc_div_values[j])) {
      ++total;
      if (actual(kind,dc_div_values[i],dc_div_values[j])==dc_div_expected(kind,dc_div_values[i],dc_div_values[j])) ++passed;
    }
  printf("{\"tests\":%u,\"passed\":%u}\n",total,passed);
  return total==passed?0:1;
}
#else
int main(void) { return 77; }
#endif
