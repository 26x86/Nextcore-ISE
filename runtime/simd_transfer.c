/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 service. */
#include "simd_transfer.h"

nc_fp_result nc_simd_transfer_execute(nc_fp_bank *bank, uint64_t x[31],
                                      uint32_t instruction, unsigned fp_present,
                                      unsigned el, uint64_t cpacr_el1,
                                      unsigned higher_el_enabled) {
    if (!bank || !x) return NC_FP_INVALID;
    uint32_t operation = instruction & UINT32_C(0xfffffc00);
    switch (operation) {
    case UINT32_C(0x9e670000): /* X to D. */
    case UINT32_C(0x9e660000): /* D to X. */
    case UINT32_C(0x9eaf0000): /* X to the upper D lane. */
    case UINT32_C(0x9eae0000): /* Upper D lane to X. */
        break;
    default:
        return NC_FP_UNSUPPORTED;
    }
    nc_fp_result access = nc_fp_access_check(fp_present, el, cpacr_el1,
                                            higher_el_enabled);
    if (access != NC_FP_OK) return access;
    if ((bank->fpcr & ~UINT32_C(0x00c00000)) ||
        (bank->fpsr & ~(uint32_t)NC_FP_STATUS_MASK)) return NC_FP_INVALID;

    unsigned dst = instruction & 31u;
    unsigned src = (instruction >> 5) & 31u;
    if (operation == UINT32_C(0x9e670000)) {
        uint64_t bits = src == 31 ? 0 : x[src];
        return nc_fp_bank_write_d(bank, dst, bits);
    }
    if (operation == UINT32_C(0x9eaf0000)) {
        uint64_t bits = src == 31 ? 0 : x[src];
        uint64_t low = bank->v[dst][0];
        return nc_fp_bank_write_q(bank, dst, low, bits);
    }
    unsigned lane = operation == UINT32_C(0x9eae0000) ? 1u : 0u;
    uint64_t bits = bank->v[src][lane];
    if (dst != 31) x[dst] = bits;
    return NC_FP_OK;
}
