/* SPDX-License-Identifier: BSD-4-Clause; independently authored FP state. */
#include "fp_guest.h"

#define RMODE_MASK UINT32_C(0x00c00000)

nc_fp_result nc_fp_bank_reset(nc_fp_bank *bank) {
    if (!bank) return NC_FP_INVALID;
    for (unsigned reg = 0; reg < 32; ++reg) {
        bank->v[reg][0] = 0;
        bank->v[reg][1] = 0;
    }
    bank->fpcr = 0;
    bank->fpsr = 0;
    return NC_FP_OK;
}

nc_fp_result nc_fp_bank_get_s(const nc_fp_bank *bank, unsigned reg, uint32_t *out) {
    if (!bank || !out || reg >= 32) return NC_FP_INVALID;
    *out = (uint32_t)bank->v[reg][0];
    return NC_FP_OK;
}
nc_fp_result nc_fp_bank_get_d(const nc_fp_bank *bank, unsigned reg, uint64_t *out) {
    if (!bank || !out || reg >= 32) return NC_FP_INVALID;
    *out = bank->v[reg][0];
    return NC_FP_OK;
}
nc_fp_result nc_fp_bank_get_q(const nc_fp_bank *bank, unsigned reg, uint64_t out[2]) {
    if (!bank || !out || reg >= 32) return NC_FP_INVALID;
    uint64_t low = bank->v[reg][0], high = bank->v[reg][1];
    out[0] = low;
    out[1] = high;
    return NC_FP_OK;
}
nc_fp_result nc_fp_bank_write_s(nc_fp_bank *bank, unsigned reg, uint32_t bits) {
    if (!bank || reg >= 32) return NC_FP_INVALID;
    bank->v[reg][0] = bits;
    bank->v[reg][1] = 0;
    return NC_FP_OK;
}
nc_fp_result nc_fp_bank_write_d(nc_fp_bank *bank, unsigned reg, uint64_t bits) {
    if (!bank || reg >= 32) return NC_FP_INVALID;
    bank->v[reg][0] = bits;
    bank->v[reg][1] = 0;
    return NC_FP_OK;
}
nc_fp_result nc_fp_bank_write_q(nc_fp_bank *bank, unsigned reg,
                                 uint64_t low, uint64_t high) {
    if (!bank || reg >= 32) return NC_FP_INVALID;
    bank->v[reg][0] = low;
    bank->v[reg][1] = high;
    return NC_FP_OK;
}
nc_fp_result nc_fp_bank_write_fpcr(nc_fp_bank *bank, uint32_t bits) {
    if (!bank || (bits & ~RMODE_MASK)) return NC_FP_INVALID;
    bank->fpcr = bits;
    return NC_FP_OK;
}
nc_fp_result nc_fp_bank_write_fpsr(nc_fp_bank *bank, uint32_t bits) {
    if (!bank || (bits & ~(uint32_t)NC_FP_STATUS_MASK)) return NC_FP_INVALID;
    bank->fpsr = bits;
    return NC_FP_OK;
}

static nc_fp_result binary32(nc_fp_bank *bank, unsigned dst,
                             unsigned left, unsigned right, int divide) {
    if (!bank || dst >= 32 || left >= 32 || right >= 32 ||
        (bank->fpcr & ~RMODE_MASK) ||
        (bank->fpsr & ~(uint32_t)NC_FP_STATUS_MASK)) return NC_FP_INVALID;
    nc_fp32_state state;
    nc_fp32_init(&state);
    if (!nc_fp32_configure(&state, bank->fpcr)) return NC_FP_INVALID;
    state.status = bank->fpsr;
    /* Read both source views before modifying any register, including aliases. */
    uint32_t a = (uint32_t)bank->v[left][0], b = (uint32_t)bank->v[right][0];
    nc_fp32_result out = divide ? nc_fp32_div(&state, a, b) : nc_fp32_add(&state, a, b);
    bank->v[dst][0] = out.bits;
    bank->v[dst][1] = 0;
    bank->fpsr = out.status;
    return NC_FP_OK;
}
nc_fp_result nc_fp_bank_add32(nc_fp_bank *bank, unsigned dst,
                              unsigned left, unsigned right) {
    return binary32(bank, dst, left, right, 0);
}
nc_fp_result nc_fp_bank_div32(nc_fp_bank *bank, unsigned dst,
                              unsigned left, unsigned right) {
    return binary32(bank, dst, left, right, 1);
}

nc_fp_result nc_fp_access_check(unsigned fp_present, unsigned el,
                                uint64_t cpacr_el1, unsigned higher_el_enabled) {
    if (fp_present > 1 || higher_el_enabled > 1 || el > 3) return NC_FP_INVALID;
    if (!fp_present) return NC_FP_UNDEFINED;
    if (el > 1 || higher_el_enabled ||
        (cpacr_el1 & ~UINT64_C(0x00300000))) return NC_FP_UNSUPPORTED;
    unsigned fpen = (unsigned)(cpacr_el1 >> 20);
    if (!(fpen & 1u) || (fpen == 1 && el == 0)) return NC_FP_TRAP_EL1;
    return NC_FP_OK;
}
