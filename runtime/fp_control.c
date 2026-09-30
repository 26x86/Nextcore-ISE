/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 service. */
#include "fp_control.h"

#define RMODE_MASK UINT32_C(0x00c00000)
#define ENABLE_MASK UINT32_C(0x00009f00)

nc_fp_result nc_fp_execute_control(nc_fp_bank *bank, uint64_t x[31],
                                   uint32_t word, unsigned fp_present,
                                   unsigned el, uint64_t cpacr_el1,
                                   unsigned higher_el_enabled) {
    if (!bank || !x) return NC_FP_INVALID;
    uint32_t fixed = word & UINT32_C(0xffffffe0);
    unsigned read, status;
    switch (fixed) {
    case UINT32_C(0xd53b4400): read = 1; status = 0; break;
    case UINT32_C(0xd51b4400): read = 0; status = 0; break;
    case UINT32_C(0xd53b4420): read = 1; status = 1; break;
    case UINT32_C(0xd51b4420): read = 0; status = 1; break;
    default: return NC_FP_UNSUPPORTED;
    }
    nc_fp_result access = nc_fp_access_check(fp_present, el, cpacr_el1,
                                            higher_el_enabled);
    if (access != NC_FP_OK) return access;
    if ((bank->fpcr & ~RMODE_MASK) ||
        (bank->fpsr & ~(uint32_t)NC_FP_STATUS_MASK)) return NC_FP_INVALID;
    unsigned rt = word & 31u;
    if (read) {
        if (rt != 31) x[rt] = status ? bank->fpsr : bank->fpcr;
        return NC_FP_OK;
    }
    uint32_t value = rt == 31 ? 0 : (uint32_t)x[rt];
    if (status) {
        if (value & ~(uint32_t)NC_FP_STATUS_MASK) return NC_FP_UNSUPPORTED;
        return nc_fp_bank_write_fpsr(bank, value);
    }
    if (value & ~(RMODE_MASK | ENABLE_MASK)) return NC_FP_UNSUPPORTED;
    /* Arm permits these enables to be RAZ/WI when exception traps are absent.
     * Do not retain them or silently imply trapping support to arithmetic. */
    return nc_fp_bank_write_fpcr(bank, value & RMODE_MASK);
}
