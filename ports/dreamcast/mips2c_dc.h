/* Native SH-4 mips2c functions for the Dreamcast runtime (AI-assisted), ISC.
 *
 * The PC port's C++ translations of the original MIPS/VU assembly (game/mips2c)
 * build for SH-4 unchanged, against these services (mips2c_dc.cpp). GOAL
 * addresses are P0 aliases of the same RAM and the MMU is off, so
 * g_ee_main_mem is 0 and a GOAL address is a C address. */
#ifndef DC_MIPS2C_H
#define DC_MIPS2C_H
#include <stdint.h>
#include "goal_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Runs every built file's link() (symbol caches and registrations) against rt.
 * Returns the number of registered functions, 0 on failure. */
unsigned dc_mips2c_install(dc_goal_runtime *rt);
/* The native entry for a registered name: a C function with the GOAL
 * function's own native signature (float arguments and result where GOAL has
 * them). 0 when the name is not built. Overrides goal_bridge.c's weak default. */
uint32_t dc_mips2c_find(const char *name);
/* Diagnostics: calls made through the native wrappers, and the first failed
 * mips2c assertion (NULL if none). */
uint32_t dc_mips2c_calls(void);
const char *dc_mips2c_assert_text(void);
#ifdef __cplusplus
}
#endif
#endif
