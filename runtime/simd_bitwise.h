/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 service. */
#ifndef NEXTCORE_SIMD_BITWISE_H
#define NEXTCORE_SIMD_BITWISE_H
#include "fp_guest.h"

/* Standalone AND/BIC/ORR/ORN/EOR/BSL/BIT/BIF Vd.8B/16B,Vn.8B/16B,Vm.8B/16B.
 * No guest profile, dispatch, PC, retirement, memory or exception routing.
 * simd_present is an explicit service input, never a guest feature switch.
 * Access uses the bounded EL0/EL1 CPACR classifier. Unsupported words reject
 * before access checks; absent SIMD is UNDEFINED before bank validation.
 * Represented FPCR/FPSR state is required and preserved.
 * All V selectors, including 31, name real registers. Sources and original
 * destination mask/value are captured before writes. The 8B form clears
 * the upper 64 bits; 16B writes both halves.
 * BSL selects Vn/Vm using original Vd; BIT inserts Vn under Vm; BIF under ~Vm.
 * Rejection leaves the bank unchanged. Null bank is INVALID. */
nc_fp_result nc_simd_execute_bitwise(nc_fp_bank *bank, uint32_t instruction,
                                    unsigned simd_present, unsigned el,
                                    uint64_t cpacr_el1, unsigned higher_el_enabled);

/* Compatibility entry: accepts only EOR encodings, with the same checks. */
nc_fp_result nc_simd_execute_eor(nc_fp_bank *bank, uint32_t instruction,
                                unsigned simd_present, unsigned el,
                                uint64_t cpacr_el1, unsigned higher_el_enabled);
#endif
