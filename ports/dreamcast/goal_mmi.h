/* PS2 MMI 128-bit integer operations for lowered SH-4 code (AI-assisted), ISC.
 * Each kind follows the exact x86 instruction and operand order the desktop
 * OpenGOAL backend emits for it (goalc/compiler/IR.cpp, IR_Int128Math3Asm and
 * IR_Int128Math2Asm), since that is the behavior the original source runs with
 * on PC. Operands are 16-byte little-endian values; dst may alias a or b. */
#ifndef DC_GOAL_MMI_H
#define DC_GOAL_MMI_H
#include <stdint.h>
enum dc_goal_mmi_kind {
  DC_MMI_PEXTLB=1,DC_MMI_PEXTLH,DC_MMI_PEXTLW,DC_MMI_PEXTUB,DC_MMI_PEXTUH,DC_MMI_PEXTUW,
  DC_MMI_PCEQB,DC_MMI_PCEQH,DC_MMI_PCEQW,DC_MMI_PCGTB,DC_MMI_PCGTH,DC_MMI_PCGTW,
  DC_MMI_PSUBW,DC_MMI_POR,DC_MMI_PXOR,DC_MMI_PAND,DC_MMI_PACKUSWB,DC_MMI_PADDB,
  /* one source and an immediate (op = kind | imm << 8) */
  DC_MMI_PW_SLL=32,DC_MMI_PW_SRL,DC_MMI_PW_SRA,DC_MMI_PH_SLL,DC_MMI_PH_SRL,
  DC_MMI_VPSRLDQ,DC_MMI_VPSLLDQ,DC_MMI_VPSHUFLW,DC_MMI_VPSHUFHW
};
/* Returns 1, or 0 for an unknown kind (dst unchanged). */
int dc_goal_mmi(uint32_t op,void *dst,const void *a,const void *b);
#endif
