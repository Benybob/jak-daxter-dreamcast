/*
 * Copyright (c) 2026 (AI-assisted)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef REGISTER_SNAPSHOT_H
#define REGISTER_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Same-stack, read-only image of selected SH-4 register values.
 * Separate storage from the original CPU/thread object. This header does not
 * modify that object's layout.
 *
 * out is caller-owned raw storage: 60 writable bytes, 4-byte aligned, and
 * disjoint from stack memory the caller is observing. The pointer carries no
 * heap, type, or ownership meaning.
 *
 * Returns 1 after a complete image is stored. Returns 0, with no store to
 * out, when out is null, out is not 4-byte aligned, or FPSCR bits FR, SZ,
 * and PR (mask 0x00380000) are not all clear. FPU enabled (SR.FD clear) is
 * a caller precondition.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct dc_goal_native_regs {
    uint32_t r8;     /* 0 */
    uint32_t r9;     /* 4 */
    uint32_t r10;    /* 8 */
    uint32_t r11;    /* 12 */
    uint32_t r12;    /* 16 */
    uint32_t r13;    /* 20 */
    uint32_t r14;    /* 24 */
    uint32_t pr;     /* 28 entry PR, data copy */
    uint32_t sp;     /* 32 entry SP (R15), data copy */
    uint32_t fpul;   /* 36 */
    uint32_t fpscr;  /* 40 full 32 bits */
    uint32_t fr12;   /* 44 bitwise */
    uint32_t fr13;   /* 48 bitwise */
    uint32_t fr14;   /* 52 bitwise */
    uint32_t fr15;   /* 56 bitwise */
};

#if defined(__cplusplus)
#define DC_GOAL_REGS_CHECK(cond, name) static_assert(cond, #name)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define DC_GOAL_REGS_CHECK(cond, name) _Static_assert(cond, #name)
#else
#define DC_GOAL_REGS_CHECK(cond, name) \
    typedef char dc_goal_regs_check_##name[((cond) ? 1 : -1)]
#endif

DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, r8) == 0, r8_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, r9) == 4, r9_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, r10) == 8, r10_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, r11) == 12, r11_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, r12) == 16, r12_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, r13) == 20, r13_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, r14) == 24, r14_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, pr) == 28, pr_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, sp) == 32, sp_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, fpul) == 36, fpul_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, fpscr) == 40, fpscr_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, fr12) == 44, fr12_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, fr13) == 48, fr13_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, fr14) == 52, fr14_off);
DC_GOAL_REGS_CHECK(offsetof(struct dc_goal_native_regs, fr15) == 56, fr15_off);
DC_GOAL_REGS_CHECK(sizeof(struct dc_goal_native_regs) == 60, byte_count);
DC_GOAL_REGS_CHECK(__alignof__(struct dc_goal_native_regs) == 4, align_4);

#undef DC_GOAL_REGS_CHECK

uint32_t dc_goal_native_regs_snapshot(struct dc_goal_native_regs *out);

#ifdef __cplusplus
}
#endif

#endif /* REGISTER_SNAPSHOT_H */
