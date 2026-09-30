/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 service. */
#include "fp_instruction.h"

nc_fp_result nc_fp_execute32(nc_fp_bank *bank, uint64_t x[31], uint32_t w) {
    if (!bank || !x) return NC_FP_INVALID;
    unsigned dst = w & 31u, left = (w >> 5) & 31u, right = (w >> 16) & 31u;
    uint32_t binary = w & UINT32_C(0xffe0fc00);
    uint32_t move = w & UINT32_C(0xfffffc00);
    if (binary != UINT32_C(0x1e202800) &&
        binary != UINT32_C(0x1e201800) &&
        move != UINT32_C(0x1e270000) &&
        move != UINT32_C(0x1e260000)) return NC_FP_UNSUPPORTED;
    if ((bank->fpcr & ~UINT32_C(0x00c00000)) ||
        (bank->fpsr & ~(uint32_t)NC_FP_STATUS_MASK)) return NC_FP_INVALID;
    if (binary == UINT32_C(0x1e202800))
        return nc_fp_bank_add32(bank, dst, left, right);
    if (binary == UINT32_C(0x1e201800))
        return nc_fp_bank_div32(bank, dst, left, right);
    if (move == UINT32_C(0x1e270000))
        return nc_fp_bank_write_s(bank, dst, left == 31 ? 0 : (uint32_t)x[left]);
    /* WZR discards the result without reading beyond the X0..X30 array. */
    if (dst != 31) x[dst] = (uint32_t)bank->v[left][0];
    return NC_FP_OK;
}
