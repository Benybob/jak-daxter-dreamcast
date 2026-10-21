/* Execute independent x86 SSE predicate/conversion oracle (AI-assisted), ISC. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(__x86_64__) || defined(__i386__)
#include "float_vectors.h"
static float from_bits(uint32_t bits) { float value; memcpy(&value,&bits,4); return value; }
static unsigned compare(float left,float right) {
  unsigned char eq,ne,lt,le,gt,ge;
  __asm__ volatile("ucomiss %[right],%[left]\n\tsete %[eq]\n\tsetne %[ne]\n\t"
                   "setb %[lt]\n\tsetbe %[le]\n\tseta %[gt]\n\tsetae %[ge]"
    : [eq]"=qm"(eq),[ne]"=qm"(ne),[lt]"=qm"(lt),[le]"=qm"(le),[gt]"=qm"(gt),[ge]"=qm"(ge)
    : [left]"x"(left),[right]"x"(right) : "cc");
  return eq|(ne<<1)|(lt<<2)|(le<<3)|(gt<<4)|(ge<<5);
}
int main(void) {
  unsigned i,total=0,passed=0;
  for (i=0;i<sizeof(dc_float_vectors)/sizeof(dc_float_vectors[0]);++i) {
    ++total;
    if (compare(from_bits(dc_float_vectors[i].left),from_bits(dc_float_vectors[i].right))==dc_float_vectors[i].mask) ++passed;
  }
  for (i=0;i<sizeof(dc_float_int_inputs)/sizeof(dc_float_int_inputs[0]);++i) {
    int32_t actual; float input=from_bits(dc_float_int_inputs[i]);
    __asm__ volatile("cvttss2si %1,%0" : "=r"(actual) : "x"(input));
    ++total; if (actual==dc_float_int_outputs[i]) ++passed;
  }
  printf("{\"tests\":%u,\"passed\":%u}\n",total,passed);
  return total==passed?0:1;
}
#else
int main(void) { return 77; }
#endif
