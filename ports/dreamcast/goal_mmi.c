/* PS2 MMI 128-bit integer operations for lowered SH-4 code (AI-assisted), ISC. */
#include "goal_mmi.h"
#include <string.h>
/* Lanes in memory order: the PS2, SH-4 and x86 hosts are all little-endian,
 * so w[i] and h[i] are the 32- and 16-bit elements (no byte assembly). */
typedef union {uint8_t b[16];uint16_t h[8];uint32_t w[4];} q128;
/* Operands are the lowered code's stack slots and GOAL fields, nearly always
 * word aligned: four word moves, not a library memcpy of 16 bytes (KOS
 * compiles with -fno-builtin; these copies were 5% of a level frame). */
typedef uint32_t __attribute__((may_alias)) word_alias;
static void load16(q128 *d,const void *s){
  if(!((uintptr_t)s&3u)){const word_alias *w=s;d->w[0]=w[0];d->w[1]=w[1];d->w[2]=w[2];d->w[3]=w[3];}
  else{const uint8_t *p=s;unsigned i;for(i=0;i<16;++i)d->b[i]=p[i];}
}
static void store16(void *d,const q128 *s){
  if(!((uintptr_t)d&3u)){word_alias *w=d;w[0]=s->w[0];w[1]=s->w[1];w[2]=s->w[2];w[3]=s->w[3];}
  else{uint8_t *p=d;unsigned i;for(i=0;i<16;++i)p[i]=s->b[i];}
}
static void zero16(q128 *d){d->w[0]=d->w[1]=d->w[2]=d->w[3]=0;}
static uint16_t w16(const q128 *v,unsigned i){return (uint16_t)(v->b[2*i]|v->b[2*i+1]<<8);}
static void set16(q128 *v,unsigned i,uint16_t x){v->b[2*i]=(uint8_t)x;v->b[2*i+1]=(uint8_t)(x>>8);}
/* x86 punpckl/h{bw,wd,dq} first, second: interleave elements of size bytes from
 * the low (high=0) or high 64 bits, first operand's element first. */
static void unpack(q128 *out,const q128 *first,const q128 *second,unsigned size,unsigned high){
  unsigned n=8/size,i,k,base=high?8:0;
  for(i=0;i<n;++i)for(k=0;k<size;++k){
    out->b[2*i*size+k]=first->b[base+i*size+k];
    out->b[(2*i+1)*size+k]=second->b[base+i*size+k];
  }
}
static int8_t s8(uint8_t x){return (int8_t)x;}
int dc_goal_mmi(uint32_t op,void *dst,const void *pa,const void *pb){
  q128 a,b,r;unsigned i,kind=op&0xffu,imm=op>>8;
  load16(&a,pa);
  if(pb)load16(&b,pb);else zero16(&b);
  zero16(&r);
  switch(kind){
  /* IR.cpp emits these _swapped: x86 operands (src2, src1). */
  case DC_MMI_PEXTLB:unpack(&r,&b,&a,1,0);break;
  case DC_MMI_PEXTLH:for(i=0;i<4;++i){r.h[2*i]=b.h[i];r.h[2*i+1]=a.h[i];}break;
  case DC_MMI_PEXTLW:r.w[0]=b.w[0];r.w[1]=a.w[0];r.w[2]=b.w[1];r.w[3]=a.w[1];break;
  case DC_MMI_PEXTUB:unpack(&r,&b,&a,1,1);break;
  case DC_MMI_PEXTUH:for(i=0;i<4;++i){r.h[2*i]=b.h[4+i];r.h[2*i+1]=a.h[4+i];}break;
  case DC_MMI_PEXTUW:r.w[0]=b.w[2];r.w[1]=a.w[2];r.w[2]=b.w[3];r.w[3]=a.w[3];break;
  /* pcmpeq (commutative) and pcmpgt src1 > src2, signed elements. */
  case DC_MMI_PCEQB:for(i=0;i<16;++i)r.b[i]=a.b[i]==b.b[i]?0xff:0;break;
  case DC_MMI_PCEQH:for(i=0;i<8;++i)r.h[i]=a.h[i]==b.h[i]?0xffff:0;break;
  case DC_MMI_PCEQW:for(i=0;i<4;++i)r.w[i]=a.w[i]==b.w[i]?0xffffffffu:0;break;
  case DC_MMI_PCGTB:for(i=0;i<16;++i)r.b[i]=s8(a.b[i])>s8(b.b[i])?0xff:0;break;
  case DC_MMI_PCGTH:for(i=0;i<8;++i)r.h[i]=(int16_t)a.h[i]>(int16_t)b.h[i]?0xffff:0;break;
  case DC_MMI_PCGTW:for(i=0;i<4;++i)r.w[i]=(int32_t)a.w[i]>(int32_t)b.w[i]?0xffffffffu:0;break;
  case DC_MMI_PSUBW:for(i=0;i<4;++i)r.w[i]=a.w[i]-b.w[i];break;  /* vpsubd src1 - src2 */
  case DC_MMI_POR:for(i=0;i<4;++i)r.w[i]=a.w[i]|b.w[i];break;
  case DC_MMI_PXOR:for(i=0;i<4;++i)r.w[i]=a.w[i]^b.w[i];break;
  case DC_MMI_PAND:for(i=0;i<4;++i)r.w[i]=a.w[i]&b.w[i];break;
  case DC_MMI_PACKUSWB:  /* vpackuswb src1, src2: signed words saturated to u8 */
    for(i=0;i<16;++i){int16_t x=(int16_t)w16(i<8?&a:&b,i&7);r.b[i]=(uint8_t)(x<0?0:x>255?255:x);}
    break;
  case DC_MMI_PADDB:for(i=0;i<16;++i)r.b[i]=(uint8_t)(a.b[i]+b.b[i]);break;
  /* Immediate shifts: x86 counts past the element width give 0 (logical) or
   * the sign fill (arithmetic). */
  case DC_MMI_PW_SLL:for(i=0;i<4;++i)r.w[i]=imm>31?0:a.w[i]<<imm;break;
  case DC_MMI_PW_SRL:for(i=0;i<4;++i)r.w[i]=imm>31?0:a.w[i]>>imm;break;
  case DC_MMI_PW_SRA:for(i=0;i<4;++i){int32_t x=(int32_t)a.w[i];r.w[i]=(uint32_t)(imm>31?(x<0?-1:0):x>>imm);}break;
  case DC_MMI_PH_SLL:for(i=0;i<8;++i)r.h[i]=imm>15?0:(uint16_t)(a.h[i]<<imm);break;
  case DC_MMI_PH_SRL:for(i=0;i<8;++i)r.h[i]=imm>15?0:(uint16_t)(a.h[i]>>imm);break;
  case DC_MMI_VPSRLDQ:for(i=0;i<16;++i)r.b[i]=imm<=15 && i+imm<16?a.b[i+imm]:0;break;
  case DC_MMI_VPSLLDQ:for(i=0;i<16;++i)r.b[i]=imm<=15 && i>=imm?a.b[i-imm]:0;break;
  case DC_MMI_VPSHUFLW:  /* low four words selected by imm, high qword copied */
    for(i=0;i<4;++i)set16(&r,i,w16(&a,(imm>>(2*i))&3u));
    for(i=4;i<8;++i)set16(&r,i,w16(&a,i));
    break;
  case DC_MMI_VPSHUFHW:
    for(i=0;i<4;++i)set16(&r,i,w16(&a,i));
    for(i=0;i<4;++i)set16(&r,4+i,w16(&a,4+((imm>>(2*i))&3u)));
    break;
  default:return 0;
  }
  store16(dst,&r);
  return 1;
}
