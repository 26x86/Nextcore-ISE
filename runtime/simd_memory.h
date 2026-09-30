/* SPDX-License-Identifier: BSD-4-Clause; independently authored Q memory. */
#ifndef NEXTCORE_SIMD_MEMORY_H
#define NEXTCORE_SIMD_MEMORY_H
#include "fp_guest.h"
#include <stddef.h>

typedef struct {
    uint64_t x[31];
    uint64_t sp;
} nc_simd_gprs;

/* Owned Normal-memory view, disjoint from bank, GPRs and this descriptor.
 * length must be nonzero; neither host nor guest mapping may wrap.
 * Boolean controls must be 0/1. Only big_endian=0 is implemented.
 * Alignment controls are explicit caller assumptions, not decoded SCTLR.
 * No MMU, MMIO, CPA2 pointer checks, tagging, partial bus transfers or
 * exception routing. */
typedef struct {
    uint8_t *bytes;
    uint64_t guest_base;
    size_t length;
    unsigned readable, writable;
    unsigned sp_alignment, data_alignment, big_endian;
} nc_simd_memory;

typedef enum {
    NC_SIMD_MEM_OK = 0,
    NC_SIMD_MEM_INVALID,
    NC_SIMD_MEM_UNDEFINED,
    NC_SIMD_MEM_TRAP_EL1,
    NC_SIMD_MEM_UNSUPPORTED,
    NC_SIMD_MEM_SP_ALIGNMENT,
    NC_SIMD_MEM_DATA_ALIGNMENT,
    NC_SIMD_MEM_DATA_ABORT
} nc_simd_mem_result;

/* Standalone 128-bit LDR/STR unsigned/pre/post immediate and LDUR/STUR.
 * No guest dispatch, profile, PC or retirement. Null state is INVALID;
 * exact decode precedes bounded FP access, which precedes bank/view checks.
 * Base 31 is SP; vector 31 is a real register. SP alignment uses the old SP.
 * Address addition wraps at 64 bits, but the complete transfer may not wrap.
 * Permissions and the entire 16-byte span are checked before any data change.
 * Every rejection/fault preserves bank, GPR/SP state and backing bytes.
 * Success preserves unrelated registers and FPCR/FPSR; writeback is last.
 * FP presence/access inputs have the same scope as nc_fp_access_check. */
nc_simd_mem_result nc_simd_execute_q_memory(
    nc_fp_bank *bank, nc_simd_gprs *gprs, const nc_simd_memory *memory,
    uint32_t instruction, unsigned fp_present, unsigned el,
    uint64_t cpacr_el1, unsigned higher_el_enabled);
#endif
