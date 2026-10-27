/* PS2 MMI 128-bit integer operations for lowered SH-4 code (AI-assisted), ISC. */
#include "goal_mmi.h"
#include <string.h>
typedef struct {uint8_t b[16];} q128;
static uint32_t w32(const q128 *v,unsigned i){return (uint32_t)v->b[4*i]|(uint32_t)v->b[4*i+1]<<8|(uint32_t)v->b[4*i+2]<<16|(uint32_t)v->b[4*i+3]<<24;}
static void set32(q128 *v,unsigned i,uint32_t x){v->b[4*i]=(uint8_t)x;v->b[4*i+1]=(uint8_t)(x>>8);v->b[4*i+2]=(uint8_t)(x>>16);v->b[4*i+3]=(uint8_t)(x>>24);}
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
  memcpy(&a,pa,16);
  if(pb)memcpy(&b,pb,16);else memset(&b,0,16);
  memset(&r,0,16);
  switch(kind){
  /* IR.cpp emits these _swapped: x86 operands (src2, src1). */
  case DC_MMI_PEXTLB:unpack(&r,&b,&a,1,0);break;
  case DC_MMI_PEXTLH:unpack(&r,&b,&a,2,0);break;
  case DC_MMI_PEXTLW:unpack(&r,&b,&a,4,0);break;
  case DC_MMI_PEXTUB:unpack(&r,&b,&a,1,1);break;
  case DC_MMI_PEXTUH:unpack(&r,&b,&a,2,1);break;
  case DC_MMI_PEXTUW:unpack(&r,&b,&a,4,1);break;
  /* pcmpeq (commutative) and pcmpgt src1 > src2, signed elements. */
  case DC_MMI_PCEQB:for(i=0;i<16;++i)r.b[i]=a.b[i]==b.b[i]?0xff:0;break;
  case DC_MMI_PCEQH:for(i=0;i<8;++i)set16(&r,i,w16(&a,i)==w16(&b,i)?0xffff:0);break;
  case DC_MMI_PCEQW:for(i=0;i<4;++i)set32(&r,i,w32(&a,i)==w32(&b,i)?0xffffffffu:0);break;
  case DC_MMI_PCGTB:for(i=0;i<16;++i)r.b[i]=s8(a.b[i])>s8(b.b[i])?0xff:0;break;
  case DC_MMI_PCGTH:for(i=0;i<8;++i)set16(&r,i,(int16_t)w16(&a,i)>(int16_t)w16(&b,i)?0xffff:0);break;
  case DC_MMI_PCGTW:for(i=0;i<4;++i)set32(&r,i,(int32_t)w32(&a,i)>(int32_t)w32(&b,i)?0xffffffffu:0);break;
  case DC_MMI_PSUBW:for(i=0;i<4;++i)set32(&r,i,w32(&a,i)-w32(&b,i));break;  /* vpsubd src1 - src2 */
  case DC_MMI_POR:for(i=0;i<16;++i)r.b[i]=a.b[i]|b.b[i];break;
  case DC_MMI_PXOR:for(i=0;i<16;++i)r.b[i]=a.b[i]^b.b[i];break;
  case DC_MMI_PAND:for(i=0;i<16;++i)r.b[i]=a.b[i]&b.b[i];break;
  case DC_MMI_PACKUSWB:  /* vpackuswb src1, src2: signed words saturated to u8 */
    for(i=0;i<16;++i){int16_t x=(int16_t)w16(i<8?&a:&b,i&7);r.b[i]=(uint8_t)(x<0?0:x>255?255:x);}
    break;
  case DC_MMI_PADDB:for(i=0;i<16;++i)r.b[i]=(uint8_t)(a.b[i]+b.b[i]);break;
  /* Immediate shifts: x86 counts past the element width give 0 (logical) or
   * the sign fill (arithmetic). */
  case DC_MMI_PW_SLL:for(i=0;i<4;++i)set32(&r,i,imm>31?0:w32(&a,i)<<imm);break;
  case DC_MMI_PW_SRL:for(i=0;i<4;++i)set32(&r,i,imm>31?0:w32(&a,i)>>imm);break;
  case DC_MMI_PW_SRA:for(i=0;i<4;++i){int32_t x=(int32_t)w32(&a,i);set32(&r,i,(uint32_t)(imm>31?(x<0?-1:0):x>>imm));}break;
  case DC_MMI_PH_SLL:for(i=0;i<8;++i)set16(&r,i,imm>15?0:(uint16_t)(w16(&a,i)<<imm));break;
  case DC_MMI_PH_SRL:for(i=0;i<8;++i)set16(&r,i,imm>15?0:(uint16_t)(w16(&a,i)>>imm));break;
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
  memcpy(dst,&r,16);
  return 1;
}
