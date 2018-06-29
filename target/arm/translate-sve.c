/*
 * AArch64 SVE translation
 *
 * Copyright (c) 2018 Linaro, Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg-op.h"
#include "tcg-op-gvec.h"
#include "tcg-gvec-desc.h"
#include "qemu/log.h"
#include "arm_ldst.h"
#include "translate.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "trace-tcg.h"
#include "translate-a64.h"


typedef void GVecGen2sFn(unsigned, uint32_t, uint32_t,
                         TCGv_i64, uint32_t, uint32_t);

typedef void gen_helper_gvec_flags_3(TCGv_i32, TCGv_ptr, TCGv_ptr,
                                     TCGv_ptr, TCGv_i32);
typedef void gen_helper_gvec_flags_4(TCGv_i32, TCGv_ptr, TCGv_ptr,
                                     TCGv_ptr, TCGv_ptr, TCGv_i32);

typedef void gen_helper_gvec_mem(TCGv_env, TCGv_ptr, TCGv_i64, TCGv_i32);
typedef void gen_helper_gvec_mem_scatter(TCGv_env, TCGv_ptr, TCGv_ptr,
                                         TCGv_ptr, TCGv_i64, TCGv_i32);

/*
 * Helpers for extracting complex instruction fields.
 */

/* See e.g. ASR (immediate, predicated).
 * Returns -1 for unallocated encoding; diagnose later.
 */
static int tszimm_esz(int x)
{
    x >>= 3;  /* discard imm3 */
    return 31 - clz32(x);
}

static int tszimm_shr(int x)
{
    return (16 << tszimm_esz(x)) - x;
}

/* See e.g. LSL (immediate, predicated).  */
static int tszimm_shl(int x)
{
    return x - (8 << tszimm_esz(x));
}

static inline int plus1(int x)
{
    return x + 1;
}

/* The SH bit is in bit 8.  Extract the low 8 and shift.  */
static inline int expand_imm_sh8s(int x)
{
    return (int8_t)x << (x & 0x100 ? 8 : 0);
}

static inline int expand_imm_sh8u(int x)
{
    return (uint8_t)x << (x & 0x100 ? 8 : 0);
}

/* Convert a 2-bit memory size (msz) to a 4-bit data type (dtype)
 * with unsigned data.  C.f. SVE Memory Contiguous Load Group.
 */
static inline int msz_dtype(int msz)
{
    static const uint8_t dtype[4] = { 0, 5, 10, 15 };
    return dtype[msz];
}

/*
 * Include the generated decoder.
 */

#include "decode-sve.inc.c"

/*
 * Implement all of the translator functions referenced by the decoder.
 */

/* Return the offset info CPUARMState of the predicate vector register Pn.
 * Note for this purpose, FFR is P16.
 */
static inline int pred_full_reg_offset(DisasContext *s, int regno)
{
    return offsetof(CPUARMState, vfp.pregs[regno]);
}

/* Return the byte size of the whole predicate register, VL / 64.  */
static inline int pred_full_reg_size(DisasContext *s)
{
    return s->sve_len >> 3;
}

/* Round up the size of a register to a size allowed by
 * the tcg vector infrastructure.  Any operation which uses this
 * size may assume that the bits above pred_full_reg_size are zero,
 * and must leave them the same way.
 *
 * Note that this is not needed for the vector registers as they
 * are always properly sized for tcg vectors.
 */
static int size_for_gvec(int size)
{
    if (size <= 8) {
        return 8;
    } else {
        return QEMU_ALIGN_UP(size, 16);
    }
}

static int pred_gvec_reg_size(DisasContext *s)
{
    return size_for_gvec(pred_full_reg_size(s));
}

/* Invoke a vector expander on two Zregs.  */
static bool do_vector2_z(DisasContext *s, GVecGen2Fn *gvec_fn,
                         int esz, int rd, int rn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        gvec_fn(esz, vec_full_reg_offset(s, rd),
                vec_full_reg_offset(s, rn), vsz, vsz);
    }
    return true;
}

/* Invoke a vector expander on three Zregs.  */
static bool do_vector3_z(DisasContext *s, GVecGen3Fn *gvec_fn,
                         int esz, int rd, int rn, int rm)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        gvec_fn(esz, vec_full_reg_offset(s, rd),
                vec_full_reg_offset(s, rn),
                vec_full_reg_offset(s, rm), vsz, vsz);
    }
    return true;
}

/* Invoke a vector move on two Zregs.  */
static bool do_mov_z(DisasContext *s, int rd, int rn)
{
    return do_vector2_z(s, tcg_gen_gvec_mov, 0, rd, rn);
}

/* Initialize a Zreg with replications of a 64-bit immediate.  */
static void do_dupi_z(DisasContext *s, int rd, uint64_t word)
{
    unsigned vsz = vec_full_reg_size(s);
    tcg_gen_gvec_dup64i(vec_full_reg_offset(s, rd), vsz, vsz, word);
}

/* Invoke a vector expander on two Pregs.  */
static bool do_vector2_p(DisasContext *s, GVecGen2Fn *gvec_fn,
                         int esz, int rd, int rn)
{
    if (sve_access_check(s)) {
        unsigned psz = pred_gvec_reg_size(s);
        gvec_fn(esz, pred_full_reg_offset(s, rd),
                pred_full_reg_offset(s, rn), psz, psz);
    }
    return true;
}

/* Invoke a vector expander on three Pregs.  */
static bool do_vector3_p(DisasContext *s, GVecGen3Fn *gvec_fn,
                         int esz, int rd, int rn, int rm)
{
    if (sve_access_check(s)) {
        unsigned psz = pred_gvec_reg_size(s);
        gvec_fn(esz, pred_full_reg_offset(s, rd),
                pred_full_reg_offset(s, rn),
                pred_full_reg_offset(s, rm), psz, psz);
    }
    return true;
}

/* Invoke a vector operation on four Pregs.  */
static bool do_vecop4_p(DisasContext *s, const GVecGen4 *gvec_op,
                        int rd, int rn, int rm, int rg)
{
    if (sve_access_check(s)) {
        unsigned psz = pred_gvec_reg_size(s);
        tcg_gen_gvec_4(pred_full_reg_offset(s, rd),
                       pred_full_reg_offset(s, rn),
                       pred_full_reg_offset(s, rm),
                       pred_full_reg_offset(s, rg),
                       psz, psz, gvec_op);
    }
    return true;
}

/* Invoke a vector move on two Pregs.  */
static bool do_mov_p(DisasContext *s, int rd, int rn)
{
    return do_vector2_p(s, tcg_gen_gvec_mov, 0, rd, rn);
}

/* Set the cpu flags as per a return from an SVE helper.  */
static void do_pred_flags(TCGv_i32 t)
{
    tcg_gen_mov_i32(cpu_NF, t);
    tcg_gen_andi_i32(cpu_ZF, t, 2);
    tcg_gen_andi_i32(cpu_CF, t, 1);
    tcg_gen_movi_i32(cpu_VF, 0);
}

/* Subroutines computing the ARM PredTest psuedofunction.  */
static void do_predtest1(TCGv_i64 d, TCGv_i64 g)
{
    TCGv_i32 t = tcg_temp_new_i32();

    gen_helper_sve_predtest1(t, d, g);
    do_pred_flags(t);
    tcg_temp_free_i32(t);
}

static void do_predtest(DisasContext *s, int dofs, int gofs, int words)
{
    TCGv_ptr dptr = tcg_temp_new_ptr();
    TCGv_ptr gptr = tcg_temp_new_ptr();
    TCGv_i32 t;

    tcg_gen_addi_ptr(dptr, cpu_env, dofs);
    tcg_gen_addi_ptr(gptr, cpu_env, gofs);
    t = tcg_const_i32(words);

    gen_helper_sve_predtest(t, dptr, gptr, t);
    tcg_temp_free_ptr(dptr);
    tcg_temp_free_ptr(gptr);

    do_pred_flags(t);
    tcg_temp_free_i32(t);
}

/* For each element size, the bits within a predicate word that are active.  */
const uint64_t pred_esz_masks[4] = {
    0xffffffffffffffffull, 0x5555555555555555ull,
    0x1111111111111111ull, 0x0101010101010101ull
};

/*
 *** SVE Logical - Unpredicated Group
 */

static bool trans_AND_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_and, 0, a->rd, a->rn, a->rm);
}

static bool trans_ORR_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    if (a->rn == a->rm) { /* MOV */
        return do_mov_z(s, a->rd, a->rn);
    } else {
        return do_vector3_z(s, tcg_gen_gvec_or, 0, a->rd, a->rn, a->rm);
    }
}

static bool trans_EOR_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_xor, 0, a->rd, a->rn, a->rm);
}

static bool trans_BIC_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_andc, 0, a->rd, a->rn, a->rm);
}

/*
 *** SVE Integer Arithmetic - Unpredicated Group
 */

static bool trans_ADD_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_add, a->esz, a->rd, a->rn, a->rm);
}

static bool trans_SUB_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_sub, a->esz, a->rd, a->rn, a->rm);
}

static bool trans_SQADD_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_ssadd, a->esz, a->rd, a->rn, a->rm);
}

static bool trans_SQSUB_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_sssub, a->esz, a->rd, a->rn, a->rm);
}

static bool trans_UQADD_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_usadd, a->esz, a->rd, a->rn, a->rm);
}

static bool trans_UQSUB_zzz(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_vector3_z(s, tcg_gen_gvec_ussub, a->esz, a->rd, a->rn, a->rm);
}

/*
 *** SVE Integer Arithmetic - Binary Predicated Group
 */

static bool do_zpzz_ool(DisasContext *s, arg_rprr_esz *a, gen_helper_gvec_4 *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        tcg_gen_gvec_4_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           pred_full_reg_offset(s, a->pg),
                           vsz, vsz, 0, fn);
    }
    return true;
}

#define DO_ZPZZ(NAME, name) \
static bool trans_##NAME##_zpzz(DisasContext *s, arg_rprr_esz *a,         \
                                uint32_t insn)                            \
{                                                                         \
    static gen_helper_gvec_4 * const fns[4] = {                           \
        gen_helper_sve_##name##_zpzz_b, gen_helper_sve_##name##_zpzz_h,   \
        gen_helper_sve_##name##_zpzz_s, gen_helper_sve_##name##_zpzz_d,   \
    };                                                                    \
    return do_zpzz_ool(s, a, fns[a->esz]);                                \
}

DO_ZPZZ(AND, and)
DO_ZPZZ(EOR, eor)
DO_ZPZZ(ORR, orr)
DO_ZPZZ(BIC, bic)

DO_ZPZZ(ADD, add)
DO_ZPZZ(SUB, sub)

DO_ZPZZ(SMAX, smax)
DO_ZPZZ(UMAX, umax)
DO_ZPZZ(SMIN, smin)
DO_ZPZZ(UMIN, umin)
DO_ZPZZ(SABD, sabd)
DO_ZPZZ(UABD, uabd)

DO_ZPZZ(MUL, mul)
DO_ZPZZ(SMULH, smulh)
DO_ZPZZ(UMULH, umulh)

DO_ZPZZ(ASR, asr)
DO_ZPZZ(LSR, lsr)
DO_ZPZZ(LSL, lsl)

static bool trans_SDIV_zpzz(DisasContext *s, arg_rprr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_4 * const fns[4] = {
        NULL, NULL, gen_helper_sve_sdiv_zpzz_s, gen_helper_sve_sdiv_zpzz_d
    };
    return do_zpzz_ool(s, a, fns[a->esz]);
}

static bool trans_UDIV_zpzz(DisasContext *s, arg_rprr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_4 * const fns[4] = {
        NULL, NULL, gen_helper_sve_udiv_zpzz_s, gen_helper_sve_udiv_zpzz_d
    };
    return do_zpzz_ool(s, a, fns[a->esz]);
}

DO_ZPZZ(SEL, sel)

#undef DO_ZPZZ

/*
 *** SVE Integer Arithmetic - Unary Predicated Group
 */

static bool do_zpz_ool(DisasContext *s, arg_rpr_esz *a, gen_helper_gvec_3 *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           pred_full_reg_offset(s, a->pg),
                           vsz, vsz, 0, fn);
    }
    return true;
}

#define DO_ZPZ(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rpr_esz *a, uint32_t insn) \
{                                                                   \
    static gen_helper_gvec_3 * const fns[4] = {                     \
        gen_helper_sve_##name##_b, gen_helper_sve_##name##_h,       \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d,       \
    };                                                              \
    return do_zpz_ool(s, a, fns[a->esz]);                           \
}

DO_ZPZ(CLS, cls)
DO_ZPZ(CLZ, clz)
DO_ZPZ(CNT_zpz, cnt_zpz)
DO_ZPZ(CNOT, cnot)
DO_ZPZ(NOT_zpz, not_zpz)
DO_ZPZ(ABS, abs)
DO_ZPZ(NEG, neg)

static bool trans_FABS(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_fabs_h,
        gen_helper_sve_fabs_s,
        gen_helper_sve_fabs_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_FNEG(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_fneg_h,
        gen_helper_sve_fneg_s,
        gen_helper_sve_fneg_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_SXTB(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_sxtb_h,
        gen_helper_sve_sxtb_s,
        gen_helper_sve_sxtb_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_UXTB(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_uxtb_h,
        gen_helper_sve_uxtb_s,
        gen_helper_sve_uxtb_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_SXTH(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL, NULL,
        gen_helper_sve_sxth_s,
        gen_helper_sve_sxth_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_UXTH(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL, NULL,
        gen_helper_sve_uxth_s,
        gen_helper_sve_uxth_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_SXTW(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ool(s, a, a->esz == 3 ? gen_helper_sve_sxtw_d : NULL);
}

static bool trans_UXTW(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ool(s, a, a->esz == 3 ? gen_helper_sve_uxtw_d : NULL);
}

#undef DO_ZPZ

/*
 *** SVE Integer Reduction Group
 */

typedef void gen_helper_gvec_reduc(TCGv_i64, TCGv_ptr, TCGv_ptr, TCGv_i32);
static bool do_vpz_ool(DisasContext *s, arg_rpr_esz *a,
                       gen_helper_gvec_reduc *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr t_zn, t_pg;
    TCGv_i32 desc;
    TCGv_i64 temp;

    if (fn == NULL) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    desc = tcg_const_i32(simd_desc(vsz, vsz, 0));
    temp = tcg_temp_new_i64();
    t_zn = tcg_temp_new_ptr();
    t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zn, cpu_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, a->pg));
    fn(temp, t_zn, t_pg, desc);
    tcg_temp_free_ptr(t_zn);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_i32(desc);

    write_fp_dreg(s, a->rd, temp);
    tcg_temp_free_i64(temp);
    return true;
}

#define DO_VPZ(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rpr_esz *a, uint32_t insn) \
{                                                                        \
    static gen_helper_gvec_reduc * const fns[4] = {                      \
        gen_helper_sve_##name##_b, gen_helper_sve_##name##_h,            \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d,            \
    };                                                                   \
    return do_vpz_ool(s, a, fns[a->esz]);                                \
}

DO_VPZ(ORV, orv)
DO_VPZ(ANDV, andv)
DO_VPZ(EORV, eorv)

DO_VPZ(UADDV, uaddv)
DO_VPZ(SMAXV, smaxv)
DO_VPZ(UMAXV, umaxv)
DO_VPZ(SMINV, sminv)
DO_VPZ(UMINV, uminv)

static bool trans_SADDV(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_reduc * const fns[4] = {
        gen_helper_sve_saddv_b, gen_helper_sve_saddv_h,
        gen_helper_sve_saddv_s, NULL
    };
    return do_vpz_ool(s, a, fns[a->esz]);
}

#undef DO_VPZ

/*
 *** SVE Shift by Immediate - Predicated Group
 */

/* Store zero into every active element of Zd.  We will use this for two
 * and three-operand predicated instructions for which logic dictates a
 * zero result.
 */
static bool do_clr_zp(DisasContext *s, int rd, int pg, int esz)
{
    static gen_helper_gvec_2 * const fns[4] = {
        gen_helper_sve_clr_b, gen_helper_sve_clr_h,
        gen_helper_sve_clr_s, gen_helper_sve_clr_d,
    };
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2_ool(vec_full_reg_offset(s, rd),
                           pred_full_reg_offset(s, pg),
                           vsz, vsz, 0, fns[esz]);
    }
    return true;
}

/* Copy Zn into Zd, storing zeros into inactive elements.  */
static void do_movz_zpz(DisasContext *s, int rd, int rn, int pg, int esz)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_movz_b, gen_helper_sve_movz_h,
        gen_helper_sve_movz_s, gen_helper_sve_movz_d,
    };
    unsigned vsz = vec_full_reg_size(s);
    tcg_gen_gvec_3_ool(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       pred_full_reg_offset(s, pg),
                       vsz, vsz, 0, fns[esz]);
}

static bool do_zpzi_ool(DisasContext *s, arg_rpri_esz *a,
                        gen_helper_gvec_3 *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           pred_full_reg_offset(s, a->pg),
                           vsz, vsz, a->imm, fn);
    }
    return true;
}

static bool trans_ASR_zpzi(DisasContext *s, arg_rpri_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_asr_zpzi_b, gen_helper_sve_asr_zpzi_h,
        gen_helper_sve_asr_zpzi_s, gen_helper_sve_asr_zpzi_d,
    };
    if (a->esz < 0) {
        /* Invalid tsz encoding -- see tszimm_esz. */
        return false;
    }
    /* Shift by element size is architecturally valid.  For
       arithmetic right-shift, it's the same as by one less. */
    a->imm = MIN(a->imm, (8 << a->esz) - 1);
    return do_zpzi_ool(s, a, fns[a->esz]);
}

static bool trans_LSR_zpzi(DisasContext *s, arg_rpri_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_lsr_zpzi_b, gen_helper_sve_lsr_zpzi_h,
        gen_helper_sve_lsr_zpzi_s, gen_helper_sve_lsr_zpzi_d,
    };
    if (a->esz < 0) {
        return false;
    }
    /* Shift by element size is architecturally valid.
       For logical shifts, it is a zeroing operation.  */
    if (a->imm >= (8 << a->esz)) {
        return do_clr_zp(s, a->rd, a->pg, a->esz);
    } else {
        return do_zpzi_ool(s, a, fns[a->esz]);
    }
}

static bool trans_LSL_zpzi(DisasContext *s, arg_rpri_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_lsl_zpzi_b, gen_helper_sve_lsl_zpzi_h,
        gen_helper_sve_lsl_zpzi_s, gen_helper_sve_lsl_zpzi_d,
    };
    if (a->esz < 0) {
        return false;
    }
    /* Shift by element size is architecturally valid.
       For logical shifts, it is a zeroing operation.  */
    if (a->imm >= (8 << a->esz)) {
        return do_clr_zp(s, a->rd, a->pg, a->esz);
    } else {
        return do_zpzi_ool(s, a, fns[a->esz]);
    }
}

static bool trans_ASRD(DisasContext *s, arg_rpri_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_asrd_b, gen_helper_sve_asrd_h,
        gen_helper_sve_asrd_s, gen_helper_sve_asrd_d,
    };
    if (a->esz < 0) {
        return false;
    }
    /* Shift by element size is architecturally valid.  For arithmetic
       right shift for division, it is a zeroing operation.  */
    if (a->imm >= (8 << a->esz)) {
        return do_clr_zp(s, a->rd, a->pg, a->esz);
    } else {
        return do_zpzi_ool(s, a, fns[a->esz]);
    }
}

/*
 *** SVE Bitwise Shift - Predicated Group
 */

#define DO_ZPZW(NAME, name) \
static bool trans_##NAME##_zpzw(DisasContext *s, arg_rprr_esz *a,         \
                                uint32_t insn)                            \
{                                                                         \
    static gen_helper_gvec_4 * const fns[3] = {                           \
        gen_helper_sve_##name##_zpzw_b, gen_helper_sve_##name##_zpzw_h,   \
        gen_helper_sve_##name##_zpzw_s,                                   \
    };                                                                    \
    if (a->esz < 0 || a->esz >= 3) {                                      \
        return false;                                                     \
    }                                                                     \
    return do_zpzz_ool(s, a, fns[a->esz]);                                \
}

DO_ZPZW(ASR, asr)
DO_ZPZW(LSR, lsr)
DO_ZPZW(LSL, lsl)

#undef DO_ZPZW

/*
 *** SVE Bitwise Shift - Unpredicated Group
 */

static bool do_shift_imm(DisasContext *s, arg_rri_esz *a, bool asr,
                         void (*gvec_fn)(unsigned, uint32_t, uint32_t,
                                         int64_t, uint32_t, uint32_t))
{
    if (a->esz < 0) {
        /* Invalid tsz encoding -- see tszimm_esz. */
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        /* Shift by element size is architecturally valid.  For
           arithmetic right-shift, it's the same as by one less.
           Otherwise it is a zeroing operation.  */
        if (a->imm >= 8 << a->esz) {
            if (asr) {
                a->imm = (8 << a->esz) - 1;
            } else {
                do_dupi_z(s, a->rd, 0);
                return true;
            }
        }
        gvec_fn(a->esz, vec_full_reg_offset(s, a->rd),
                vec_full_reg_offset(s, a->rn), a->imm, vsz, vsz);
    }
    return true;
}

static bool trans_ASR_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    return do_shift_imm(s, a, true, tcg_gen_gvec_sari);
}

static bool trans_LSR_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    return do_shift_imm(s, a, false, tcg_gen_gvec_shri);
}

static bool trans_LSL_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    return do_shift_imm(s, a, false, tcg_gen_gvec_shli);
}

static bool do_zzw_ool(DisasContext *s, arg_rrr_esz *a, gen_helper_gvec_3 *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vsz, vsz, 0, fn);
    }
    return true;
}

#define DO_ZZW(NAME, name) \
static bool trans_##NAME##_zzw(DisasContext *s, arg_rrr_esz *a,           \
                               uint32_t insn)                             \
{                                                                         \
    static gen_helper_gvec_3 * const fns[4] = {                           \
        gen_helper_sve_##name##_zzw_b, gen_helper_sve_##name##_zzw_h,     \
        gen_helper_sve_##name##_zzw_s, NULL                               \
    };                                                                    \
    return do_zzw_ool(s, a, fns[a->esz]);                                 \
}

DO_ZZW(ASR, asr)
DO_ZZW(LSR, lsr)
DO_ZZW(LSL, lsl)

#undef DO_ZZW

/*
 *** SVE Integer Multiply-Add Group
 */

static bool do_zpzzz_ool(DisasContext *s, arg_rprrr_esz *a,
                         gen_helper_gvec_5 *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_5_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->ra),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           pred_full_reg_offset(s, a->pg),
                           vsz, vsz, 0, fn);
    }
    return true;
}

#define DO_ZPZZZ(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rprrr_esz *a, uint32_t insn) \
{                                                                    \
    static gen_helper_gvec_5 * const fns[4] = {                      \
        gen_helper_sve_##name##_b, gen_helper_sve_##name##_h,        \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d,        \
    };                                                               \
    return do_zpzzz_ool(s, a, fns[a->esz]);                          \
}

DO_ZPZZZ(MLA, mla)
DO_ZPZZZ(MLS, mls)

#undef DO_ZPZZZ

/*
 *** SVE Index Generation Group
 */

static void do_index(DisasContext *s, int esz, int rd,
                     TCGv_i64 start, TCGv_i64 incr)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_i32 desc = tcg_const_i32(simd_desc(vsz, vsz, 0));
    TCGv_ptr t_zd = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zd, cpu_env, vec_full_reg_offset(s, rd));
    if (esz == 3) {
        gen_helper_sve_index_d(t_zd, start, incr, desc);
    } else {
        typedef void index_fn(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32);
        static index_fn * const fns[3] = {
            gen_helper_sve_index_b,
            gen_helper_sve_index_h,
            gen_helper_sve_index_s,
        };
        TCGv_i32 s32 = tcg_temp_new_i32();
        TCGv_i32 i32 = tcg_temp_new_i32();

        tcg_gen_extrl_i64_i32(s32, start);
        tcg_gen_extrl_i64_i32(i32, incr);
        fns[esz](t_zd, s32, i32, desc);

        tcg_temp_free_i32(s32);
        tcg_temp_free_i32(i32);
    }
    tcg_temp_free_ptr(t_zd);
    tcg_temp_free_i32(desc);
}

static bool trans_INDEX_ii(DisasContext *s, arg_INDEX_ii *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        TCGv_i64 start = tcg_const_i64(a->imm1);
        TCGv_i64 incr = tcg_const_i64(a->imm2);
        do_index(s, a->esz, a->rd, start, incr);
        tcg_temp_free_i64(start);
        tcg_temp_free_i64(incr);
    }
    return true;
}

static bool trans_INDEX_ir(DisasContext *s, arg_INDEX_ir *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        TCGv_i64 start = tcg_const_i64(a->imm);
        TCGv_i64 incr = cpu_reg(s, a->rm);
        do_index(s, a->esz, a->rd, start, incr);
        tcg_temp_free_i64(start);
    }
    return true;
}

static bool trans_INDEX_ri(DisasContext *s, arg_INDEX_ri *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        TCGv_i64 start = cpu_reg(s, a->rn);
        TCGv_i64 incr = tcg_const_i64(a->imm);
        do_index(s, a->esz, a->rd, start, incr);
        tcg_temp_free_i64(incr);
    }
    return true;
}

static bool trans_INDEX_rr(DisasContext *s, arg_INDEX_rr *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        TCGv_i64 start = cpu_reg(s, a->rn);
        TCGv_i64 incr = cpu_reg(s, a->rm);
        do_index(s, a->esz, a->rd, start, incr);
    }
    return true;
}

/*
 *** SVE Stack Allocation Group
 */

static bool trans_ADDVL(DisasContext *s, arg_ADDVL *a, uint32_t insn)
{
    TCGv_i64 rd = cpu_reg_sp(s, a->rd);
    TCGv_i64 rn = cpu_reg_sp(s, a->rn);
    tcg_gen_addi_i64(rd, rn, a->imm * vec_full_reg_size(s));
    return true;
}

static bool trans_ADDPL(DisasContext *s, arg_ADDPL *a, uint32_t insn)
{
    TCGv_i64 rd = cpu_reg_sp(s, a->rd);
    TCGv_i64 rn = cpu_reg_sp(s, a->rn);
    tcg_gen_addi_i64(rd, rn, a->imm * pred_full_reg_size(s));
    return true;
}

static bool trans_RDVL(DisasContext *s, arg_RDVL *a, uint32_t insn)
{
    TCGv_i64 reg = cpu_reg(s, a->rd);
    tcg_gen_movi_i64(reg, a->imm * vec_full_reg_size(s));
    return true;
}

/*
 *** SVE Compute Vector Address Group
 */

static bool do_adr(DisasContext *s, arg_rrri *a, gen_helper_gvec_3 *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vsz, vsz, a->imm, fn);
    }
    return true;
}

static bool trans_ADR_p32(DisasContext *s, arg_rrri *a, uint32_t insn)
{
    return do_adr(s, a, gen_helper_sve_adr_p32);
}

static bool trans_ADR_p64(DisasContext *s, arg_rrri *a, uint32_t insn)
{
    return do_adr(s, a, gen_helper_sve_adr_p64);
}

static bool trans_ADR_s32(DisasContext *s, arg_rrri *a, uint32_t insn)
{
    return do_adr(s, a, gen_helper_sve_adr_s32);
}

static bool trans_ADR_u32(DisasContext *s, arg_rrri *a, uint32_t insn)
{
    return do_adr(s, a, gen_helper_sve_adr_u32);
}

/*
 *** SVE Integer Misc - Unpredicated Group
 */

static bool trans_FEXPA(DisasContext *s, arg_rr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_2 * const fns[4] = {
        NULL,
        gen_helper_sve_fexpa_h,
        gen_helper_sve_fexpa_s,
        gen_helper_sve_fexpa_d,
    };
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vsz, vsz, 0, fns[a->esz]);
    }
    return true;
}

static bool trans_FTSSEL(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_ftssel_h,
        gen_helper_sve_ftssel_s,
        gen_helper_sve_ftssel_d,
    };
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vsz, vsz, 0, fns[a->esz]);
    }
    return true;
}

/*
 *** SVE Predicate Logical Operations Group
 */

static bool do_pppp_flags(DisasContext *s, arg_rprr_s *a,
                          const GVecGen4 *gvec_op)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned psz = pred_gvec_reg_size(s);
    int dofs = pred_full_reg_offset(s, a->rd);
    int nofs = pred_full_reg_offset(s, a->rn);
    int mofs = pred_full_reg_offset(s, a->rm);
    int gofs = pred_full_reg_offset(s, a->pg);

    if (psz == 8) {
        /* Do the operation and the flags generation in temps.  */
        TCGv_i64 pd = tcg_temp_new_i64();
        TCGv_i64 pn = tcg_temp_new_i64();
        TCGv_i64 pm = tcg_temp_new_i64();
        TCGv_i64 pg = tcg_temp_new_i64();

        tcg_gen_ld_i64(pn, cpu_env, nofs);
        tcg_gen_ld_i64(pm, cpu_env, mofs);
        tcg_gen_ld_i64(pg, cpu_env, gofs);

        gvec_op->fni8(pd, pn, pm, pg);
        tcg_gen_st_i64(pd, cpu_env, dofs);

        do_predtest1(pd, pg);

        tcg_temp_free_i64(pd);
        tcg_temp_free_i64(pn);
        tcg_temp_free_i64(pm);
        tcg_temp_free_i64(pg);
    } else {
        /* The operation and flags generation is large.  The computation
         * of the flags depends on the original contents of the guarding
         * predicate.  If the destination overwrites the guarding predicate,
         * then the easiest way to get this right is to save a copy.
          */
        int tofs = gofs;
        if (a->rd == a->pg) {
            tofs = offsetof(CPUARMState, vfp.preg_tmp);
            tcg_gen_gvec_mov(0, tofs, gofs, psz, psz);
        }

        tcg_gen_gvec_4(dofs, nofs, mofs, gofs, psz, psz, gvec_op);
        do_predtest(s, dofs, tofs, psz / 8);
    }
    return true;
}

static void gen_and_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_and_i64(pd, pn, pm);
    tcg_gen_and_i64(pd, pd, pg);
}

static void gen_and_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_and_vec(vece, pd, pn, pm);
    tcg_gen_and_vec(vece, pd, pd, pg);
}

static bool trans_AND_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_and_pg_i64,
        .fniv = gen_and_pg_vec,
        .fno = gen_helper_sve_and_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else if (a->rn == a->rm) {
        if (a->pg == a->rn) {
            return do_mov_p(s, a->rd, a->rn);
        } else {
            return do_vector3_p(s, tcg_gen_gvec_and, 0, a->rd, a->rn, a->pg);
        }
    } else if (a->pg == a->rn || a->pg == a->rm) {
        return do_vector3_p(s, tcg_gen_gvec_and, 0, a->rd, a->rn, a->rm);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
}

static void gen_bic_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_andc_i64(pd, pn, pm);
    tcg_gen_and_i64(pd, pd, pg);
}

static void gen_bic_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_andc_vec(vece, pd, pn, pm);
    tcg_gen_and_vec(vece, pd, pd, pg);
}

static bool trans_BIC_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_bic_pg_i64,
        .fniv = gen_bic_pg_vec,
        .fno = gen_helper_sve_bic_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else if (a->pg == a->rn) {
        return do_vector3_p(s, tcg_gen_gvec_andc, 0, a->rd, a->rn, a->rm);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
}

static void gen_eor_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_xor_i64(pd, pn, pm);
    tcg_gen_and_i64(pd, pd, pg);
}

static void gen_eor_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_xor_vec(vece, pd, pn, pm);
    tcg_gen_and_vec(vece, pd, pd, pg);
}

static bool trans_EOR_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_eor_pg_i64,
        .fniv = gen_eor_pg_vec,
        .fno = gen_helper_sve_eor_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
}

static void gen_sel_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_and_i64(pn, pn, pg);
    tcg_gen_andc_i64(pm, pm, pg);
    tcg_gen_or_i64(pd, pn, pm);
}

static void gen_sel_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_and_vec(vece, pn, pn, pg);
    tcg_gen_andc_vec(vece, pm, pm, pg);
    tcg_gen_or_vec(vece, pd, pn, pm);
}

static bool trans_SEL_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_sel_pg_i64,
        .fniv = gen_sel_pg_vec,
        .fno = gen_helper_sve_sel_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return false;
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
}

static void gen_orr_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_or_i64(pd, pn, pm);
    tcg_gen_and_i64(pd, pd, pg);
}

static void gen_orr_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_or_vec(vece, pd, pn, pm);
    tcg_gen_and_vec(vece, pd, pd, pg);
}

static bool trans_ORR_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_orr_pg_i64,
        .fniv = gen_orr_pg_vec,
        .fno = gen_helper_sve_orr_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else if (a->pg == a->rn && a->rn == a->rm) {
        return do_mov_p(s, a->rd, a->rn);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
}

static void gen_orn_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_orc_i64(pd, pn, pm);
    tcg_gen_and_i64(pd, pd, pg);
}

static void gen_orn_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_orc_vec(vece, pd, pn, pm);
    tcg_gen_and_vec(vece, pd, pd, pg);
}

static bool trans_ORN_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_orn_pg_i64,
        .fniv = gen_orn_pg_vec,
        .fno = gen_helper_sve_orn_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
}

static void gen_nor_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_or_i64(pd, pn, pm);
    tcg_gen_andc_i64(pd, pg, pd);
}

static void gen_nor_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_or_vec(vece, pd, pn, pm);
    tcg_gen_andc_vec(vece, pd, pg, pd);
}

static bool trans_NOR_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_nor_pg_i64,
        .fniv = gen_nor_pg_vec,
        .fno = gen_helper_sve_nor_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
}

static void gen_nand_pg_i64(TCGv_i64 pd, TCGv_i64 pn, TCGv_i64 pm, TCGv_i64 pg)
{
    tcg_gen_and_i64(pd, pn, pm);
    tcg_gen_andc_i64(pd, pg, pd);
}

static void gen_nand_pg_vec(unsigned vece, TCGv_vec pd, TCGv_vec pn,
                           TCGv_vec pm, TCGv_vec pg)
{
    tcg_gen_and_vec(vece, pd, pn, pm);
    tcg_gen_andc_vec(vece, pd, pg, pd);
}

static bool trans_NAND_pppp(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    static const GVecGen4 op = {
        .fni8 = gen_nand_pg_i64,
        .fniv = gen_nand_pg_vec,
        .fno = gen_helper_sve_nand_pppp,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64,
    };
    if (a->s) {
        return do_pppp_flags(s, a, &op);
    } else {
        return do_vecop4_p(s, &op, a->rd, a->rn, a->rm, a->pg);
    }
}

/*
 *** SVE Predicate Misc Group
 */

static bool trans_PTEST(DisasContext *s, arg_PTEST *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        int nofs = pred_full_reg_offset(s, a->rn);
        int gofs = pred_full_reg_offset(s, a->pg);
        int words = DIV_ROUND_UP(pred_full_reg_size(s), 8);

        if (words == 1) {
            TCGv_i64 pn = tcg_temp_new_i64();
            TCGv_i64 pg = tcg_temp_new_i64();

            tcg_gen_ld_i64(pn, cpu_env, nofs);
            tcg_gen_ld_i64(pg, cpu_env, gofs);
            do_predtest1(pn, pg);

            tcg_temp_free_i64(pn);
            tcg_temp_free_i64(pg);
        } else {
            do_predtest(s, nofs, gofs, words);
        }
    }
    return true;
}

/* See the ARM pseudocode DecodePredCount.  */
static unsigned decode_pred_count(unsigned fullsz, int pattern, int esz)
{
    unsigned elements = fullsz >> esz;
    unsigned bound;

    switch (pattern) {
    case 0x0: /* POW2 */
        return pow2floor(elements);
    case 0x1: /* VL1 */
    case 0x2: /* VL2 */
    case 0x3: /* VL3 */
    case 0x4: /* VL4 */
    case 0x5: /* VL5 */
    case 0x6: /* VL6 */
    case 0x7: /* VL7 */
    case 0x8: /* VL8 */
        bound = pattern;
        break;
    case 0x9: /* VL16 */
    case 0xa: /* VL32 */
    case 0xb: /* VL64 */
    case 0xc: /* VL128 */
    case 0xd: /* VL256 */
        bound = 16 << (pattern - 9);
        break;
    case 0x1d: /* MUL4 */
        return elements - elements % 4;
    case 0x1e: /* MUL3 */
        return elements - elements % 3;
    case 0x1f: /* ALL */
        return elements;
    default:   /* #uimm5 */
        return 0;
    }
    return elements >= bound ? bound : 0;
}

/* This handles all of the predicate initialization instructions,
 * PTRUE, PFALSE, SETFFR.  For PFALSE, we will have set PAT == 32
 * so that decode_pred_count returns 0.  For SETFFR, we will have
 * set RD == 16 == FFR.
 */
static bool do_predset(DisasContext *s, int esz, int rd, int pat, bool setflag)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned ofs = pred_full_reg_offset(s, rd);
    unsigned numelem, setsz, i;
    uint64_t word, lastword;
    TCGv_i64 t;

    numelem = decode_pred_count(fullsz, pat, esz);

    /* Determine what we must store into each bit, and how many.  */
    if (numelem == 0) {
        lastword = word = 0;
        setsz = fullsz;
    } else {
        setsz = numelem << esz;
        lastword = word = pred_esz_masks[esz];
        if (setsz % 64) {
            lastword &= ~(-1ull << (setsz % 64));
        }
    }

    t = tcg_temp_new_i64();
    if (fullsz <= 64) {
        tcg_gen_movi_i64(t, lastword);
        tcg_gen_st_i64(t, cpu_env, ofs);
        goto done;
    }

    if (word == lastword) {
        unsigned maxsz = size_for_gvec(fullsz / 8);
        unsigned oprsz = size_for_gvec(setsz / 8);

        if (oprsz * 8 == setsz) {
            tcg_gen_gvec_dup64i(ofs, oprsz, maxsz, word);
            goto done;
        }
        if (oprsz * 8 == setsz + 8) {
            tcg_gen_gvec_dup64i(ofs, oprsz, maxsz, word);
            tcg_gen_movi_i64(t, 0);
            tcg_gen_st_i64(t, cpu_env, ofs + oprsz - 8);
            goto done;
        }
    }

    setsz /= 8;
    fullsz /= 8;

    tcg_gen_movi_i64(t, word);
    for (i = 0; i < setsz; i += 8) {
        tcg_gen_st_i64(t, cpu_env, ofs + i);
    }
    if (lastword != word) {
        tcg_gen_movi_i64(t, lastword);
        tcg_gen_st_i64(t, cpu_env, ofs + i);
        i += 8;
    }
    if (i < fullsz) {
        tcg_gen_movi_i64(t, 0);
        for (; i < fullsz; i += 8) {
            tcg_gen_st_i64(t, cpu_env, ofs + i);
        }
    }

 done:
    tcg_temp_free_i64(t);

    /* PTRUES */
    if (setflag) {
        tcg_gen_movi_i32(cpu_NF, -(word != 0));
        tcg_gen_movi_i32(cpu_CF, word == 0);
        tcg_gen_movi_i32(cpu_VF, 0);
        tcg_gen_mov_i32(cpu_ZF, cpu_NF);
    }
    return true;
}

static bool trans_PTRUE(DisasContext *s, arg_PTRUE *a, uint32_t insn)
{
    return do_predset(s, a->esz, a->rd, a->pat, a->s);
}

static bool trans_SETFFR(DisasContext *s, arg_SETFFR *a, uint32_t insn)
{
    /* Note pat == 31 is #all, to set all elements.  */
    return do_predset(s, 0, FFR_PRED_NUM, 31, false);
}

static bool trans_PFALSE(DisasContext *s, arg_PFALSE *a, uint32_t insn)
{
    /* Note pat == 32 is #unimp, to set no elements.  */
    return do_predset(s, 0, a->rd, 32, false);
}

static bool trans_RDFFR_p(DisasContext *s, arg_RDFFR_p *a, uint32_t insn)
{
    /* The path through do_pppp_flags is complicated enough to want to avoid
     * duplication.  Frob the arguments into the form of a predicated AND.
     */
    arg_rprr_s alt_a = {
        .rd = a->rd, .pg = a->pg, .s = a->s,
        .rn = FFR_PRED_NUM, .rm = FFR_PRED_NUM,
    };
    return trans_AND_pppp(s, &alt_a, insn);
}

static bool trans_RDFFR(DisasContext *s, arg_RDFFR *a, uint32_t insn)
{
    return do_mov_p(s, a->rd, FFR_PRED_NUM);
}

static bool trans_WRFFR(DisasContext *s, arg_WRFFR *a, uint32_t insn)
{
    return do_mov_p(s, FFR_PRED_NUM, a->rn);
}

static bool do_pfirst_pnext(DisasContext *s, arg_rr_esz *a,
                            void (*gen_fn)(TCGv_i32, TCGv_ptr,
                                           TCGv_ptr, TCGv_i32))
{
    if (!sve_access_check(s)) {
        return true;
    }

    TCGv_ptr t_pd = tcg_temp_new_ptr();
    TCGv_ptr t_pg = tcg_temp_new_ptr();
    TCGv_i32 t;
    unsigned desc;

    desc = DIV_ROUND_UP(pred_full_reg_size(s), 8);
    desc = deposit32(desc, SIMD_DATA_SHIFT, 2, a->esz);

    tcg_gen_addi_ptr(t_pd, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, a->rn));
    t = tcg_const_i32(desc);

    gen_fn(t, t_pd, t_pg, t);
    tcg_temp_free_ptr(t_pd);
    tcg_temp_free_ptr(t_pg);

    do_pred_flags(t);
    tcg_temp_free_i32(t);
    return true;
}

static bool trans_PFIRST(DisasContext *s, arg_rr_esz *a, uint32_t insn)
{
    return do_pfirst_pnext(s, a, gen_helper_sve_pfirst);
}

static bool trans_PNEXT(DisasContext *s, arg_rr_esz *a, uint32_t insn)
{
    return do_pfirst_pnext(s, a, gen_helper_sve_pnext);
}

/*
 *** SVE Element Count Group
 */

/* Perform an inline saturating addition of a 32-bit value within
 * a 64-bit register.  The second operand is known to be positive,
 * which halves the comparisions we must perform to bound the result.
 */
static void do_sat_addsub_32(TCGv_i64 reg, TCGv_i64 val, bool u, bool d)
{
    int64_t ibound;
    TCGv_i64 bound;
    TCGCond cond;

    /* Use normal 64-bit arithmetic to detect 32-bit overflow.  */
    if (u) {
        tcg_gen_ext32u_i64(reg, reg);
    } else {
        tcg_gen_ext32s_i64(reg, reg);
    }
    if (d) {
        tcg_gen_sub_i64(reg, reg, val);
        ibound = (u ? 0 : INT32_MIN);
        cond = TCG_COND_LT;
    } else {
        tcg_gen_add_i64(reg, reg, val);
        ibound = (u ? UINT32_MAX : INT32_MAX);
        cond = TCG_COND_GT;
    }
    bound = tcg_const_i64(ibound);
    tcg_gen_movcond_i64(cond, reg, reg, bound, bound, reg);
    tcg_temp_free_i64(bound);
}

/* Similarly with 64-bit values.  */
static void do_sat_addsub_64(TCGv_i64 reg, TCGv_i64 val, bool u, bool d)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2;

    if (u) {
        if (d) {
            tcg_gen_sub_i64(t0, reg, val);
            tcg_gen_movi_i64(t1, 0);
            tcg_gen_movcond_i64(TCG_COND_LTU, reg, reg, val, t1, t0);
        } else {
            tcg_gen_add_i64(t0, reg, val);
            tcg_gen_movi_i64(t1, -1);
            tcg_gen_movcond_i64(TCG_COND_LTU, reg, t0, reg, t1, t0);
        }
    } else {
        if (d) {
            /* Detect signed overflow for subtraction.  */
            tcg_gen_xor_i64(t0, reg, val);
            tcg_gen_sub_i64(t1, reg, val);
            tcg_gen_xor_i64(reg, reg, t0);
            tcg_gen_and_i64(t0, t0, reg);

            /* Bound the result.  */
            tcg_gen_movi_i64(reg, INT64_MIN);
            t2 = tcg_const_i64(0);
            tcg_gen_movcond_i64(TCG_COND_LT, reg, t0, t2, reg, t1);
        } else {
            /* Detect signed overflow for addition.  */
            tcg_gen_xor_i64(t0, reg, val);
            tcg_gen_add_i64(reg, reg, val);
            tcg_gen_xor_i64(t1, reg, val);
            tcg_gen_andc_i64(t0, t1, t0);

            /* Bound the result.  */
            tcg_gen_movi_i64(t1, INT64_MAX);
            t2 = tcg_const_i64(0);
            tcg_gen_movcond_i64(TCG_COND_LT, reg, t0, t2, t1, reg);
        }
        tcg_temp_free_i64(t2);
    }
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

/* Similarly with a vector and a scalar operand.  */
static void do_sat_addsub_vec(DisasContext *s, int esz, int rd, int rn,
                              TCGv_i64 val, bool u, bool d)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr dptr, nptr;
    TCGv_i32 t32, desc;
    TCGv_i64 t64;

    dptr = tcg_temp_new_ptr();
    nptr = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(dptr, cpu_env, vec_full_reg_offset(s, rd));
    tcg_gen_addi_ptr(nptr, cpu_env, vec_full_reg_offset(s, rn));
    desc = tcg_const_i32(simd_desc(vsz, vsz, 0));

    switch (esz) {
    case MO_8:
        t32 = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(t32, val);
        if (d) {
            tcg_gen_neg_i32(t32, t32);
        }
        if (u) {
            gen_helper_sve_uqaddi_b(dptr, nptr, t32, desc);
        } else {
            gen_helper_sve_sqaddi_b(dptr, nptr, t32, desc);
        }
        tcg_temp_free_i32(t32);
        break;

    case MO_16:
        t32 = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(t32, val);
        if (d) {
            tcg_gen_neg_i32(t32, t32);
        }
        if (u) {
            gen_helper_sve_uqaddi_h(dptr, nptr, t32, desc);
        } else {
            gen_helper_sve_sqaddi_h(dptr, nptr, t32, desc);
        }
        tcg_temp_free_i32(t32);
        break;

    case MO_32:
        t64 = tcg_temp_new_i64();
        if (d) {
            tcg_gen_neg_i64(t64, val);
        } else {
            tcg_gen_mov_i64(t64, val);
        }
        if (u) {
            gen_helper_sve_uqaddi_s(dptr, nptr, t64, desc);
        } else {
            gen_helper_sve_sqaddi_s(dptr, nptr, t64, desc);
        }
        tcg_temp_free_i64(t64);
        break;

    case MO_64:
        if (u) {
            if (d) {
                gen_helper_sve_uqsubi_d(dptr, nptr, val, desc);
            } else {
                gen_helper_sve_uqaddi_d(dptr, nptr, val, desc);
            }
        } else if (d) {
            t64 = tcg_temp_new_i64();
            tcg_gen_neg_i64(t64, val);
            gen_helper_sve_sqaddi_d(dptr, nptr, t64, desc);
            tcg_temp_free_i64(t64);
        } else {
            gen_helper_sve_sqaddi_d(dptr, nptr, val, desc);
        }
        break;

    default:
        g_assert_not_reached();
    }

    tcg_temp_free_ptr(dptr);
    tcg_temp_free_ptr(nptr);
    tcg_temp_free_i32(desc);
}

static bool trans_CNT_r(DisasContext *s, arg_CNT_r *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        unsigned fullsz = vec_full_reg_size(s);
        unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
        tcg_gen_movi_i64(cpu_reg(s, a->rd), numelem * a->imm);
    }
    return true;
}

static bool trans_INCDEC_r(DisasContext *s, arg_incdec_cnt *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        unsigned fullsz = vec_full_reg_size(s);
        unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
        int inc = numelem * a->imm * (a->d ? -1 : 1);
        TCGv_i64 reg = cpu_reg(s, a->rd);

        tcg_gen_addi_i64(reg, reg, inc);
    }
    return true;
}

static bool trans_SINCDEC_r_32(DisasContext *s, arg_incdec_cnt *a,
                               uint32_t insn)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;
    TCGv_i64 reg = cpu_reg(s, a->rd);

    /* Use normal 64-bit arithmetic to detect 32-bit overflow.  */
    if (inc == 0) {
        if (a->u) {
            tcg_gen_ext32u_i64(reg, reg);
        } else {
            tcg_gen_ext32s_i64(reg, reg);
        }
    } else {
        TCGv_i64 t = tcg_const_i64(inc);
        do_sat_addsub_32(reg, t, a->u, a->d);
        tcg_temp_free_i64(t);
    }
    return true;
}

static bool trans_SINCDEC_r_64(DisasContext *s, arg_incdec_cnt *a,
                               uint32_t insn)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;
    TCGv_i64 reg = cpu_reg(s, a->rd);

    if (inc != 0) {
        TCGv_i64 t = tcg_const_i64(inc);
        do_sat_addsub_64(reg, t, a->u, a->d);
        tcg_temp_free_i64(t);
    }
    return true;
}

static bool trans_INCDEC_v(DisasContext *s, arg_incdec2_cnt *a, uint32_t insn)
{
    if (a->esz == 0) {
        return false;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;

    if (inc != 0) {
        if (sve_access_check(s)) {
            TCGv_i64 t = tcg_const_i64(a->d ? -inc : inc);
            tcg_gen_gvec_adds(a->esz, vec_full_reg_offset(s, a->rd),
                              vec_full_reg_offset(s, a->rn),
                              t, fullsz, fullsz);
            tcg_temp_free_i64(t);
        }
    } else {
        do_mov_z(s, a->rd, a->rn);
    }
    return true;
}

static bool trans_SINCDEC_v(DisasContext *s, arg_incdec2_cnt *a,
                            uint32_t insn)
{
    if (a->esz == 0) {
        return false;
    }

    unsigned fullsz = vec_full_reg_size(s);
    unsigned numelem = decode_pred_count(fullsz, a->pat, a->esz);
    int inc = numelem * a->imm;

    if (inc != 0) {
        if (sve_access_check(s)) {
            TCGv_i64 t = tcg_const_i64(inc);
            do_sat_addsub_vec(s, a->esz, a->rd, a->rn, t, a->u, a->d);
            tcg_temp_free_i64(t);
        }
    } else {
        do_mov_z(s, a->rd, a->rn);
    }
    return true;
}

/*
 *** SVE Bitwise Immediate Group
 */

static bool do_zz_dbm(DisasContext *s, arg_rr_dbm *a, GVecGen2iFn *gvec_fn)
{
    uint64_t imm;
    if (!logic_imm_decode_wmask(&imm, extract32(a->dbm, 12, 1),
                                extract32(a->dbm, 0, 6),
                                extract32(a->dbm, 6, 6))) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        gvec_fn(MO_64, vec_full_reg_offset(s, a->rd),
                vec_full_reg_offset(s, a->rn), imm, vsz, vsz);
    }
    return true;
}

static bool trans_AND_zzi(DisasContext *s, arg_rr_dbm *a, uint32_t insn)
{
    return do_zz_dbm(s, a, tcg_gen_gvec_andi);
}

static bool trans_ORR_zzi(DisasContext *s, arg_rr_dbm *a, uint32_t insn)
{
    return do_zz_dbm(s, a, tcg_gen_gvec_ori);
}

static bool trans_EOR_zzi(DisasContext *s, arg_rr_dbm *a, uint32_t insn)
{
    return do_zz_dbm(s, a, tcg_gen_gvec_xori);
}

static bool trans_DUPM(DisasContext *s, arg_DUPM *a, uint32_t insn)
{
    uint64_t imm;
    if (!logic_imm_decode_wmask(&imm, extract32(a->dbm, 12, 1),
                                extract32(a->dbm, 0, 6),
                                extract32(a->dbm, 6, 6))) {
        return false;
    }
    if (sve_access_check(s)) {
        do_dupi_z(s, a->rd, imm);
    }
    return true;
}

/*
 *** SVE Integer Wide Immediate - Predicated Group
 */

/* Implement all merging copies.  This is used for CPY (immediate),
 * FCPY, CPY (scalar), CPY (SIMD&FP scalar).
 */
static void do_cpy_m(DisasContext *s, int esz, int rd, int rn, int pg,
                     TCGv_i64 val)
{
    typedef void gen_cpy(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv_i32);
    static gen_cpy * const fns[4] = {
        gen_helper_sve_cpy_m_b, gen_helper_sve_cpy_m_h,
        gen_helper_sve_cpy_m_s, gen_helper_sve_cpy_m_d,
    };
    unsigned vsz = vec_full_reg_size(s);
    TCGv_i32 desc = tcg_const_i32(simd_desc(vsz, vsz, 0));
    TCGv_ptr t_zd = tcg_temp_new_ptr();
    TCGv_ptr t_zn = tcg_temp_new_ptr();
    TCGv_ptr t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zd, cpu_env, vec_full_reg_offset(s, rd));
    tcg_gen_addi_ptr(t_zn, cpu_env, vec_full_reg_offset(s, rn));
    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, pg));

    fns[esz](t_zd, t_zn, t_pg, val, desc);

    tcg_temp_free_ptr(t_zd);
    tcg_temp_free_ptr(t_zn);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_i32(desc);
}

static bool trans_FCPY(DisasContext *s, arg_FCPY *a, uint32_t insn)
{
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        /* Decode the VFP immediate.  */
        uint64_t imm = vfp_expand_imm(a->esz, a->imm);
        TCGv_i64 t_imm = tcg_const_i64(imm);
        do_cpy_m(s, a->esz, a->rd, a->rn, a->pg, t_imm);
        tcg_temp_free_i64(t_imm);
    }
    return true;
}

static bool trans_CPY_m_i(DisasContext *s, arg_rpri_esz *a, uint32_t insn)
{
    if (a->esz == 0 && extract32(insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 t_imm = tcg_const_i64(a->imm);
        do_cpy_m(s, a->esz, a->rd, a->rn, a->pg, t_imm);
        tcg_temp_free_i64(t_imm);
    }
    return true;
}

static bool trans_CPY_z_i(DisasContext *s, arg_CPY_z_i *a, uint32_t insn)
{
    static gen_helper_gvec_2i * const fns[4] = {
        gen_helper_sve_cpy_z_b, gen_helper_sve_cpy_z_h,
        gen_helper_sve_cpy_z_s, gen_helper_sve_cpy_z_d,
    };

    if (a->esz == 0 && extract32(insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_i64 t_imm = tcg_const_i64(a->imm);
        tcg_gen_gvec_2i_ool(vec_full_reg_offset(s, a->rd),
                            pred_full_reg_offset(s, a->pg),
                            t_imm, vsz, vsz, 0, fns[a->esz]);
        tcg_temp_free_i64(t_imm);
    }
    return true;
}

/*
 *** SVE Permute Extract Group
 */

static bool trans_EXT(DisasContext *s, arg_EXT *a, uint32_t insn)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = vec_full_reg_size(s);
    unsigned n_ofs = a->imm >= vsz ? 0 : a->imm;
    unsigned n_siz = vsz - n_ofs;
    unsigned d = vec_full_reg_offset(s, a->rd);
    unsigned n = vec_full_reg_offset(s, a->rn);
    unsigned m = vec_full_reg_offset(s, a->rm);

    /* Use host vector move insns if we have appropriate sizes
     * and no unfortunate overlap.
     */
    if (m != d
        && n_ofs == size_for_gvec(n_ofs)
        && n_siz == size_for_gvec(n_siz)
        && (d != n || n_siz <= n_ofs)) {
        tcg_gen_gvec_mov(0, d, n + n_ofs, n_siz, n_siz);
        if (n_ofs != 0) {
            tcg_gen_gvec_mov(0, d + n_siz, m, n_ofs, n_ofs);
        }
    } else {
        tcg_gen_gvec_3_ool(d, n, m, vsz, vsz, n_ofs, gen_helper_sve_ext);
    }
    return true;
}

/*
 *** SVE Permute - Unpredicated Group
 */

static bool trans_DUP_s(DisasContext *s, arg_DUP_s *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_dup_i64(a->esz, vec_full_reg_offset(s, a->rd),
                             vsz, vsz, cpu_reg_sp(s, a->rn));
    }
    return true;
}

static bool trans_DUP_x(DisasContext *s, arg_DUP_x *a, uint32_t insn)
{
    if ((a->imm & 0x1f) == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        unsigned dofs = vec_full_reg_offset(s, a->rd);
        unsigned esz, index;

        esz = ctz32(a->imm);
        index = a->imm >> (esz + 1);

        if ((index << esz) < vsz) {
            unsigned nofs = vec_reg_offset(s, a->rn, index, esz);
            tcg_gen_gvec_dup_mem(esz, dofs, nofs, vsz, vsz);
        } else {
            tcg_gen_gvec_dup64i(dofs, vsz, vsz, 0);
        }
    }
    return true;
}

static void do_insr_i64(DisasContext *s, arg_rrr_esz *a, TCGv_i64 val)
{
    typedef void gen_insr(TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv_i32);
    static gen_insr * const fns[4] = {
        gen_helper_sve_insr_b, gen_helper_sve_insr_h,
        gen_helper_sve_insr_s, gen_helper_sve_insr_d,
    };
    unsigned vsz = vec_full_reg_size(s);
    TCGv_i32 desc = tcg_const_i32(simd_desc(vsz, vsz, 0));
    TCGv_ptr t_zd = tcg_temp_new_ptr();
    TCGv_ptr t_zn = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_zd, cpu_env, vec_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_zn, cpu_env, vec_full_reg_offset(s, a->rn));

    fns[a->esz](t_zd, t_zn, val, desc);

    tcg_temp_free_ptr(t_zd);
    tcg_temp_free_ptr(t_zn);
    tcg_temp_free_i32(desc);
}

static bool trans_INSR_f(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_ld_i64(t, cpu_env, vec_reg_offset(s, a->rm, 0, MO_64));
        do_insr_i64(s, a, t);
        tcg_temp_free_i64(t);
    }
    return true;
}

static bool trans_INSR_r(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        do_insr_i64(s, a, cpu_reg(s, a->rm));
    }
    return true;
}

static bool trans_REV_v(DisasContext *s, arg_rr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_2 * const fns[4] = {
        gen_helper_sve_rev_b, gen_helper_sve_rev_h,
        gen_helper_sve_rev_s, gen_helper_sve_rev_d
    };

    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vsz, vsz, 0, fns[a->esz]);
    }
    return true;
}

static bool trans_TBL(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_tbl_b, gen_helper_sve_tbl_h,
        gen_helper_sve_tbl_s, gen_helper_sve_tbl_d
    };

    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vsz, vsz, 0, fns[a->esz]);
    }
    return true;
}

static bool trans_UNPK(DisasContext *s, arg_UNPK *a, uint32_t insn)
{
    static gen_helper_gvec_2 * const fns[4][2] = {
        { NULL, NULL },
        { gen_helper_sve_sunpk_h, gen_helper_sve_uunpk_h },
        { gen_helper_sve_sunpk_s, gen_helper_sve_uunpk_s },
        { gen_helper_sve_sunpk_d, gen_helper_sve_uunpk_d },
    };

    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_2_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn)
                           + (a->h ? vsz / 2 : 0),
                           vsz, vsz, 0, fns[a->esz][a->u]);
    }
    return true;
}

/*
 *** SVE Permute - Predicates Group
 */

static bool do_perm_pred3(DisasContext *s, arg_rrr_esz *a, bool high_odd,
                          gen_helper_gvec_3 *fn)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = pred_full_reg_size(s);

    /* Predicate sizes may be smaller and cannot use simd_desc.
       We cannot round up, as we do elsewhere, because we need
       the exact size for ZIP2 and REV.  We retain the style for
       the other helpers for consistency.  */
    TCGv_ptr t_d = tcg_temp_new_ptr();
    TCGv_ptr t_n = tcg_temp_new_ptr();
    TCGv_ptr t_m = tcg_temp_new_ptr();
    TCGv_i32 t_desc;
    int desc;

    desc = vsz - 2;
    desc = deposit32(desc, SIMD_DATA_SHIFT, 2, a->esz);
    desc = deposit32(desc, SIMD_DATA_SHIFT + 2, 2, high_odd);

    tcg_gen_addi_ptr(t_d, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_n, cpu_env, pred_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(t_m, cpu_env, pred_full_reg_offset(s, a->rm));
    t_desc = tcg_const_i32(desc);

    fn(t_d, t_n, t_m, t_desc);

    tcg_temp_free_ptr(t_d);
    tcg_temp_free_ptr(t_n);
    tcg_temp_free_ptr(t_m);
    tcg_temp_free_i32(t_desc);
    return true;
}

static bool do_perm_pred2(DisasContext *s, arg_rr_esz *a, bool high_odd,
                          gen_helper_gvec_2 *fn)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = pred_full_reg_size(s);
    TCGv_ptr t_d = tcg_temp_new_ptr();
    TCGv_ptr t_n = tcg_temp_new_ptr();
    TCGv_i32 t_desc;
    int desc;

    tcg_gen_addi_ptr(t_d, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(t_n, cpu_env, pred_full_reg_offset(s, a->rn));

    /* Predicate sizes may be smaller and cannot use simd_desc.
       We cannot round up, as we do elsewhere, because we need
       the exact size for ZIP2 and REV.  We retain the style for
       the other helpers for consistency.  */

    desc = vsz - 2;
    desc = deposit32(desc, SIMD_DATA_SHIFT, 2, a->esz);
    desc = deposit32(desc, SIMD_DATA_SHIFT + 2, 2, high_odd);
    t_desc = tcg_const_i32(desc);

    fn(t_d, t_n, t_desc);

    tcg_temp_free_i32(t_desc);
    tcg_temp_free_ptr(t_d);
    tcg_temp_free_ptr(t_n);
    return true;
}

static bool trans_ZIP1_p(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_perm_pred3(s, a, 0, gen_helper_sve_zip_p);
}

static bool trans_ZIP2_p(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_perm_pred3(s, a, 1, gen_helper_sve_zip_p);
}

static bool trans_UZP1_p(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_perm_pred3(s, a, 0, gen_helper_sve_uzp_p);
}

static bool trans_UZP2_p(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_perm_pred3(s, a, 1, gen_helper_sve_uzp_p);
}

static bool trans_TRN1_p(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_perm_pred3(s, a, 0, gen_helper_sve_trn_p);
}

static bool trans_TRN2_p(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_perm_pred3(s, a, 1, gen_helper_sve_trn_p);
}

static bool trans_REV_p(DisasContext *s, arg_rr_esz *a, uint32_t insn)
{
    return do_perm_pred2(s, a, 0, gen_helper_sve_rev_p);
}

static bool trans_PUNPKLO(DisasContext *s, arg_PUNPKLO *a, uint32_t insn)
{
    return do_perm_pred2(s, a, 0, gen_helper_sve_punpk_p);
}

static bool trans_PUNPKHI(DisasContext *s, arg_PUNPKHI *a, uint32_t insn)
{
    return do_perm_pred2(s, a, 1, gen_helper_sve_punpk_p);
}

/*
 *** SVE Permute - Interleaving Group
 */

static bool do_zip(DisasContext *s, arg_rrr_esz *a, bool high)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_zip_b, gen_helper_sve_zip_h,
        gen_helper_sve_zip_s, gen_helper_sve_zip_d,
    };

    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        unsigned high_ofs = high ? vsz / 2 : 0;
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn) + high_ofs,
                           vec_full_reg_offset(s, a->rm) + high_ofs,
                           vsz, vsz, 0, fns[a->esz]);
    }
    return true;
}

static bool do_zzz_data_ool(DisasContext *s, arg_rrr_esz *a, int data,
                            gen_helper_gvec_3 *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_3_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           vsz, vsz, data, fn);
    }
    return true;
}

static bool trans_ZIP1_z(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_zip(s, a, false);
}

static bool trans_ZIP2_z(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_zip(s, a, true);
}

static gen_helper_gvec_3 * const uzp_fns[4] = {
    gen_helper_sve_uzp_b, gen_helper_sve_uzp_h,
    gen_helper_sve_uzp_s, gen_helper_sve_uzp_d,
};

static bool trans_UZP1_z(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_zzz_data_ool(s, a, 0, uzp_fns[a->esz]);
}

static bool trans_UZP2_z(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_zzz_data_ool(s, a, 1 << a->esz, uzp_fns[a->esz]);
}

static gen_helper_gvec_3 * const trn_fns[4] = {
    gen_helper_sve_trn_b, gen_helper_sve_trn_h,
    gen_helper_sve_trn_s, gen_helper_sve_trn_d,
};

static bool trans_TRN1_z(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_zzz_data_ool(s, a, 0, trn_fns[a->esz]);
}

static bool trans_TRN2_z(DisasContext *s, arg_rrr_esz *a, uint32_t insn)
{
    return do_zzz_data_ool(s, a, 1 << a->esz, trn_fns[a->esz]);
}

/*
 *** SVE Permute Vector - Predicated Group
 */

static bool trans_COMPACT(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL, NULL, gen_helper_sve_compact_s, gen_helper_sve_compact_d
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

/* Call the helper that computes the ARM LastActiveElement pseudocode
 * function, scaled by the element size.  This includes the not found
 * indication; e.g. not found for esz=3 is -8.
 */
static void find_last_active(DisasContext *s, TCGv_i32 ret, int esz, int pg)
{
    /* Predicate sizes may be smaller and cannot use simd_desc.  We cannot
     * round up, as we do elsewhere, because we need the exact size.
     */
    TCGv_ptr t_p = tcg_temp_new_ptr();
    TCGv_i32 t_desc;
    unsigned vsz = pred_full_reg_size(s);
    unsigned desc;

    desc = vsz - 2;
    desc = deposit32(desc, SIMD_DATA_SHIFT, 2, esz);

    tcg_gen_addi_ptr(t_p, cpu_env, pred_full_reg_offset(s, pg));
    t_desc = tcg_const_i32(desc);

    gen_helper_sve_last_active_element(ret, t_p, t_desc);

    tcg_temp_free_i32(t_desc);
    tcg_temp_free_ptr(t_p);
}

/* Increment LAST to the offset of the next element in the vector,
 * wrapping around to 0.
 */
static void incr_last_active(DisasContext *s, TCGv_i32 last, int esz)
{
    unsigned vsz = vec_full_reg_size(s);

    tcg_gen_addi_i32(last, last, 1 << esz);
    if (is_power_of_2(vsz)) {
        tcg_gen_andi_i32(last, last, vsz - 1);
    } else {
        TCGv_i32 max = tcg_const_i32(vsz);
        TCGv_i32 zero = tcg_const_i32(0);
        tcg_gen_movcond_i32(TCG_COND_GEU, last, last, max, zero, last);
        tcg_temp_free_i32(max);
        tcg_temp_free_i32(zero);
    }
}

/* If LAST < 0, set LAST to the offset of the last element in the vector.  */
static void wrap_last_active(DisasContext *s, TCGv_i32 last, int esz)
{
    unsigned vsz = vec_full_reg_size(s);

    if (is_power_of_2(vsz)) {
        tcg_gen_andi_i32(last, last, vsz - 1);
    } else {
        TCGv_i32 max = tcg_const_i32(vsz - (1 << esz));
        TCGv_i32 zero = tcg_const_i32(0);
        tcg_gen_movcond_i32(TCG_COND_LT, last, last, zero, max, last);
        tcg_temp_free_i32(max);
        tcg_temp_free_i32(zero);
    }
}

/* Load an unsigned element of ESZ from BASE+OFS.  */
static TCGv_i64 load_esz(TCGv_ptr base, int ofs, int esz)
{
    TCGv_i64 r = tcg_temp_new_i64();

    switch (esz) {
    case 0:
        tcg_gen_ld8u_i64(r, base, ofs);
        break;
    case 1:
        tcg_gen_ld16u_i64(r, base, ofs);
        break;
    case 2:
        tcg_gen_ld32u_i64(r, base, ofs);
        break;
    case 3:
        tcg_gen_ld_i64(r, base, ofs);
        break;
    default:
        g_assert_not_reached();
    }
    return r;
}

/* Load an unsigned element of ESZ from RM[LAST].  */
static TCGv_i64 load_last_active(DisasContext *s, TCGv_i32 last,
                                 int rm, int esz)
{
    TCGv_ptr p = tcg_temp_new_ptr();
    TCGv_i64 r;

    /* Convert offset into vector into offset into ENV.
     * The final adjustment for the vector register base
     * is added via constant offset to the load.
     */
#ifdef HOST_WORDS_BIGENDIAN
    /* Adjust for element ordering.  See vec_reg_offset.  */
    if (esz < 3) {
        tcg_gen_xori_i32(last, last, 8 - (1 << esz));
    }
#endif
    tcg_gen_ext_i32_ptr(p, last);
    tcg_gen_add_ptr(p, p, cpu_env);

    r = load_esz(p, vec_full_reg_offset(s, rm), esz);
    tcg_temp_free_ptr(p);

    return r;
}

/* Compute CLAST for a Zreg.  */
static bool do_clast_vector(DisasContext *s, arg_rprr_esz *a, bool before)
{
    TCGv_i32 last;
    TCGLabel *over;
    TCGv_i64 ele;
    unsigned vsz, esz = a->esz;

    if (!sve_access_check(s)) {
        return true;
    }

    last = tcg_temp_local_new_i32();
    over = gen_new_label();

    find_last_active(s, last, esz, a->pg);

    /* There is of course no movcond for a 2048-bit vector,
     * so we must branch over the actual store.
     */
    tcg_gen_brcondi_i32(TCG_COND_LT, last, 0, over);

    if (!before) {
        incr_last_active(s, last, esz);
    }

    ele = load_last_active(s, last, a->rm, esz);
    tcg_temp_free_i32(last);

    vsz = vec_full_reg_size(s);
    tcg_gen_gvec_dup_i64(esz, vec_full_reg_offset(s, a->rd), vsz, vsz, ele);
    tcg_temp_free_i64(ele);

    /* If this insn used MOVPRFX, we may need a second move.  */
    if (a->rd != a->rn) {
        TCGLabel *done = gen_new_label();
        tcg_gen_br(done);

        gen_set_label(over);
        do_mov_z(s, a->rd, a->rn);

        gen_set_label(done);
    } else {
        gen_set_label(over);
    }
    return true;
}

static bool trans_CLASTA_z(DisasContext *s, arg_rprr_esz *a, uint32_t insn)
{
    return do_clast_vector(s, a, false);
}

static bool trans_CLASTB_z(DisasContext *s, arg_rprr_esz *a, uint32_t insn)
{
    return do_clast_vector(s, a, true);
}

/* Compute CLAST for a scalar.  */
static void do_clast_scalar(DisasContext *s, int esz, int pg, int rm,
                            bool before, TCGv_i64 reg_val)
{
    TCGv_i32 last = tcg_temp_new_i32();
    TCGv_i64 ele, cmp, zero;

    find_last_active(s, last, esz, pg);

    /* Extend the original value of last prior to incrementing.  */
    cmp = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(cmp, last);

    if (!before) {
        incr_last_active(s, last, esz);
    }

    /* The conceit here is that while last < 0 indicates not found, after
     * adjusting for cpu_env->vfp.zregs[rm], it is still a valid address
     * from which we can load garbage.  We then discard the garbage with
     * a conditional move.
     */
    ele = load_last_active(s, last, rm, esz);
    tcg_temp_free_i32(last);

    zero = tcg_const_i64(0);
    tcg_gen_movcond_i64(TCG_COND_GE, reg_val, cmp, zero, ele, reg_val);

    tcg_temp_free_i64(zero);
    tcg_temp_free_i64(cmp);
    tcg_temp_free_i64(ele);
}

/* Compute CLAST for a Vreg.  */
static bool do_clast_fp(DisasContext *s, arg_rpr_esz *a, bool before)
{
    if (sve_access_check(s)) {
        int esz = a->esz;
        int ofs = vec_reg_offset(s, a->rd, 0, esz);
        TCGv_i64 reg = load_esz(cpu_env, ofs, esz);

        do_clast_scalar(s, esz, a->pg, a->rn, before, reg);
        write_fp_dreg(s, a->rd, reg);
        tcg_temp_free_i64(reg);
    }
    return true;
}

static bool trans_CLASTA_v(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_clast_fp(s, a, false);
}

static bool trans_CLASTB_v(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_clast_fp(s, a, true);
}

/* Compute CLAST for a Xreg.  */
static bool do_clast_general(DisasContext *s, arg_rpr_esz *a, bool before)
{
    TCGv_i64 reg;

    if (!sve_access_check(s)) {
        return true;
    }

    reg = cpu_reg(s, a->rd);
    switch (a->esz) {
    case 0:
        tcg_gen_ext8u_i64(reg, reg);
        break;
    case 1:
        tcg_gen_ext16u_i64(reg, reg);
        break;
    case 2:
        tcg_gen_ext32u_i64(reg, reg);
        break;
    case 3:
        break;
    default:
        g_assert_not_reached();
    }

    do_clast_scalar(s, a->esz, a->pg, a->rn, before, reg);
    return true;
}

static bool trans_CLASTA_r(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_clast_general(s, a, false);
}

static bool trans_CLASTB_r(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_clast_general(s, a, true);
}

/* Compute LAST for a scalar.  */
static TCGv_i64 do_last_scalar(DisasContext *s, int esz,
                               int pg, int rm, bool before)
{
    TCGv_i32 last = tcg_temp_new_i32();
    TCGv_i64 ret;

    find_last_active(s, last, esz, pg);
    if (before) {
        wrap_last_active(s, last, esz);
    } else {
        incr_last_active(s, last, esz);
    }

    ret = load_last_active(s, last, rm, esz);
    tcg_temp_free_i32(last);
    return ret;
}

/* Compute LAST for a Vreg.  */
static bool do_last_fp(DisasContext *s, arg_rpr_esz *a, bool before)
{
    if (sve_access_check(s)) {
        TCGv_i64 val = do_last_scalar(s, a->esz, a->pg, a->rn, before);
        write_fp_dreg(s, a->rd, val);
        tcg_temp_free_i64(val);
    }
    return true;
}

static bool trans_LASTA_v(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_last_fp(s, a, false);
}

static bool trans_LASTB_v(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_last_fp(s, a, true);
}

/* Compute LAST for a Xreg.  */
static bool do_last_general(DisasContext *s, arg_rpr_esz *a, bool before)
{
    if (sve_access_check(s)) {
        TCGv_i64 val = do_last_scalar(s, a->esz, a->pg, a->rn, before);
        tcg_gen_mov_i64(cpu_reg(s, a->rd), val);
        tcg_temp_free_i64(val);
    }
    return true;
}

static bool trans_LASTA_r(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_last_general(s, a, false);
}

static bool trans_LASTB_r(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_last_general(s, a, true);
}

static bool trans_CPY_m_r(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        do_cpy_m(s, a->esz, a->rd, a->rd, a->pg, cpu_reg_sp(s, a->rn));
    }
    return true;
}

static bool trans_CPY_m_v(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        int ofs = vec_reg_offset(s, a->rn, 0, a->esz);
        TCGv_i64 t = load_esz(cpu_env, ofs, a->esz);
        do_cpy_m(s, a->esz, a->rd, a->rd, a->pg, t);
        tcg_temp_free_i64(t);
    }
    return true;
}

static bool trans_REVB(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        gen_helper_sve_revb_h,
        gen_helper_sve_revb_s,
        gen_helper_sve_revb_d,
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_REVH(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        NULL,
        NULL,
        gen_helper_sve_revh_s,
        gen_helper_sve_revh_d,
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_REVW(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ool(s, a, a->esz == 3 ? gen_helper_sve_revw_d : NULL);
}

static bool trans_RBIT(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_sve_rbit_b,
        gen_helper_sve_rbit_h,
        gen_helper_sve_rbit_s,
        gen_helper_sve_rbit_d,
    };
    return do_zpz_ool(s, a, fns[a->esz]);
}

static bool trans_SPLICE(DisasContext *s, arg_rprr_esz *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_4_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           pred_full_reg_offset(s, a->pg),
                           vsz, vsz, a->esz, gen_helper_sve_splice);
    }
    return true;
}

/*
 *** SVE Integer Compare - Vectors Group
 */

static bool do_ppzz_flags(DisasContext *s, arg_rprr_esz *a,
                          gen_helper_gvec_flags_4 *gen_fn)
{
    TCGv_ptr pd, zn, zm, pg;
    unsigned vsz;
    TCGv_i32 t;

    if (gen_fn == NULL) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    vsz = vec_full_reg_size(s);
    t = tcg_const_i32(simd_desc(vsz, vsz, 0));
    pd = tcg_temp_new_ptr();
    zn = tcg_temp_new_ptr();
    zm = tcg_temp_new_ptr();
    pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(pd, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(zn, cpu_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(zm, cpu_env, vec_full_reg_offset(s, a->rm));
    tcg_gen_addi_ptr(pg, cpu_env, pred_full_reg_offset(s, a->pg));

    gen_fn(t, pd, zn, zm, pg, t);

    tcg_temp_free_ptr(pd);
    tcg_temp_free_ptr(zn);
    tcg_temp_free_ptr(zm);
    tcg_temp_free_ptr(pg);

    do_pred_flags(t);

    tcg_temp_free_i32(t);
    return true;
}

#define DO_PPZZ(NAME, name) \
static bool trans_##NAME##_ppzz(DisasContext *s, arg_rprr_esz *a,         \
                                uint32_t insn)                            \
{                                                                         \
    static gen_helper_gvec_flags_4 * const fns[4] = {                     \
        gen_helper_sve_##name##_ppzz_b, gen_helper_sve_##name##_ppzz_h,   \
        gen_helper_sve_##name##_ppzz_s, gen_helper_sve_##name##_ppzz_d,   \
    };                                                                    \
    return do_ppzz_flags(s, a, fns[a->esz]);                              \
}

DO_PPZZ(CMPEQ, cmpeq)
DO_PPZZ(CMPNE, cmpne)
DO_PPZZ(CMPGT, cmpgt)
DO_PPZZ(CMPGE, cmpge)
DO_PPZZ(CMPHI, cmphi)
DO_PPZZ(CMPHS, cmphs)

#undef DO_PPZZ

#define DO_PPZW(NAME, name) \
static bool trans_##NAME##_ppzw(DisasContext *s, arg_rprr_esz *a,         \
                                uint32_t insn)                            \
{                                                                         \
    static gen_helper_gvec_flags_4 * const fns[4] = {                     \
        gen_helper_sve_##name##_ppzw_b, gen_helper_sve_##name##_ppzw_h,   \
        gen_helper_sve_##name##_ppzw_s, NULL                              \
    };                                                                    \
    return do_ppzz_flags(s, a, fns[a->esz]);                              \
}

DO_PPZW(CMPEQ, cmpeq)
DO_PPZW(CMPNE, cmpne)
DO_PPZW(CMPGT, cmpgt)
DO_PPZW(CMPGE, cmpge)
DO_PPZW(CMPHI, cmphi)
DO_PPZW(CMPHS, cmphs)
DO_PPZW(CMPLT, cmplt)
DO_PPZW(CMPLE, cmple)
DO_PPZW(CMPLO, cmplo)
DO_PPZW(CMPLS, cmpls)

#undef DO_PPZW

/*
 *** SVE Integer Compare - Immediate Groups
 */

static bool do_ppzi_flags(DisasContext *s, arg_rpri_esz *a,
                          gen_helper_gvec_flags_3 *gen_fn)
{
    TCGv_ptr pd, zn, pg;
    unsigned vsz;
    TCGv_i32 t;

    if (gen_fn == NULL) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    vsz = vec_full_reg_size(s);
    t = tcg_const_i32(simd_desc(vsz, vsz, a->imm));
    pd = tcg_temp_new_ptr();
    zn = tcg_temp_new_ptr();
    pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(pd, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(zn, cpu_env, vec_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(pg, cpu_env, pred_full_reg_offset(s, a->pg));

    gen_fn(t, pd, zn, pg, t);

    tcg_temp_free_ptr(pd);
    tcg_temp_free_ptr(zn);
    tcg_temp_free_ptr(pg);

    do_pred_flags(t);

    tcg_temp_free_i32(t);
    return true;
}

#define DO_PPZI(NAME, name) \
static bool trans_##NAME##_ppzi(DisasContext *s, arg_rpri_esz *a,         \
                                uint32_t insn)                            \
{                                                                         \
    static gen_helper_gvec_flags_3 * const fns[4] = {                     \
        gen_helper_sve_##name##_ppzi_b, gen_helper_sve_##name##_ppzi_h,   \
        gen_helper_sve_##name##_ppzi_s, gen_helper_sve_##name##_ppzi_d,   \
    };                                                                    \
    return do_ppzi_flags(s, a, fns[a->esz]);                              \
}

DO_PPZI(CMPEQ, cmpeq)
DO_PPZI(CMPNE, cmpne)
DO_PPZI(CMPGT, cmpgt)
DO_PPZI(CMPGE, cmpge)
DO_PPZI(CMPHI, cmphi)
DO_PPZI(CMPHS, cmphs)
DO_PPZI(CMPLT, cmplt)
DO_PPZI(CMPLE, cmple)
DO_PPZI(CMPLO, cmplo)
DO_PPZI(CMPLS, cmpls)

#undef DO_PPZI

/*
 *** SVE Partition Break Group
 */

static bool do_brk3(DisasContext *s, arg_rprr_s *a,
                    gen_helper_gvec_4 *fn, gen_helper_gvec_flags_4 *fn_s)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = pred_full_reg_size(s);

    /* Predicate sizes may be smaller and cannot use simd_desc.  */
    TCGv_ptr d = tcg_temp_new_ptr();
    TCGv_ptr n = tcg_temp_new_ptr();
    TCGv_ptr m = tcg_temp_new_ptr();
    TCGv_ptr g = tcg_temp_new_ptr();
    TCGv_i32 t = tcg_const_i32(vsz - 2);

    tcg_gen_addi_ptr(d, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(n, cpu_env, pred_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(m, cpu_env, pred_full_reg_offset(s, a->rm));
    tcg_gen_addi_ptr(g, cpu_env, pred_full_reg_offset(s, a->pg));

    if (a->s) {
        fn_s(t, d, n, m, g, t);
        do_pred_flags(t);
    } else {
        fn(d, n, m, g, t);
    }
    tcg_temp_free_ptr(d);
    tcg_temp_free_ptr(n);
    tcg_temp_free_ptr(m);
    tcg_temp_free_ptr(g);
    tcg_temp_free_i32(t);
    return true;
}

static bool do_brk2(DisasContext *s, arg_rpr_s *a,
                    gen_helper_gvec_3 *fn, gen_helper_gvec_flags_3 *fn_s)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = pred_full_reg_size(s);

    /* Predicate sizes may be smaller and cannot use simd_desc.  */
    TCGv_ptr d = tcg_temp_new_ptr();
    TCGv_ptr n = tcg_temp_new_ptr();
    TCGv_ptr g = tcg_temp_new_ptr();
    TCGv_i32 t = tcg_const_i32(vsz - 2);

    tcg_gen_addi_ptr(d, cpu_env, pred_full_reg_offset(s, a->rd));
    tcg_gen_addi_ptr(n, cpu_env, pred_full_reg_offset(s, a->rn));
    tcg_gen_addi_ptr(g, cpu_env, pred_full_reg_offset(s, a->pg));

    if (a->s) {
        fn_s(t, d, n, g, t);
        do_pred_flags(t);
    } else {
        fn(d, n, g, t);
    }
    tcg_temp_free_ptr(d);
    tcg_temp_free_ptr(n);
    tcg_temp_free_ptr(g);
    tcg_temp_free_i32(t);
    return true;
}

static bool trans_BRKPA(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    return do_brk3(s, a, gen_helper_sve_brkpa, gen_helper_sve_brkpas);
}

static bool trans_BRKPB(DisasContext *s, arg_rprr_s *a, uint32_t insn)
{
    return do_brk3(s, a, gen_helper_sve_brkpb, gen_helper_sve_brkpbs);
}

static bool trans_BRKA_m(DisasContext *s, arg_rpr_s *a, uint32_t insn)
{
    return do_brk2(s, a, gen_helper_sve_brka_m, gen_helper_sve_brkas_m);
}

static bool trans_BRKB_m(DisasContext *s, arg_rpr_s *a, uint32_t insn)
{
    return do_brk2(s, a, gen_helper_sve_brkb_m, gen_helper_sve_brkbs_m);
}

static bool trans_BRKA_z(DisasContext *s, arg_rpr_s *a, uint32_t insn)
{
    return do_brk2(s, a, gen_helper_sve_brka_z, gen_helper_sve_brkas_z);
}

static bool trans_BRKB_z(DisasContext *s, arg_rpr_s *a, uint32_t insn)
{
    return do_brk2(s, a, gen_helper_sve_brkb_z, gen_helper_sve_brkbs_z);
}

static bool trans_BRKN(DisasContext *s, arg_rpr_s *a, uint32_t insn)
{
    return do_brk2(s, a, gen_helper_sve_brkn, gen_helper_sve_brkns);
}

/*
 *** SVE Predicate Count Group
 */

static void do_cntp(DisasContext *s, TCGv_i64 val, int esz, int pn, int pg)
{
    unsigned psz = pred_full_reg_size(s);

    if (psz <= 8) {
        uint64_t psz_mask;

        tcg_gen_ld_i64(val, cpu_env, pred_full_reg_offset(s, pn));
        if (pn != pg) {
            TCGv_i64 g = tcg_temp_new_i64();
            tcg_gen_ld_i64(g, cpu_env, pred_full_reg_offset(s, pg));
            tcg_gen_and_i64(val, val, g);
            tcg_temp_free_i64(g);
        }

        /* Reduce the pred_esz_masks value simply to reduce the
         * size of the code generated here.
         */
        psz_mask = MAKE_64BIT_MASK(0, psz * 8);
        tcg_gen_andi_i64(val, val, pred_esz_masks[esz] & psz_mask);

        tcg_gen_ctpop_i64(val, val);
    } else {
        TCGv_ptr t_pn = tcg_temp_new_ptr();
        TCGv_ptr t_pg = tcg_temp_new_ptr();
        unsigned desc;
        TCGv_i32 t_desc;

        desc = psz - 2;
        desc = deposit32(desc, SIMD_DATA_SHIFT, 2, esz);

        tcg_gen_addi_ptr(t_pn, cpu_env, pred_full_reg_offset(s, pn));
        tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, pg));
        t_desc = tcg_const_i32(desc);

        gen_helper_sve_cntp(val, t_pn, t_pg, t_desc);
        tcg_temp_free_ptr(t_pn);
        tcg_temp_free_ptr(t_pg);
        tcg_temp_free_i32(t_desc);
    }
}

static bool trans_CNTP(DisasContext *s, arg_CNTP *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        do_cntp(s, cpu_reg(s, a->rd), a->esz, a->rn, a->pg);
    }
    return true;
}

static bool trans_INCDECP_r(DisasContext *s, arg_incdec_pred *a,
                            uint32_t insn)
{
    if (sve_access_check(s)) {
        TCGv_i64 reg = cpu_reg(s, a->rd);
        TCGv_i64 val = tcg_temp_new_i64();

        do_cntp(s, val, a->esz, a->pg, a->pg);
        if (a->d) {
            tcg_gen_sub_i64(reg, reg, val);
        } else {
            tcg_gen_add_i64(reg, reg, val);
        }
        tcg_temp_free_i64(val);
    }
    return true;
}

static bool trans_INCDECP_z(DisasContext *s, arg_incdec2_pred *a,
                            uint32_t insn)
{
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_i64 val = tcg_temp_new_i64();
        GVecGen2sFn *gvec_fn = a->d ? tcg_gen_gvec_subs : tcg_gen_gvec_adds;

        do_cntp(s, val, a->esz, a->pg, a->pg);
        gvec_fn(a->esz, vec_full_reg_offset(s, a->rd),
                vec_full_reg_offset(s, a->rn), val, vsz, vsz);
    }
    return true;
}

static bool trans_SINCDECP_r_32(DisasContext *s, arg_incdec_pred *a,
                                uint32_t insn)
{
    if (sve_access_check(s)) {
        TCGv_i64 reg = cpu_reg(s, a->rd);
        TCGv_i64 val = tcg_temp_new_i64();

        do_cntp(s, val, a->esz, a->pg, a->pg);
        do_sat_addsub_32(reg, val, a->u, a->d);
    }
    return true;
}

static bool trans_SINCDECP_r_64(DisasContext *s, arg_incdec_pred *a,
                                uint32_t insn)
{
    if (sve_access_check(s)) {
        TCGv_i64 reg = cpu_reg(s, a->rd);
        TCGv_i64 val = tcg_temp_new_i64();

        do_cntp(s, val, a->esz, a->pg, a->pg);
        do_sat_addsub_64(reg, val, a->u, a->d);
    }
    return true;
}

static bool trans_SINCDECP_z(DisasContext *s, arg_incdec2_pred *a,
                             uint32_t insn)
{
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 val = tcg_temp_new_i64();
        do_cntp(s, val, a->esz, a->pg, a->pg);
        do_sat_addsub_vec(s, a->esz, a->rd, a->rn, val, a->u, a->d);
    }
    return true;
}

/*
 *** SVE Integer Compare Scalars Group
 */

static bool trans_CTERM(DisasContext *s, arg_CTERM *a, uint32_t insn)
{
    if (!sve_access_check(s)) {
        return true;
    }

    TCGCond cond = (a->ne ? TCG_COND_NE : TCG_COND_EQ);
    TCGv_i64 rn = read_cpu_reg(s, a->rn, a->sf);
    TCGv_i64 rm = read_cpu_reg(s, a->rm, a->sf);
    TCGv_i64 cmp = tcg_temp_new_i64();

    tcg_gen_setcond_i64(cond, cmp, rn, rm);
    tcg_gen_extrl_i64_i32(cpu_NF, cmp);
    tcg_temp_free_i64(cmp);

    /* VF = !NF & !CF.  */
    tcg_gen_xori_i32(cpu_VF, cpu_NF, 1);
    tcg_gen_andc_i32(cpu_VF, cpu_VF, cpu_CF);

    /* Both NF and VF actually look at bit 31.  */
    tcg_gen_neg_i32(cpu_NF, cpu_NF);
    tcg_gen_neg_i32(cpu_VF, cpu_VF);
    return true;
}

static bool trans_WHILE(DisasContext *s, arg_WHILE *a, uint32_t insn)
{
    if (!sve_access_check(s)) {
        return true;
    }

    TCGv_i64 op0 = read_cpu_reg(s, a->rn, 1);
    TCGv_i64 op1 = read_cpu_reg(s, a->rm, 1);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i32 t2, t3;
    TCGv_ptr ptr;
    unsigned desc, vsz = vec_full_reg_size(s);
    TCGCond cond;

    if (!a->sf) {
        if (a->u) {
            tcg_gen_ext32u_i64(op0, op0);
            tcg_gen_ext32u_i64(op1, op1);
        } else {
            tcg_gen_ext32s_i64(op0, op0);
            tcg_gen_ext32s_i64(op1, op1);
        }
    }

    /* For the helper, compress the different conditions into a computation
     * of how many iterations for which the condition is true.
     *
     * This is slightly complicated by 0 <= UINT64_MAX, which is nominally
     * 2**64 iterations, overflowing to 0.  Of course, predicate registers
     * aren't that large, so any value >= predicate size is sufficient.
     */
    tcg_gen_sub_i64(t0, op1, op0);

    /* t0 = MIN(op1 - op0, vsz).  */
    tcg_gen_movi_i64(t1, vsz);
    tcg_gen_umin_i64(t0, t0, t1);
    if (a->eq) {
        /* Equality means one more iteration.  */
        tcg_gen_addi_i64(t0, t0, 1);
    }

    /* t0 = (condition true ? t0 : 0).  */
    cond = (a->u
            ? (a->eq ? TCG_COND_LEU : TCG_COND_LTU)
            : (a->eq ? TCG_COND_LE : TCG_COND_LT));
    tcg_gen_movi_i64(t1, 0);
    tcg_gen_movcond_i64(cond, t0, op0, op1, t0, t1);

    t2 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t2, t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);

    desc = (vsz / 8) - 2;
    desc = deposit32(desc, SIMD_DATA_SHIFT, 2, a->esz);
    t3 = tcg_const_i32(desc);

    ptr = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(ptr, cpu_env, pred_full_reg_offset(s, a->rd));

    gen_helper_sve_while(t2, ptr, t2, t3);
    do_pred_flags(t2);

    tcg_temp_free_ptr(ptr);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
    return true;
}

/*
 *** SVE Integer Wide Immediate - Unpredicated Group
 */

static bool trans_FDUP(DisasContext *s, arg_FDUP *a, uint32_t insn)
{
    if (a->esz == 0) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        int dofs = vec_full_reg_offset(s, a->rd);
        uint64_t imm;

        /* Decode the VFP immediate.  */
        imm = vfp_expand_imm(a->esz, a->imm);
        imm = dup_const(a->esz, imm);

        tcg_gen_gvec_dup64i(dofs, vsz, vsz, imm);
    }
    return true;
}

static bool trans_DUP_i(DisasContext *s, arg_DUP_i *a, uint32_t insn)
{
    if (a->esz == 0 && extract32(insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        int dofs = vec_full_reg_offset(s, a->rd);

        tcg_gen_gvec_dup64i(dofs, vsz, vsz, dup_const(a->esz, a->imm));
    }
    return true;
}

static bool trans_ADD_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    if (a->esz == 0 && extract32(insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_addi(a->esz, vec_full_reg_offset(s, a->rd),
                          vec_full_reg_offset(s, a->rn), a->imm, vsz, vsz);
    }
    return true;
}

static bool trans_SUB_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    a->imm = -a->imm;
    return trans_ADD_zzi(s, a, insn);
}

static bool trans_SUBR_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    static const GVecGen2s op[4] = {
        { .fni8 = tcg_gen_vec_sub8_i64,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_sve_subri_b,
          .opc = INDEX_op_sub_vec,
          .vece = MO_8,
          .scalar_first = true },
        { .fni8 = tcg_gen_vec_sub16_i64,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_sve_subri_h,
          .opc = INDEX_op_sub_vec,
          .vece = MO_16,
          .scalar_first = true },
        { .fni4 = tcg_gen_sub_i32,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_sve_subri_s,
          .opc = INDEX_op_sub_vec,
          .vece = MO_32,
          .scalar_first = true },
        { .fni8 = tcg_gen_sub_i64,
          .fniv = tcg_gen_sub_vec,
          .fno = gen_helper_sve_subri_d,
          .opc = INDEX_op_sub_vec,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .vece = MO_64,
          .scalar_first = true }
    };

    if (a->esz == 0 && extract32(insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_i64 c = tcg_const_i64(a->imm);
        tcg_gen_gvec_2s(vec_full_reg_offset(s, a->rd),
                        vec_full_reg_offset(s, a->rn),
                        vsz, vsz, c, &op[a->esz]);
        tcg_temp_free_i64(c);
    }
    return true;
}

static bool trans_MUL_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        tcg_gen_gvec_muli(a->esz, vec_full_reg_offset(s, a->rd),
                          vec_full_reg_offset(s, a->rn), a->imm, vsz, vsz);
    }
    return true;
}

static bool do_zzi_sat(DisasContext *s, arg_rri_esz *a, uint32_t insn,
                       bool u, bool d)
{
    if (a->esz == 0 && extract32(insn, 13, 1)) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 val = tcg_const_i64(a->imm);
        do_sat_addsub_vec(s, a->esz, a->rd, a->rn, val, u, d);
        tcg_temp_free_i64(val);
    }
    return true;
}

static bool trans_SQADD_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    return do_zzi_sat(s, a, insn, false, false);
}

static bool trans_UQADD_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    return do_zzi_sat(s, a, insn, true, false);
}

static bool trans_SQSUB_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    return do_zzi_sat(s, a, insn, false, true);
}

static bool trans_UQSUB_zzi(DisasContext *s, arg_rri_esz *a, uint32_t insn)
{
    return do_zzi_sat(s, a, insn, true, true);
}

static bool do_zzi_ool(DisasContext *s, arg_rri_esz *a, gen_helper_gvec_2i *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_i64 c = tcg_const_i64(a->imm);

        tcg_gen_gvec_2i_ool(vec_full_reg_offset(s, a->rd),
                            vec_full_reg_offset(s, a->rn),
                            c, vsz, vsz, 0, fn);
        tcg_temp_free_i64(c);
    }
    return true;
}

#define DO_ZZI(NAME, name) \
static bool trans_##NAME##_zzi(DisasContext *s, arg_rri_esz *a,         \
                               uint32_t insn)                           \
{                                                                       \
    static gen_helper_gvec_2i * const fns[4] = {                        \
        gen_helper_sve_##name##i_b, gen_helper_sve_##name##i_h,         \
        gen_helper_sve_##name##i_s, gen_helper_sve_##name##i_d,         \
    };                                                                  \
    return do_zzi_ool(s, a, fns[a->esz]);                               \
}

DO_ZZI(SMAX, smax)
DO_ZZI(UMAX, umax)
DO_ZZI(SMIN, smin)
DO_ZZI(UMIN, umin)

#undef DO_ZZI

/*
 *** SVE Floating Point Accumulating Reduction Group
 */

static bool trans_FADDA(DisasContext *s, arg_rprr_esz *a, uint32_t insn)
{
    typedef void fadda_fn(TCGv_i64, TCGv_i64, TCGv_ptr,
                          TCGv_ptr, TCGv_ptr, TCGv_i32);
    static fadda_fn * const fns[3] = {
        gen_helper_sve_fadda_h,
        gen_helper_sve_fadda_s,
        gen_helper_sve_fadda_d,
    };
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr t_rm, t_pg, t_fpst;
    TCGv_i64 t_val;
    TCGv_i32 t_desc;

    if (a->esz == 0) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    t_val = load_esz(cpu_env, vec_reg_offset(s, a->rn, 0, a->esz), a->esz);
    t_rm = tcg_temp_new_ptr();
    t_pg = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(t_rm, cpu_env, vec_full_reg_offset(s, a->rm));
    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, a->pg));
    t_fpst = get_fpstatus_ptr(a->esz == MO_16);
    t_desc = tcg_const_i32(simd_desc(vsz, vsz, 0));

    fns[a->esz - 1](t_val, t_val, t_rm, t_pg, t_fpst, t_desc);

    tcg_temp_free_i32(t_desc);
    tcg_temp_free_ptr(t_fpst);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_ptr(t_rm);

    write_fp_dreg(s, a->rd, t_val);
    tcg_temp_free_i64(t_val);
    return true;
}

/*
 *** SVE Floating Point Arithmetic - Unpredicated Group
 */

static bool do_zzz_fp(DisasContext *s, arg_rrr_esz *a,
                      gen_helper_gvec_3_ptr *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = get_fpstatus_ptr(a->esz == MO_16);
        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           status, vsz, vsz, 0, fn);
        tcg_temp_free_ptr(status);
    }
    return true;
}


#define DO_FP3(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rrr_esz *a, uint32_t insn) \
{                                                                   \
    static gen_helper_gvec_3_ptr * const fns[4] = {                 \
        NULL, gen_helper_gvec_##name##_h,                           \
        gen_helper_gvec_##name##_s, gen_helper_gvec_##name##_d      \
    };                                                              \
    return do_zzz_fp(s, a, fns[a->esz]);                            \
}

DO_FP3(FADD_zzz, fadd)
DO_FP3(FSUB_zzz, fsub)
DO_FP3(FMUL_zzz, fmul)
DO_FP3(FTSMUL, ftsmul)
DO_FP3(FRECPS, recps)
DO_FP3(FRSQRTS, rsqrts)

#undef DO_FP3

/*
 *** SVE Floating Point Arithmetic - Predicated Group
 */

static bool do_zpzz_fp(DisasContext *s, arg_rprr_esz *a,
                       gen_helper_gvec_4_ptr *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = get_fpstatus_ptr(a->esz == MO_16);
        tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           pred_full_reg_offset(s, a->pg),
                           status, vsz, vsz, 0, fn);
        tcg_temp_free_ptr(status);
    }
    return true;
}

#define DO_FP3(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rprr_esz *a, uint32_t insn) \
{                                                                   \
    static gen_helper_gvec_4_ptr * const fns[4] = {                 \
        NULL, gen_helper_sve_##name##_h,                            \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d        \
    };                                                              \
    return do_zpzz_fp(s, a, fns[a->esz]);                           \
}

DO_FP3(FADD_zpzz, fadd)
DO_FP3(FSUB_zpzz, fsub)
DO_FP3(FMUL_zpzz, fmul)
DO_FP3(FMIN_zpzz, fmin)
DO_FP3(FMAX_zpzz, fmax)
DO_FP3(FMINNM_zpzz, fminnum)
DO_FP3(FMAXNM_zpzz, fmaxnum)
DO_FP3(FABD, fabd)
DO_FP3(FSCALE, fscalbn)
DO_FP3(FDIV, fdiv)
DO_FP3(FMULX, fmulx)

#undef DO_FP3

typedef void gen_helper_sve_fmla(TCGv_env, TCGv_ptr, TCGv_i32);

static bool do_fmla(DisasContext *s, arg_rprrr_esz *a, gen_helper_sve_fmla *fn)
{
    if (fn == NULL) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = vec_full_reg_size(s);
    unsigned desc;
    TCGv_i32 t_desc;
    TCGv_ptr pg = tcg_temp_new_ptr();

    /* We would need 7 operands to pass these arguments "properly".
     * So we encode all the register numbers into the descriptor.
     */
    desc = deposit32(a->rd, 5, 5, a->rn);
    desc = deposit32(desc, 10, 5, a->rm);
    desc = deposit32(desc, 15, 5, a->ra);
    desc = simd_desc(vsz, vsz, desc);

    t_desc = tcg_const_i32(desc);
    tcg_gen_addi_ptr(pg, cpu_env, pred_full_reg_offset(s, a->pg));
    fn(cpu_env, pg, t_desc);
    tcg_temp_free_i32(t_desc);
    tcg_temp_free_ptr(pg);
    return true;
}

#define DO_FMLA(NAME, name) \
static bool trans_##NAME(DisasContext *s, arg_rprrr_esz *a, uint32_t insn) \
{                                                                    \
    static gen_helper_sve_fmla * const fns[4] = {                    \
        NULL, gen_helper_sve_##name##_h,                             \
        gen_helper_sve_##name##_s, gen_helper_sve_##name##_d         \
    };                                                               \
    return do_fmla(s, a, fns[a->esz]);                               \
}

DO_FMLA(FMLA_zpzzz, fmla_zpzzz)
DO_FMLA(FMLS_zpzzz, fmls_zpzzz)
DO_FMLA(FNMLA_zpzzz, fnmla_zpzzz)
DO_FMLA(FNMLS_zpzzz, fnmls_zpzzz)

#undef DO_FMLA

/*
 *** SVE Floating Point Unary Operations Predicated Group
 */

static bool do_zpz_ptr(DisasContext *s, int rd, int rn, int pg,
                       bool is_fp16, gen_helper_gvec_3_ptr *fn)
{
    if (sve_access_check(s)) {
        unsigned vsz = vec_full_reg_size(s);
        TCGv_ptr status = get_fpstatus_ptr(is_fp16);
        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, rd),
                           vec_full_reg_offset(s, rn),
                           pred_full_reg_offset(s, pg),
                           status, vsz, vsz, 0, fn);
        tcg_temp_free_ptr(status);
    }
    return true;
}

static bool trans_SCVTF_hh(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_scvt_hh);
}

static bool trans_SCVTF_sh(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_scvt_sh);
}

static bool trans_SCVTF_dh(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_scvt_dh);
}

static bool trans_SCVTF_ss(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_scvt_ss);
}

static bool trans_SCVTF_ds(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_scvt_ds);
}

static bool trans_SCVTF_sd(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_scvt_sd);
}

static bool trans_SCVTF_dd(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_scvt_dd);
}

static bool trans_UCVTF_hh(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_ucvt_hh);
}

static bool trans_UCVTF_sh(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_ucvt_sh);
}

static bool trans_UCVTF_dh(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, true, gen_helper_sve_ucvt_dh);
}

static bool trans_UCVTF_ss(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_ucvt_ss);
}

static bool trans_UCVTF_ds(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_ucvt_ds);
}

static bool trans_UCVTF_sd(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_ucvt_sd);
}

static bool trans_UCVTF_dd(DisasContext *s, arg_rpr_esz *a, uint32_t insn)
{
    return do_zpz_ptr(s, a->rd, a->rn, a->pg, false, gen_helper_sve_ucvt_dd);
}

/*
 *** SVE Memory - 32-bit Gather and Unsized Contiguous Group
 */

/* Subroutine loading a vector register at VOFS of LEN bytes.
 * The load should begin at the address Rn + IMM.
 */

static void do_ldr(DisasContext *s, uint32_t vofs, uint32_t len,
                   int rn, int imm)
{
    uint32_t len_align = QEMU_ALIGN_DOWN(len, 8);
    uint32_t len_remain = len % 8;
    uint32_t nparts = len / 8 + ctpop8(len_remain);
    int midx = get_mem_index(s);
    TCGv_i64 addr, t0, t1;

    addr = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();

    /* Note that unpredicated load/store of vector/predicate registers
     * are defined as a stream of bytes, which equates to little-endian
     * operations on larger quantities.  There is no nice way to force
     * a little-endian load for aarch64_be-linux-user out of line.
     *
     * Attempt to keep code expansion to a minimum by limiting the
     * amount of unrolling done.
     */
    if (nparts <= 4) {
        int i;

        for (i = 0; i < len_align; i += 8) {
            tcg_gen_addi_i64(addr, cpu_reg_sp(s, rn), imm + i);
            tcg_gen_qemu_ld_i64(t0, addr, midx, MO_LEQ);
            tcg_gen_st_i64(t0, cpu_env, vofs + i);
        }
    } else {
        TCGLabel *loop = gen_new_label();
        TCGv_ptr tp, i = tcg_const_local_ptr(0);

        gen_set_label(loop);

        /* Minimize the number of local temps that must be re-read from
         * the stack each iteration.  Instead, re-compute values other
         * than the loop counter.
         */
        tp = tcg_temp_new_ptr();
        tcg_gen_addi_ptr(tp, i, imm);
        tcg_gen_extu_ptr_i64(addr, tp);
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, rn));

        tcg_gen_qemu_ld_i64(t0, addr, midx, MO_LEQ);

        tcg_gen_add_ptr(tp, cpu_env, i);
        tcg_gen_addi_ptr(i, i, 8);
        tcg_gen_st_i64(t0, tp, vofs);
        tcg_temp_free_ptr(tp);

        tcg_gen_brcondi_ptr(TCG_COND_LTU, i, len_align, loop);
        tcg_temp_free_ptr(i);
    }

    /* Predicate register loads can be any multiple of 2.
     * Note that we still store the entire 64-bit unit into cpu_env.
     */
    if (len_remain) {
        tcg_gen_addi_i64(addr, cpu_reg_sp(s, rn), imm + len_align);

        switch (len_remain) {
        case 2:
        case 4:
        case 8:
            tcg_gen_qemu_ld_i64(t0, addr, midx, MO_LE | ctz32(len_remain));
            break;

        case 6:
            t1 = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(t0, addr, midx, MO_LEUL);
            tcg_gen_addi_i64(addr, addr, 4);
            tcg_gen_qemu_ld_i64(t1, addr, midx, MO_LEUW);
            tcg_gen_deposit_i64(t0, t0, t1, 32, 32);
            tcg_temp_free_i64(t1);
            break;

        default:
            g_assert_not_reached();
        }
        tcg_gen_st_i64(t0, cpu_env, vofs + len_align);
    }
    tcg_temp_free_i64(addr);
    tcg_temp_free_i64(t0);
}

/* Similarly for stores.  */
static void do_str(DisasContext *s, uint32_t vofs, uint32_t len,
                   int rn, int imm)
{
    uint32_t len_align = QEMU_ALIGN_DOWN(len, 8);
    uint32_t len_remain = len % 8;
    uint32_t nparts = len / 8 + ctpop8(len_remain);
    int midx = get_mem_index(s);
    TCGv_i64 addr, t0;

    addr = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();

    /* Note that unpredicated load/store of vector/predicate registers
     * are defined as a stream of bytes, which equates to little-endian
     * operations on larger quantities.  There is no nice way to force
     * a little-endian store for aarch64_be-linux-user out of line.
     *
     * Attempt to keep code expansion to a minimum by limiting the
     * amount of unrolling done.
     */
    if (nparts <= 4) {
        int i;

        for (i = 0; i < len_align; i += 8) {
            tcg_gen_ld_i64(t0, cpu_env, vofs + i);
            tcg_gen_addi_i64(addr, cpu_reg_sp(s, rn), imm + i);
            tcg_gen_qemu_st_i64(t0, addr, midx, MO_LEQ);
        }
    } else {
        TCGLabel *loop = gen_new_label();
        TCGv_ptr t2, i = tcg_const_local_ptr(0);

        gen_set_label(loop);

        t2 = tcg_temp_new_ptr();
        tcg_gen_add_ptr(t2, cpu_env, i);
        tcg_gen_ld_i64(t0, t2, vofs);

        /* Minimize the number of local temps that must be re-read from
         * the stack each iteration.  Instead, re-compute values other
         * than the loop counter.
         */
        tcg_gen_addi_ptr(t2, i, imm);
        tcg_gen_extu_ptr_i64(addr, t2);
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, rn));
        tcg_temp_free_ptr(t2);

        tcg_gen_qemu_st_i64(t0, addr, midx, MO_LEQ);

        tcg_gen_addi_ptr(i, i, 8);

        tcg_gen_brcondi_ptr(TCG_COND_LTU, i, len_align, loop);
        tcg_temp_free_ptr(i);
    }

    /* Predicate register stores can be any multiple of 2.  */
    if (len_remain) {
        tcg_gen_ld_i64(t0, cpu_env, vofs + len_align);
        tcg_gen_addi_i64(addr, cpu_reg_sp(s, rn), imm + len_align);

        switch (len_remain) {
        case 2:
        case 4:
        case 8:
            tcg_gen_qemu_st_i64(t0, addr, midx, MO_LE | ctz32(len_remain));
            break;

        case 6:
            tcg_gen_qemu_st_i64(t0, addr, midx, MO_LEUL);
            tcg_gen_addi_i64(addr, addr, 4);
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_qemu_st_i64(t0, addr, midx, MO_LEUW);
            break;

        default:
            g_assert_not_reached();
        }
    }
    tcg_temp_free_i64(addr);
    tcg_temp_free_i64(t0);
}

static bool trans_LDR_zri(DisasContext *s, arg_rri *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        int size = vec_full_reg_size(s);
        int off = vec_full_reg_offset(s, a->rd);
        do_ldr(s, off, size, a->rn, a->imm * size);
    }
    return true;
}

static bool trans_LDR_pri(DisasContext *s, arg_rri *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        int size = pred_full_reg_size(s);
        int off = pred_full_reg_offset(s, a->rd);
        do_ldr(s, off, size, a->rn, a->imm * size);
    }
    return true;
}

static bool trans_STR_zri(DisasContext *s, arg_rri *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        int size = vec_full_reg_size(s);
        int off = vec_full_reg_offset(s, a->rd);
        do_str(s, off, size, a->rn, a->imm * size);
    }
    return true;
}

static bool trans_STR_pri(DisasContext *s, arg_rri *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        int size = pred_full_reg_size(s);
        int off = pred_full_reg_offset(s, a->rd);
        do_str(s, off, size, a->rn, a->imm * size);
    }
    return true;
}

/*
 *** SVE Memory - Contiguous Load Group
 */

/* The memory mode of the dtype.  */
static const TCGMemOp dtype_mop[16] = {
    MO_UB, MO_UB, MO_UB, MO_UB,
    MO_SL, MO_UW, MO_UW, MO_UW,
    MO_SW, MO_SW, MO_UL, MO_UL,
    MO_SB, MO_SB, MO_SB, MO_Q
};

#define dtype_msz(x)  (dtype_mop[x] & MO_SIZE)

/* The vector element size of dtype.  */
static const uint8_t dtype_esz[16] = {
    0, 1, 2, 3,
    3, 1, 2, 3,
    3, 2, 2, 3,
    3, 2, 1, 3
};

static void do_mem_zpa(DisasContext *s, int zt, int pg, TCGv_i64 addr,
                       gen_helper_gvec_mem *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr t_pg;
    TCGv_i32 desc;

    /* For e.g. LD4, there are not enough arguments to pass all 4
     * registers as pointers, so encode the regno into the data field.
     * For consistency, do this even for LD1.
     */
    desc = tcg_const_i32(simd_desc(vsz, vsz, zt));
    t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, pg));
    fn(cpu_env, t_pg, addr, desc);

    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_i32(desc);
}

static void do_ld_zpa(DisasContext *s, int zt, int pg,
                      TCGv_i64 addr, int dtype, int nreg)
{
    static gen_helper_gvec_mem * const fns[16][4] = {
        { gen_helper_sve_ld1bb_r, gen_helper_sve_ld2bb_r,
          gen_helper_sve_ld3bb_r, gen_helper_sve_ld4bb_r },
        { gen_helper_sve_ld1bhu_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bsu_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bdu_r, NULL, NULL, NULL },

        { gen_helper_sve_ld1sds_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1hh_r, gen_helper_sve_ld2hh_r,
          gen_helper_sve_ld3hh_r, gen_helper_sve_ld4hh_r },
        { gen_helper_sve_ld1hsu_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1hdu_r, NULL, NULL, NULL },

        { gen_helper_sve_ld1hds_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1hss_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1ss_r, gen_helper_sve_ld2ss_r,
          gen_helper_sve_ld3ss_r, gen_helper_sve_ld4ss_r },
        { gen_helper_sve_ld1sdu_r, NULL, NULL, NULL },

        { gen_helper_sve_ld1bds_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bss_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1bhs_r, NULL, NULL, NULL },
        { gen_helper_sve_ld1dd_r, gen_helper_sve_ld2dd_r,
          gen_helper_sve_ld3dd_r, gen_helper_sve_ld4dd_r },
    };
    gen_helper_gvec_mem *fn = fns[dtype][nreg];

    /* While there are holes in the table, they are not
     * accessible via the instruction encoding.
     */
    assert(fn != NULL);
    do_mem_zpa(s, zt, pg, addr, fn);
}

static bool trans_LD_zprr(DisasContext *s, arg_rprr_load *a, uint32_t insn)
{
    if (a->rm == 31) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_muli_i64(addr, cpu_reg(s, a->rm),
                         (a->nreg + 1) << dtype_msz(a->dtype));
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_ld_zpa(s, a->rd, a->pg, addr, a->dtype, a->nreg);
    }
    return true;
}

static bool trans_LD_zpri(DisasContext *s, arg_rpri_load *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        int vsz = vec_full_reg_size(s);
        int elements = vsz >> dtype_esz[a->dtype];
        TCGv_i64 addr = new_tmp_a64(s);

        tcg_gen_addi_i64(addr, cpu_reg_sp(s, a->rn),
                         (a->imm * elements * (a->nreg + 1))
                         << dtype_msz(a->dtype));
        do_ld_zpa(s, a->rd, a->pg, addr, a->dtype, a->nreg);
    }
    return true;
}

static bool trans_LDFF1_zprr(DisasContext *s, arg_rprr_load *a, uint32_t insn)
{
    static gen_helper_gvec_mem * const fns[16] = {
        gen_helper_sve_ldff1bb_r,
        gen_helper_sve_ldff1bhu_r,
        gen_helper_sve_ldff1bsu_r,
        gen_helper_sve_ldff1bdu_r,

        gen_helper_sve_ldff1sds_r,
        gen_helper_sve_ldff1hh_r,
        gen_helper_sve_ldff1hsu_r,
        gen_helper_sve_ldff1hdu_r,

        gen_helper_sve_ldff1hds_r,
        gen_helper_sve_ldff1hss_r,
        gen_helper_sve_ldff1ss_r,
        gen_helper_sve_ldff1sdu_r,

        gen_helper_sve_ldff1bds_r,
        gen_helper_sve_ldff1bss_r,
        gen_helper_sve_ldff1bhs_r,
        gen_helper_sve_ldff1dd_r,
    };

    if (sve_access_check(s)) {
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), dtype_msz(a->dtype));
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_mem_zpa(s, a->rd, a->pg, addr, fns[a->dtype]);
    }
    return true;
}

static bool trans_LDNF1_zpri(DisasContext *s, arg_rpri_load *a, uint32_t insn)
{
    static gen_helper_gvec_mem * const fns[16] = {
        gen_helper_sve_ldnf1bb_r,
        gen_helper_sve_ldnf1bhu_r,
        gen_helper_sve_ldnf1bsu_r,
        gen_helper_sve_ldnf1bdu_r,

        gen_helper_sve_ldnf1sds_r,
        gen_helper_sve_ldnf1hh_r,
        gen_helper_sve_ldnf1hsu_r,
        gen_helper_sve_ldnf1hdu_r,

        gen_helper_sve_ldnf1hds_r,
        gen_helper_sve_ldnf1hss_r,
        gen_helper_sve_ldnf1ss_r,
        gen_helper_sve_ldnf1sdu_r,

        gen_helper_sve_ldnf1bds_r,
        gen_helper_sve_ldnf1bss_r,
        gen_helper_sve_ldnf1bhs_r,
        gen_helper_sve_ldnf1dd_r,
    };

    if (sve_access_check(s)) {
        int vsz = vec_full_reg_size(s);
        int elements = vsz >> dtype_esz[a->dtype];
        int off = (a->imm * elements) << dtype_msz(a->dtype);
        TCGv_i64 addr = new_tmp_a64(s);

        tcg_gen_addi_i64(addr, cpu_reg_sp(s, a->rn), off);
        do_mem_zpa(s, a->rd, a->pg, addr, fns[a->dtype]);
    }
    return true;
}

static void do_ldrq(DisasContext *s, int zt, int pg, TCGv_i64 addr, int msz)
{
    static gen_helper_gvec_mem * const fns[4] = {
        gen_helper_sve_ld1bb_r, gen_helper_sve_ld1hh_r,
        gen_helper_sve_ld1ss_r, gen_helper_sve_ld1dd_r,
    };
    unsigned vsz = vec_full_reg_size(s);
    TCGv_ptr t_pg;
    TCGv_i32 desc;

    /* Load the first quadword using the normal predicated load helpers.  */
    desc = tcg_const_i32(simd_desc(16, 16, zt));
    t_pg = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, pg));
    fns[msz](cpu_env, t_pg, addr, desc);

    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_i32(desc);

    /* Replicate that first quadword.  */
    if (vsz > 16) {
        unsigned dofs = vec_full_reg_offset(s, zt);
        tcg_gen_gvec_dup_mem(4, dofs + 16, dofs, vsz - 16, vsz - 16);
    }
}

static bool trans_LD1RQ_zprr(DisasContext *s, arg_rprr_load *a, uint32_t insn)
{
    if (a->rm == 31) {
        return false;
    }
    if (sve_access_check(s)) {
        int msz = dtype_msz(a->dtype);
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_shli_i64(addr, cpu_reg(s, a->rm), msz);
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_ldrq(s, a->rd, a->pg, addr, msz);
    }
    return true;
}

static bool trans_LD1RQ_zpri(DisasContext *s, arg_rpri_load *a, uint32_t insn)
{
    if (sve_access_check(s)) {
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_addi_i64(addr, cpu_reg_sp(s, a->rn), a->imm * 16);
        do_ldrq(s, a->rd, a->pg, addr, dtype_msz(a->dtype));
    }
    return true;
}

/* Load and broadcast element.  */
static bool trans_LD1R_zpri(DisasContext *s, arg_rpri_load *a, uint32_t insn)
{
    if (!sve_access_check(s)) {
        return true;
    }

    unsigned vsz = vec_full_reg_size(s);
    unsigned psz = pred_full_reg_size(s);
    unsigned esz = dtype_esz[a->dtype];
    TCGLabel *over = gen_new_label();
    TCGv_i64 temp;

    /* If the guarding predicate has no bits set, no load occurs.  */
    if (psz <= 8) {
        /* Reduce the pred_esz_masks value simply to reduce the
         * size of the code generated here.
         */
        uint64_t psz_mask = MAKE_64BIT_MASK(0, psz * 8);
        temp = tcg_temp_new_i64();
        tcg_gen_ld_i64(temp, cpu_env, pred_full_reg_offset(s, a->pg));
        tcg_gen_andi_i64(temp, temp, pred_esz_masks[esz] & psz_mask);
        tcg_gen_brcondi_i64(TCG_COND_EQ, temp, 0, over);
        tcg_temp_free_i64(temp);
    } else {
        TCGv_i32 t32 = tcg_temp_new_i32();
        find_last_active(s, t32, esz, a->pg);
        tcg_gen_brcondi_i32(TCG_COND_LT, t32, 0, over);
        tcg_temp_free_i32(t32);
    }

    /* Load the data.  */
    temp = tcg_temp_new_i64();
    tcg_gen_addi_i64(temp, cpu_reg_sp(s, a->rn), a->imm << esz);
    tcg_gen_qemu_ld_i64(temp, temp, get_mem_index(s),
                        s->be_data | dtype_mop[a->dtype]);

    /* Broadcast to *all* elements.  */
    tcg_gen_gvec_dup_i64(esz, vec_full_reg_offset(s, a->rd),
                         vsz, vsz, temp);
    tcg_temp_free_i64(temp);

    /* Zero the inactive elements.  */
    gen_set_label(over);
    do_movz_zpz(s, a->rd, a->rd, a->pg, esz);
    return true;
}

static void do_st_zpa(DisasContext *s, int zt, int pg, TCGv_i64 addr,
                      int msz, int esz, int nreg)
{
    static gen_helper_gvec_mem * const fn_single[4][4] = {
        { gen_helper_sve_st1bb_r, gen_helper_sve_st1bh_r,
          gen_helper_sve_st1bs_r, gen_helper_sve_st1bd_r },
        { NULL,                   gen_helper_sve_st1hh_r,
          gen_helper_sve_st1hs_r, gen_helper_sve_st1hd_r },
        { NULL, NULL,
          gen_helper_sve_st1ss_r, gen_helper_sve_st1sd_r },
        { NULL, NULL, NULL, gen_helper_sve_st1dd_r },
    };
    static gen_helper_gvec_mem * const fn_multiple[3][4] = {
        { gen_helper_sve_st2bb_r, gen_helper_sve_st2hh_r,
          gen_helper_sve_st2ss_r, gen_helper_sve_st2dd_r },
        { gen_helper_sve_st3bb_r, gen_helper_sve_st3hh_r,
          gen_helper_sve_st3ss_r, gen_helper_sve_st3dd_r },
        { gen_helper_sve_st4bb_r, gen_helper_sve_st4hh_r,
          gen_helper_sve_st4ss_r, gen_helper_sve_st4dd_r },
    };
    gen_helper_gvec_mem *fn;

    if (nreg == 0) {
        /* ST1 */
        fn = fn_single[msz][esz];
    } else {
        /* ST2, ST3, ST4 -- msz == esz, enforced by encoding */
        assert(msz == esz);
        fn = fn_multiple[nreg - 1][msz];
    }
    assert(fn != NULL);
    do_mem_zpa(s, zt, pg, addr, fn);
}

static bool trans_ST_zprr(DisasContext *s, arg_rprr_store *a, uint32_t insn)
{
    if (a->rm == 31 || a->msz > a->esz) {
        return false;
    }
    if (sve_access_check(s)) {
        TCGv_i64 addr = new_tmp_a64(s);
        tcg_gen_muli_i64(addr, cpu_reg(s, a->rm), (a->nreg + 1) << a->msz);
        tcg_gen_add_i64(addr, addr, cpu_reg_sp(s, a->rn));
        do_st_zpa(s, a->rd, a->pg, addr, a->msz, a->esz, a->nreg);
    }
    return true;
}

static bool trans_ST_zpri(DisasContext *s, arg_rpri_store *a, uint32_t insn)
{
    if (a->msz > a->esz) {
        return false;
    }
    if (sve_access_check(s)) {
        int vsz = vec_full_reg_size(s);
        int elements = vsz >> a->esz;
        TCGv_i64 addr = new_tmp_a64(s);

        tcg_gen_addi_i64(addr, cpu_reg_sp(s, a->rn),
                         (a->imm * elements * (a->nreg + 1)) << a->msz);
        do_st_zpa(s, a->rd, a->pg, addr, a->msz, a->esz, a->nreg);
    }
    return true;
}

/*
 *** SVE gather loads / scatter stores
 */

static void do_mem_zpz(DisasContext *s, int zt, int pg, int zm, int scale,
                       TCGv_i64 scalar, gen_helper_gvec_mem_scatter *fn)
{
    unsigned vsz = vec_full_reg_size(s);
    TCGv_i32 desc = tcg_const_i32(simd_desc(vsz, vsz, scale));
    TCGv_ptr t_zm = tcg_temp_new_ptr();
    TCGv_ptr t_pg = tcg_temp_new_ptr();
    TCGv_ptr t_zt = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(t_pg, cpu_env, pred_full_reg_offset(s, pg));
    tcg_gen_addi_ptr(t_zm, cpu_env, vec_full_reg_offset(s, zm));
    tcg_gen_addi_ptr(t_zt, cpu_env, vec_full_reg_offset(s, zt));
    fn(cpu_env, t_zt, t_pg, t_zm, scalar, desc);

    tcg_temp_free_ptr(t_zt);
    tcg_temp_free_ptr(t_zm);
    tcg_temp_free_ptr(t_pg);
    tcg_temp_free_i32(desc);
}

static bool trans_ST1_zprz(DisasContext *s, arg_ST1_zprz *a, uint32_t insn)
{
    /* Indexed by [xs][msz].  */
    static gen_helper_gvec_mem_scatter * const fn32[2][3] = {
        { gen_helper_sve_stbs_zsu,
          gen_helper_sve_sths_zsu,
          gen_helper_sve_stss_zsu, },
        { gen_helper_sve_stbs_zss,
          gen_helper_sve_sths_zss,
          gen_helper_sve_stss_zss, },
    };
    /* Note that we overload xs=2 to indicate 64-bit offset.  */
    static gen_helper_gvec_mem_scatter * const fn64[3][4] = {
        { gen_helper_sve_stbd_zsu,
          gen_helper_sve_sthd_zsu,
          gen_helper_sve_stsd_zsu,
          gen_helper_sve_stdd_zsu, },
        { gen_helper_sve_stbd_zss,
          gen_helper_sve_sthd_zss,
          gen_helper_sve_stsd_zss,
          gen_helper_sve_stdd_zss, },
        { gen_helper_sve_stbd_zd,
          gen_helper_sve_sthd_zd,
          gen_helper_sve_stsd_zd,
          gen_helper_sve_stdd_zd, },
    };
    gen_helper_gvec_mem_scatter *fn;

    if (a->esz < a->msz || (a->msz == 0 && a->scale)) {
        return false;
    }
    if (!sve_access_check(s)) {
        return true;
    }
    switch (a->esz) {
    case MO_32:
        fn = fn32[a->xs][a->msz];
        break;
    case MO_64:
        fn = fn64[a->xs][a->msz];
        break;
    default:
        g_assert_not_reached();
    }
    do_mem_zpz(s, a->rd, a->pg, a->rm, a->scale * a->msz,
               cpu_reg_sp(s, a->rn), fn);
    return true;
}

/*
 * Prefetches
 */

static bool trans_PRF(DisasContext *s, arg_PRF *a, uint32_t insn)
{
    /* Prefetch is a nop within QEMU.  */
    sve_access_check(s);
    return true;
}

static bool trans_PRF_rr(DisasContext *s, arg_PRF_rr *a, uint32_t insn)
{
    if (a->rm == 31) {
        return false;
    }
    /* Prefetch is a nop within QEMU.  */
    sve_access_check(s);
    return true;
}
