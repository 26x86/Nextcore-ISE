/* SPDX-License-Identifier: BSD-4-Clause
 * x86 EFI bridge to the actual native-code AArch64 translator.
 */
#include "boot_jit.h"

_Static_assert(sizeof(vf_boot_result)==64,"Rust/C boot result ABI");
_Static_assert(sizeof(vf_pauth_context)==368,"Rust/C PAC state ABI");
_Static_assert(sizeof(vf_boot_options_v2)==64,"Rust/C platform options ABI");
_Static_assert(sizeof(vf_boot_result_v2)==128,"Rust/C platform result ABI");

static uint32_t decoded_fault_instruction(const vf_cpu *cpu,int status) {
    switch(status) {
    case VF_BAD_INSTRUCTION:
    case VF_UNDEFINED_INSTRUCTION:
    case VF_PRIVILEGE_FAULT:
    case VF_DATA_FAULT:
    case VF_TRANSLATION_FAULT:
    case VF_PERMISSION_FAULT:
    case VF_ALIGNMENT_FAULT:
    case VF_SP_ALIGNMENT_FAULT:
    case VF_SYSTEM_REGISTER_TRAP:
    case VF_DATA_ABORT:
        return cpu->instruction;
    default:
        /* No decoded faulting instruction exists for fetch failures,
         * interrupts, successful exits, exhausted budgets or host failures. */
        return 0;
    }
}

int vf_boot_run(uint8_t *ram,size_t ram_size,uint64_t base,uint64_t entry,
                uint64_t args,uint64_t stack,uint8_t *code,size_t code_bytes,
                uint64_t budget,vf_protect protect,void *opaque,
                vf_boot_result *result) {
    return vf_boot_run_with_pauth(ram,ram_size,base,entry,args,stack,code,
                                  code_bytes,budget,protect,opaque,0,result);
}

int vf_boot_run_with_pauth(uint8_t *ram,size_t ram_size,uint64_t base,uint64_t entry,
                uint64_t args,uint64_t stack,uint8_t *code,size_t code_bytes,
                uint64_t budget,vf_protect protect,void *opaque,vf_pauth_step pauth,
                vf_boot_result *result) {
    const uint64_t registers[4]={args,0,0,0};
    return vf_boot_run_with_registers(ram,ram_size,base,entry,args,stack,code,
                  code_bytes,budget,protect,opaque,registers,pauth,result);
}

int vf_boot_run_with_registers(uint8_t *ram,size_t ram_size,uint64_t base,uint64_t entry,
                uint64_t args,uint64_t stack,uint8_t *code,size_t code_bytes,
                uint64_t budget,vf_protect protect,void *opaque,
                const uint64_t registers[4],vf_pauth_step pauth,
                vf_boot_result *result) {
    if(!result)return VF_DATA_FAULT;
    vf_boot_result_v2 extended;
    int status=vf_boot_run_v2(ram,ram_size,base,entry,args,stack,code,code_bytes,
                     budget,protect,opaque,registers,pauth,0,&extended);
    *result=extended.base;return status;
}

int vf_boot_run_v2(uint8_t *ram,size_t ram_size,uint64_t base,uint64_t entry,
                uint64_t args,uint64_t stack,uint8_t *code,size_t code_bytes,
                uint64_t budget,vf_protect protect,void *opaque,
                const uint64_t registers[4],vf_pauth_step pauth,
                const vf_boot_options_v2 *options,vf_boot_result_v2 *extended) {
    if(!extended)return VF_DATA_FAULT;
    *extended=(vf_boot_result_v2){0};
    vf_boot_result *result=&extended->base;
    result->pc=entry;result->x0=args;
    uintptr_t r=(uintptr_t)ram,c=(uintptr_t)code;
    if(!ram || !code || code_bytes<64 || !protect || !registers ||
       ram_size>UINTPTR_MAX-r || code_bytes>UINTPTR_MAX-c ||
       (r<c+code_bytes && c<r+ram_size))
        return result->status=VF_DATA_FAULT;
    vf_cpu cpu;
    vf_cpu_reset(&cpu,VF_EL1);
    vf_code buffer={code,code_bytes,0};
    int status=vf_run_boot_v2(&cpu,ram,ram_size,base,entry,args,stack,
                           &buffer,budget,protect,opaque,registers,pauth,options);
    vf_boot_snapshot(&cpu,status,extended);return status;
}
void vf_boot_snapshot(const vf_cpu *state,int status,vf_boot_result_v2 *extended) {
    const vf_cpu cpu=*state;
    vf_boot_result *result=&extended->base;
    result->status=(uint32_t)status;
    result->fault_instruction=decoded_fault_instruction(&cpu,status);
    result->retired=cpu.retired;result->pc=cpu.pc;
    result->x0=cpu.x[0];result->x1=cpu.x[1];result->x2=cpu.x[2];result->x3=cpu.x[3];
    result->compiled_blocks=cpu.compiled_blocks;
    extended->platform_override=cpu.platform_override;
    extended->pending_lines=cpu.irq_level|(cpu.fiq_level<<1);
    extended->platform_profile=cpu.platform_profile;
    extended->elr=cpu.elr_el[VF_EL1];extended->spsr=cpu.spsr_el[VF_EL1];
    extended->exception_vector=cpu.exception_vector;extended->esr=cpu.esr_el[VF_EL1];
    extended->pstate=cpu.pstate;extended->sp=cpu.sp;
}
