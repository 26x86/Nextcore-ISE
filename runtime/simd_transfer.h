/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 service. */
#ifndef NEXTCORE_SIMD_TRANSFER_H
#define NEXTCORE_SIMD_TRANSFER_H

#include "fp_guest.h"

/* Standalone FMOV Dd,Xn / Xd,Dn / Vd.D[1],Xn / Xd,Vn.D[1].
 * fp_present describes FEAT_FP for all four forms, including the upper lane.
 * The bank and the 31-element X0..X30 array must be valid, disjoint storage.
 * X selector 31 reads zero or discards writes; V selector 31 is real storage.
 * Low-lane writes clear the upper lane; upper-lane writes retain the low lane.
 * Null storage is INVALID. Exact decode precedes the bounded EL0/EL1 access
 * classifier, which precedes represented FPCR/FPSR validation. All rejection
 * paths preserve both objects. Successful transfers preserve controls and
 * unrelated registers without floating-point conversion or exceptions.
 * This API does not change guest dispatch, features, PC, retirement or memory. */
nc_fp_result nc_simd_transfer_execute(nc_fp_bank *bank, uint64_t x[31],
                                      uint32_t instruction, unsigned fp_present,
                                      unsigned el, uint64_t cpacr_el1,
                                      unsigned higher_el_enabled);

#endif
