/* SPDX-License-Identifier: BSD-4-Clause; independently authored FP state. */
#ifndef NEXTCORE_FP_GUEST_H
#define NEXTCORE_FP_GUEST_H
#include "fp_scalar.h"
#include <stdint.h>

/* Internal storage only: no guest instruction or feature admission. */
typedef struct {
    uint64_t v[32][2]; /* Low, then high 64 bits of each V register. */
    uint32_t fpcr;
    uint32_t fpsr;
} nc_fp_bank;

typedef enum {
    NC_FP_OK = 0,
    NC_FP_INVALID,
    NC_FP_UNDEFINED,
    NC_FP_TRAP_EL1,
    NC_FP_UNSUPPORTED
} nc_fp_result;

/* Null pointers, out-of-range indices and unrepresented FPCR/FPSR bits are
 * INVALID. Rejection leaves the bank and any supplied output unchanged. */
nc_fp_result nc_fp_bank_reset(nc_fp_bank *bank);
nc_fp_result nc_fp_bank_get_s(const nc_fp_bank *bank, unsigned reg, uint32_t *out);
nc_fp_result nc_fp_bank_get_d(const nc_fp_bank *bank, unsigned reg, uint64_t *out);
nc_fp_result nc_fp_bank_get_q(const nc_fp_bank *bank, unsigned reg, uint64_t out[2]);
nc_fp_result nc_fp_bank_write_s(nc_fp_bank *bank, unsigned reg, uint32_t bits);
nc_fp_result nc_fp_bank_write_d(nc_fp_bank *bank, unsigned reg, uint64_t bits);
nc_fp_result nc_fp_bank_write_q(nc_fp_bank *bank, unsigned reg,
                                 uint64_t low, uint64_t high);
/* Only RMode [23:22] and IOC/DZC/OFC/UFC/IXC [4:0] are represented.
 * FPSR writes replace cumulative flags, including explicit clearing. */
nc_fp_result nc_fp_bank_write_fpcr(nc_fp_bank *bank, uint32_t bits);
nc_fp_result nc_fp_bank_write_fpsr(nc_fp_bank *bank, uint32_t bits);
/* Checked binary32 services capture both S operands before writing the
 * destination, clear its upper 96 bits and accumulate FPSR without FPCR changes. */
nc_fp_result nc_fp_bank_add32(nc_fp_bank *bank, unsigned dst,
                              unsigned left, unsigned right);
nc_fp_result nc_fp_bank_div32(nc_fp_bank *bank, unsigned dst,
                              unsigned left, unsigned right);

/* Pure classifier for standalone service inputs, never a guest profile switch.
 * Boolean inputs must be 0/1 and EL must be 0..3; malformed inputs are INVALID.
 * Absent FP is UNDEFINED before inspecting controls. Present FP supports only
 * EL0/EL1, higher_el_enabled=0, and CPACR_EL1.FPEN [21:20]; other contexts are
 * UNSUPPORTED. TRAP_EL1 denotes EC 0x07; CPU exception routing is not implemented. */
nc_fp_result nc_fp_access_check(unsigned fp_present, unsigned el,
                                uint64_t cpacr_el1, unsigned higher_el_enabled);
#endif
