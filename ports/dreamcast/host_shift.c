/* Execute actual x86 shifts as the frontend-contract oracle (AI-assisted), ISC. */
#include "shift_vectors.h"
#include <stdio.h>
#if defined(__x86_64__)
static uint64_t actual(unsigned kind,uint64_t value,uint64_t amount) {
  if (!kind) __asm__ volatile("shlq %%cl,%0" : "+r"(value) : "c"(amount) : "cc");
  else if (kind==1) __asm__ volatile("shrq %%cl,%0" : "+r"(value) : "c"(amount) : "cc");
  else __asm__ volatile("sarq %%cl,%0" : "+r"(value) : "c"(amount) : "cc");
  return value;
}
int main(void) {
  unsigned i,j,kind,total=0,passed=0;
  for (i=0;i<DC_SHIFT_VALUE_COUNT;++i) for (j=0;j<DC_SHIFT_COUNT_COUNT;++j) for (kind=0;kind<3;++kind) {
    uint64_t value=dc_shift_values[i],amount=dc_shift_count(j);
    ++total; if (actual(kind,value,amount)==dc_shift_expected(kind,value,amount)) ++passed;
  }
  printf("{\"tests\":%u,\"passed\":%u}\n",total,passed);
  return total==passed?0:1;
}
#else
int main(void) { return 77; }
#endif
