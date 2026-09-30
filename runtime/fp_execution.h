/* SPDX-License-Identifier: BSD-4-Clause; bounded research FP execution. */
#ifndef NEXTCORE_FP_EXECUTION_H
#define NEXTCORE_FP_EXECUTION_H
#include "jit.h"

/* Broad A64 FP/Advanced SIMD data and memory spaces, plus exact FPCR/FPSR
 * transfers. This identifies dispatch ownership, not architectural validity:
 * unimplemented or reserved forms stop the research host with a coverage gap. */
int vf_fp_instruction_class(uint32_t instruction);

/* Uses cpu->instruction and the live software-owned FP/GPR banks. Only partial
 * profile one is admitted. Known forms check bounded CPACR access before bank
 * validation; unknown forms report a coverage gap before inventing a trap.
 * VF_NEXT commits PC, retirement and counters once. Every other result leaves
 * the entire CPU unchanged. The caller records architectural access traps.
 * No guest memory, host floating point, or translated SIMD memory is used. */
int vf_fp_step(vf_cpu *cpu);
#endif
