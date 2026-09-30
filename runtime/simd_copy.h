/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 service. */
#ifndef NEXTCORE_SIMD_COPY_H
#define NEXTCORE_SIMD_COPY_H
#include "fp_guest.h"

/* Checked vector DUP (general/element), INS (general/element), UMOV and SMOV.
 * bank and x must be valid, disjoint storage; x contains X0-X30 only.
 * Integer selector 31 is zero/discard, never SP; vector 31 is real.
 * Exact decode precedes bounded FP access, then bank validation.
 * DUP reads a full source register and clears the upper half for Q=0.
 * INS captures the source before writing exactly one destination lane.
 * UMOV/SMOV W writes zero-extend into X, including signed W results.
 * DUP-general immediate bits above the size selector and INS-element
 * source-immediate bits below size are ignored as specified by A64.
 * All failures preserve both arrays; success preserves FPCR/FPSR and
 * unrelated registers. No memory, profile, PC, retirement or exception route.
 * Presence/access have exactly the scope of nc_fp_access_check. */
nc_fp_result nc_simd_execute_copy(nc_fp_bank *bank, uint64_t x[31],
                                  uint32_t instruction, unsigned simd_present,
                                  unsigned el, uint64_t cpacr_el1,
                                  unsigned higher_el_enabled);
#endif
