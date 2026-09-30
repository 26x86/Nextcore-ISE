/* SPDX-License-Identifier: BSD-4-Clause; independently authored Q memory. */
#include "simd_memory.h"

static int overlaps(const void *a, size_t as, const void *b, size_t bs) {
    uintptr_t ap = (uintptr_t)a, bp = (uintptr_t)b;
    return ap <= bp ? bp - ap < as : ap - bp < bs;
}

nc_simd_mem_result nc_simd_execute_q_memory(
    nc_fp_bank *bank, nc_simd_gprs *gprs, const nc_simd_memory *memory,
    uint32_t instruction, unsigned fp_present, unsigned el,
    uint64_t cpacr_el1, unsigned higher_el_enabled) {
    if (!bank || !gprs || !memory) return NC_SIMD_MEM_INVALID;
    uint64_t offset;
    unsigned mode = 0; /* 0: no writeback, 1: post, 3: pre. */
    if ((instruction & UINT32_C(0xff800000)) == UINT32_C(0x3d800000)) {
        offset = (uint64_t)((instruction >> 10) & 4095u) << 4;
    } else if ((instruction & UINT32_C(0xffa00000)) == UINT32_C(0x3c800000)) {
        mode = (instruction >> 10) & 3u;
        if (mode == 2) return NC_SIMD_MEM_UNSUPPORTED;
        unsigned imm = (instruction >> 12) & 511u;
        int signed_offset = (int)imm - ((imm & 256u) ? 512 : 0);
        offset = (uint64_t)(int64_t)signed_offset;
    } else {
        return NC_SIMD_MEM_UNSUPPORTED;
    }
    nc_fp_result access = nc_fp_access_check(fp_present, el, cpacr_el1,
                                            higher_el_enabled);
    if (access != NC_FP_OK) return (nc_simd_mem_result)access;
    if ((bank->fpcr & ~UINT32_C(0x00c00000)) ||
        (bank->fpsr & ~(uint32_t)NC_FP_STATUS_MASK)) return NC_SIMD_MEM_INVALID;
    if (!memory->bytes || !memory->length || memory->readable > 1 ||
        memory->writable > 1 || memory->sp_alignment > 1 ||
        memory->data_alignment > 1 || memory->big_endian > 1 ||
        memory->length - 1 > UINT64_MAX - memory->guest_base ||
        memory->length - 1 > UINTPTR_MAX - (uintptr_t)memory->bytes ||
        overlaps(bank, sizeof(*bank), gprs, sizeof(*gprs)) ||
        overlaps(bank, sizeof(*bank), memory, sizeof(*memory)) ||
        overlaps(gprs, sizeof(*gprs), memory, sizeof(*memory)) ||
        overlaps(memory->bytes, memory->length, bank, sizeof(*bank)) ||
        overlaps(memory->bytes, memory->length, gprs, sizeof(*gprs)) ||
        overlaps(memory->bytes, memory->length, memory, sizeof(*memory)))
        return NC_SIMD_MEM_INVALID;
    if (memory->big_endian) return NC_SIMD_MEM_UNSUPPORTED;
    unsigned rn = (instruction >> 5) & 31u, rt = instruction & 31u;
    unsigned load = (instruction >> 22) & 1u;
    uint64_t base = rn == 31 ? gprs->sp : gprs->x[rn];
    if (rn == 31 && memory->sp_alignment && (base & 15u))
        return NC_SIMD_MEM_SP_ALIGNMENT;
    uint64_t updated = base + offset;
    uint64_t address = mode == 1 ? base : updated;
    if (memory->data_alignment && (address & 15u))
        return NC_SIMD_MEM_DATA_ALIGNMENT;
    if ((load ? !memory->readable : !memory->writable) ||
        address > UINT64_MAX - 15u || address < memory->guest_base)
        return NC_SIMD_MEM_DATA_ABORT;
    uint64_t index = address - memory->guest_base;
    if (memory->length < 16 || index > memory->length - 16)
        return NC_SIMD_MEM_DATA_ABORT;
    uint8_t *bytes = memory->bytes + (size_t)index;
    if (load) {
        uint64_t low = 0, high = 0;
        for (unsigned i = 0; i < 8; ++i) {
            low |= (uint64_t)bytes[i] << (8u * i);
            high |= (uint64_t)bytes[i + 8] << (8u * i);
        }
        bank->v[rt][0] = low;
        bank->v[rt][1] = high;
    } else {
        uint64_t low = bank->v[rt][0], high = bank->v[rt][1];
        for (unsigned i = 0; i < 8; ++i) {
            bytes[i] = (uint8_t)(low >> (8u * i));
            bytes[i + 8] = (uint8_t)(high >> (8u * i));
        }
    }
    if (mode) {
        if (rn == 31) gprs->sp = updated;
        else gprs->x[rn] = updated;
    }
    return NC_SIMD_MEM_OK;
}
