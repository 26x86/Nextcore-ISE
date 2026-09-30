/* SPDX-License-Identifier: BSD-4-Clause; bounded research FP execution. */
#include "fp_execution.h"
#include "fp_control.h"
#include "fp_instruction.h"
#include "simd_bitwise.h"

enum fp_operation { FP_UNKNOWN, FP_CONTROL, FP_SCALAR32, FP_BITWISE };

static enum fp_operation supported_operation(uint32_t word) {
    switch (word & UINT32_C(0xffffffe0)) {
    case UINT32_C(0xd53b4400):
    case UINT32_C(0xd51b4400):
    case UINT32_C(0xd53b4420):
    case UINT32_C(0xd51b4420): return FP_CONTROL;
    }
    uint32_t binary = word & UINT32_C(0xffe0fc00);
    uint32_t move = word & UINT32_C(0xfffffc00);
    if (binary == UINT32_C(0x1e202800) ||
        binary == UINT32_C(0x1e201800) ||
        move == UINT32_C(0x1e270000) ||
        move == UINT32_C(0x1e260000)) return FP_SCALAR32;
    if ((word & UINT32_C(0x9f20fc00)) == UINT32_C(0x0e201c00))
        return FP_BITWISE;
    return FP_UNKNOWN;
}

int vf_fp_instruction_class(uint32_t word) {
    /* A64 top-level bits [28:25]=x111 own scalar FP and Advanced SIMD data.
     * Loads/stores have x1x0; bit 26 selects the SIMD/FP register space.
     * These broad spaces include forms whose exact legality is not modeled. */
    if ((word & UINT32_C(0x0e000000)) == UINT32_C(0x0e000000) ||
        (word & UINT32_C(0x0e000000)) == UINT32_C(0x0c000000)) return 1;
    return supported_operation(word) == FP_CONTROL;
}

int vf_fp_step(vf_cpu *cpu) {
    if (!cpu) return VF_IMPLEMENTATION_GAP;
    if (!cpu->fp_execution_profile) return VF_UNDEFINED_INSTRUCTION;
    if (cpu->fp_execution_profile != VF_FP_EXECUTION_PARTIAL ||
        !vf_cpu_state_valid(cpu) || cpu->exception_pending != VF_EXCEPTION_NONE)
        return VF_IMPLEMENTATION_GAP;
    enum fp_operation operation = supported_operation(cpu->instruction);
    if (operation == FP_UNKNOWN) return VF_IMPLEMENTATION_GAP;
    unsigned higher_el_enabled = cpu->hcr_el2 != 0 || cpu->scr_el3 != 0;
    nc_fp_result access = nc_fp_access_check(1,cpu->current_el,cpu->cpacr_el1,
                                            higher_el_enabled);
    if (access == NC_FP_TRAP_EL1) return VF_FP_ACCESS_TRAP;
    if (access != NC_FP_OK) return VF_IMPLEMENTATION_GAP;

    nc_fp_result result;
    switch (operation) {
    case FP_CONTROL:
        result = nc_fp_execute_control(&cpu->fp,cpu->x,cpu->instruction,1,
            cpu->current_el,cpu->cpacr_el1,higher_el_enabled);
        break;
    case FP_SCALAR32:
        result = nc_fp_execute32(&cpu->fp,cpu->x,cpu->instruction);
        break;
    case FP_BITWISE:
        result = nc_simd_execute_bitwise(&cpu->fp,cpu->instruction,1,
            cpu->current_el,cpu->cpacr_el1,higher_el_enabled);
        break;
    default: return VF_IMPLEMENTATION_GAP;
    }
    if (result != NC_FP_OK) return VF_IMPLEMENTATION_GAP;
    cpu->pc += 4;
    cpu->retired++;
    vf_cpu_advance_counter(cpu,1);
    return VF_NEXT;
}
