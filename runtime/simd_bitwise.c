/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 service. */
#include "simd_bitwise.h"

static uint64_t apply(uint32_t operation, uint64_t dst,
                      uint64_t left, uint64_t right) {
    switch (operation) {
    case UINT32_C(0x0e201c00): return left & right;
    case UINT32_C(0x0e601c00): return left & ~right;
    case UINT32_C(0x0ea01c00): return left | right;
    case UINT32_C(0x0ee01c00): return left | ~right;
    case UINT32_C(0x2e201c00): return left ^ right;
    case UINT32_C(0x2e601c00): return (dst & left) | (~dst & right);
    case UINT32_C(0x2ea01c00): return (dst & ~right) | (left & right);
    case UINT32_C(0x2ee01c00): return (dst & right) | (left & ~right);
    default: return 0; /* Caller validates the operation before evaluation. */
    }
}

nc_fp_result nc_simd_execute_bitwise(nc_fp_bank *bank, uint32_t instruction,
                                    unsigned simd_present, unsigned el,
                                    uint64_t cpacr_el1, unsigned higher_el_enabled) {
    if (!bank) return NC_FP_INVALID;
    if ((instruction & UINT32_C(0x9f20fc00)) != UINT32_C(0x0e201c00))
        return NC_FP_UNSUPPORTED;
    nc_fp_result access = nc_fp_access_check(simd_present, el, cpacr_el1,
                                            higher_el_enabled);
    if (access != NC_FP_OK) return access;
    if ((bank->fpcr & ~UINT32_C(0x00c00000)) ||
        (bank->fpsr & ~(uint32_t)NC_FP_STATUS_MASK)) return NC_FP_INVALID;
    unsigned dst = instruction & 31u;
    unsigned left = (instruction >> 5) & 31u;
    unsigned right = (instruction >> 16) & 31u;
    uint32_t operation = instruction & UINT32_C(0xbfe0fc00);
    uint64_t low = apply(operation, bank->v[dst][0],
                         bank->v[left][0], bank->v[right][0]);
    uint64_t high = (instruction & UINT32_C(0x40000000)) ?
        apply(operation, bank->v[dst][1], bank->v[left][1], bank->v[right][1]) : 0;
    return nc_fp_bank_write_q(bank, dst, low, high);
}

nc_fp_result nc_simd_execute_eor(nc_fp_bank *bank, uint32_t instruction,
                                unsigned simd_present, unsigned el,
                                uint64_t cpacr_el1, unsigned higher_el_enabled) {
    if (!bank) return NC_FP_INVALID;
    if ((instruction & UINT32_C(0xbfe0fc00)) != UINT32_C(0x2e201c00))
        return NC_FP_UNSUPPORTED;
    return nc_simd_execute_bitwise(bank, instruction, simd_present, el,
                                   cpacr_el1, higher_el_enabled);
}
