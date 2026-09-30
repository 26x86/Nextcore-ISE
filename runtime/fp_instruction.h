/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 service. */
#ifndef NEXTCORE_FP_INSTRUCTION_H
#define NEXTCORE_FP_INSTRUCTION_H
#include "fp_guest.h"

/* Standalone instruction semantics only: no CPU profile, access check,
 * exception routing, memory, PC or retirement. Never called by the guest
 * dispatcher while its PFR0 reports absent FP/Advanced SIMD.
 *
 * Supports only FADD/FDIV S and raw FMOV S,W / W,S. FPCR is RMode-only;
 * FPSR represents IOC/DZC/OFC/UFC/IXC. All other instructions return
 * UNSUPPORTED. Invalid pointers or supported instructions with malformed
 * state return INVALID. Rejection leaves both arrays unchanged.
 *
 * bank and x must be disjoint valid storage. x contains X0..X30 only;
 * encoded W31 is WZR. S31 is an ordinary bank register. Writes to S clear
 * its upper 96 bits; writes to W zero-extend into X. Raw moves preserve
 * FPSR and every NaN payload bit. */
nc_fp_result nc_fp_execute32(nc_fp_bank *bank, uint64_t x[31],
                              uint32_t instruction);
#endif
