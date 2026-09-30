/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 service. */
#include "simd_copy.h"

enum copy_operation { COPY_DUP_ELEMENT, COPY_DUP_GENERAL, COPY_INS_GENERAL,
                      COPY_INS_ELEMENT, COPY_UMOV, COPY_SMOV };

static uint64_t lane_mask(unsigned bits) {
    return bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
}

static uint64_t read_lane(const nc_fp_bank *bank, unsigned reg,
                          unsigned lane, unsigned bits) {
    unsigned offset = lane * bits;
    return (bank->v[reg][offset / 64] >> (offset % 64)) & lane_mask(bits);
}

static void write_lane(nc_fp_bank *bank, unsigned reg, unsigned lane,
                       unsigned bits, uint64_t value) {
    unsigned offset = lane * bits, half = offset / 64, shift = offset % 64;
    uint64_t mask = lane_mask(bits);
    bank->v[reg][half] = (bank->v[reg][half] & ~(mask << shift)) |
                         ((value & mask) << shift);
}

nc_fp_result nc_simd_execute_copy(nc_fp_bank *bank, uint64_t x[31],
                                  uint32_t instruction, unsigned simd_present,
                                  unsigned el, uint64_t cpacr_el1,
                                  unsigned higher_el_enabled) {
    if (!bank || !x) return NC_FP_INVALID;
    enum copy_operation operation;
    uint32_t selector = instruction & UINT32_C(0xbfe0fc00);
    if (selector == UINT32_C(0x0e000400)) operation = COPY_DUP_ELEMENT;
    else if (selector == UINT32_C(0x0e000c00)) operation = COPY_DUP_GENERAL;
    else if (selector == UINT32_C(0x0e003c00)) operation = COPY_UMOV;
    else if (selector == UINT32_C(0x0e002c00)) operation = COPY_SMOV;
    else if ((instruction & UINT32_C(0xffe0fc00)) == UINT32_C(0x4e001c00))
        operation = COPY_INS_GENERAL;
    else if ((instruction & UINT32_C(0xffe08400)) == UINT32_C(0x6e000400))
        operation = COPY_INS_ELEMENT;
    else return NC_FP_UNSUPPORTED;

    unsigned imm5 = (instruction >> 16) & 31u;
    if (!(imm5 & 15u)) return NC_FP_UNSUPPORTED;
    unsigned size = 0;
    while (!(imm5 & (1u << size))) ++size;
    unsigned bits = 8u << size, q = (instruction >> 30) & 1u;
    if ((operation == COPY_DUP_ELEMENT || operation == COPY_DUP_GENERAL) &&
        size == 3 && !q) return NC_FP_UNSUPPORTED;
    if (operation == COPY_UMOV && (q ? size != 3 : size > 2))
        return NC_FP_UNSUPPORTED;
    if (operation == COPY_SMOV && (q ? size > 2 : size > 1))
        return NC_FP_UNSUPPORTED;

    nc_fp_result access = nc_fp_access_check(simd_present, el, cpacr_el1,
                                            higher_el_enabled);
    if (access != NC_FP_OK) return access;
    if ((bank->fpcr & ~UINT32_C(0x00c00000)) ||
        (bank->fpsr & ~(uint32_t)NC_FP_STATUS_MASK)) return NC_FP_INVALID;
    uintptr_t bp = (uintptr_t)bank, xp = (uintptr_t)x;
    if (bp <= xp ? xp - bp < sizeof(*bank) : bp - xp < 31 * sizeof(*x))
        return NC_FP_INVALID;

    unsigned dst = instruction & 31u, src = (instruction >> 5) & 31u;
    unsigned lane = imm5 >> (size + 1);
    uint64_t value;
    if (operation == COPY_DUP_GENERAL || operation == COPY_INS_GENERAL)
        value = src == 31 ? 0 : x[src] & lane_mask(bits);
    else if (operation == COPY_INS_ELEMENT)
        value = read_lane(bank, src, ((instruction >> 11) & 15u) >> size, bits);
    else value = read_lane(bank, src, lane, bits);

    if (operation == COPY_DUP_ELEMENT || operation == COPY_DUP_GENERAL) {
        uint64_t repeated = 0;
        for (unsigned shift = 0; shift < 64; shift += bits)
            repeated |= value << shift;
        bank->v[dst][0] = repeated;
        bank->v[dst][1] = q ? repeated : 0;
    } else if (operation == COPY_INS_GENERAL || operation == COPY_INS_ELEMENT) {
        write_lane(bank, dst, lane, bits, value);
    } else {
        if (operation == COPY_SMOV) {
            uint64_t sign = UINT64_C(1) << (bits - 1);
            value = (value ^ sign) - sign;
        }
        if (dst != 31) x[dst] = q ? value : (uint32_t)value;
    }
    return NC_FP_OK;
}
