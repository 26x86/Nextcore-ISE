/* SPDX-License-Identifier: BSD-4-Clause */
#ifndef NEXTCORE_BOOT_JIT_H
#define NEXTCORE_BOOT_JIT_H
#include "jit.h"

/* Stable, pointer-free result layout shared with the Rust EFI adapter.
 * fault_instruction is nonzero only for a decoded synchronous instruction,
 * system-register or data fault; fetch failures, interrupts, HALT, BUDGET,
 * CODE_FULL, PROTECTION and invalid-argument exits carry zero. */
typedef struct {
    uint32_t status, fault_instruction;
    uint64_t retired, pc, x0, x1, x2, x3, compiled_blocks;
} vf_boot_result;
typedef struct {
    vf_boot_result base;
    uint64_t platform_override;
    uint32_t pending_lines,platform_profile;
    uint64_t elr,spsr,exception_vector,esr,pstate,sp;
} vf_boot_result_v2;
/* Internal common snapshot helper; preserves the existing public records. */
void vf_boot_snapshot(const vf_cpu *,int,vf_boot_result_v2 *);
int vf_boot_run_v2(uint8_t *ram,size_t ram_size,uint64_t ram_base,
                uint64_t entry,uint64_t args,uint64_t stack,
                uint8_t *code,size_t code_bytes,uint64_t budget,
                vf_protect protect,void *opaque,
                const uint64_t initial_x0_x3[4],vf_pauth_step pauth,
                const vf_boot_options_v2 *options,vf_boot_result_v2 *result);

int vf_boot_run(uint8_t *ram, size_t ram_size, uint64_t ram_base,
                uint64_t entry, uint64_t args, uint64_t stack,
                uint8_t *code, size_t code_bytes, uint64_t budget,
                vf_protect protect, void *opaque, vf_boot_result *result);
int vf_boot_run_with_pauth(uint8_t *ram, size_t ram_size, uint64_t ram_base,
                uint64_t entry, uint64_t args, uint64_t stack,
                uint8_t *code, size_t code_bytes, uint64_t budget,
                vf_protect protect, void *opaque, vf_pauth_step pauth,
                vf_boot_result *result);
/* Explicit register state for bounded diagnostics. This does not provision
 * an SPTM, select an entry protocol, or validate guest argument contents. */
int vf_boot_run_with_registers(uint8_t *ram, size_t ram_size, uint64_t ram_base,
                uint64_t entry, uint64_t args, uint64_t stack,
                uint8_t *code, size_t code_bytes, uint64_t budget,
                vf_protect protect, void *opaque,
                const uint64_t initial_x0_x3[4], vf_pauth_step pauth,
                vf_boot_result *result);
#endif
