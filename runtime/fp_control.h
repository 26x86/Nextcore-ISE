/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 service. */
#ifndef NEXTCORE_FP_CONTROL_H
#define NEXTCORE_FP_CONTROL_H
#include "fp_guest.h"

/* Standalone service only; never a guest profile or dispatcher.
 * Exact FPCR/FPSR MRS/MSR use X0..X30 and zero/discard for Rt=31.
 * Storage must be valid and disjoint. Access uses nc_fp_access_check.
 * FPCR represents RMode; unsupported exception enables [15,12:8] are RAZ/WI.
 * FPSR represents only cumulative IOC/DZC/OFC/UFC/IXC. MSR ignores upper
 * 32 bits and rejects all other lower bits. Malformed bank state is INVALID.
 * Rejection changes neither bank nor GPRs. No PC/retirement/exception routing. */
nc_fp_result nc_fp_execute_control(nc_fp_bank *bank, uint64_t x[31],
                                   uint32_t word, unsigned fp_present,
                                   unsigned el, uint64_t cpacr_el1,
                                   unsigned higher_el_enabled);
#endif
