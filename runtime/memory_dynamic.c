/* SPDX-License-Identifier: BSD-4-Clause */
#include "memory_dynamic.h"
static int span(uintptr_t p,size_t n){return p && n<=UINTPTR_MAX-p;}
static int overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn){return a<b+bn && b<a+an;}
int vf_boot_run_memory_dynamic(uint64_t base,uint64_t size,uint64_t entry,uint64_t args,uint64_t stack,
    uint8_t *code,size_t code_bytes,uint64_t budget,vf_protect protect,void *protect_opaque,
    const uint64_t initial[4],const vf_boot_options_v2 *options,const vf_memory_controls_v2 *controls,
    vf_memory_callback_v2 memory,vf_dynamic_callback control_callback,void *owner,vf_memory_run_result_dynamic *result) {
    uintptr_t r=(uintptr_t)result,c=(uintptr_t)code,i=(uintptr_t)initial,o=(uintptr_t)options,s=(uintptr_t)controls;
    if(!span(r,sizeof(*result)) || r%_Alignof(vf_memory_run_result_dynamic))return VF_DATA_FAULT;
    if((span(c,code_bytes)&&overlap(r,sizeof(*result),c,code_bytes)) ||
       (span(i,32)&&overlap(r,sizeof(*result),i,32)) ||
       (options&&span(o,sizeof(*options))&&overlap(r,sizeof(*result),o,sizeof(*options))) ||
       (span(s,sizeof(*controls))&&overlap(r,sizeof(*result),s,sizeof(*controls))))return VF_DATA_FAULT;
    *result=(vf_memory_run_result_dynamic){0};result->memory.base.abi_version=3;result->memory.base.struct_size=sizeof(*result);
    result->memory.base.execution.base.pc=entry;result->memory.base.execution.base.x0=args;
    result->memory.base.execution.base.status=VF_DATA_FAULT;result->memory.base.provider_status=VF_PROVIDER_INVALID_REQUEST;
    result->memory.last_reply=(vf_memory_reply_v2){.abi_version=3,.struct_size=128,.level=UINT32_MAX};
    if(!span(c,code_bytes) || code_bytes<64 || !span(i,32) || i%_Alignof(uint64_t) ||
       !span(s,sizeof(*controls)) || s%_Alignof(vf_memory_controls_v2) ||
       (options&&(!span(o,sizeof(*options)) || o%_Alignof(vf_boot_options_v2))) ||
       !protect || !memory || !control_callback || !owner || ((uintptr_t)owner&7) || !budget || size<8 || base>UINT64_MAX-size || base>=(UINT64_C(1)<<48) || size>(UINT64_C(1)<<48)-base ||
       overlap(c,code_bytes,i,32) || overlap(c,code_bytes,s,sizeof(*controls)) ||
       (options&&overlap(c,code_bytes,o,sizeof(*options))))return VF_DATA_FAULT;
    const vf_memory_controls_v2 control=*controls;
    if(!vf_dynamic_controls_valid(&control) || (control.sctlr&1) || control.epoch!=1)return VF_DATA_FAULT;
    const vf_boot_options_v2 defaults={2,sizeof(vf_boot_options_v2),0,0,0,0x3c5,0,0,0,0};
    vf_boot_options_v2 config=options?*options:defaults;
    unsigned mode=config.initial_pstate&15;
    if(config.abi_version!=2 || config.struct_size!=sizeof(config) || config.flags || config.reserved ||
       config.irq_level>1 || config.fiq_level>1 || (mode!=4&&mode!=5) || (config.initial_pstate&0xc0)!=0xc0 ||
       (config.initial_pstate&~UINT64_C(0xf00003cf)) || (config.vbar&0x7ff))return VF_DATA_FAULT;
    /* The caller binds one exclusively borrowed service to this complete seed.
     * M=0 fetch uses physical addresses; later fetch uses the acknowledged E. */
    vf_cpu cpu;vf_cpu_reset(&cpu,mode==0?VF_EL0:VF_EL1);
    if(vf_cpu_configure_platform(&cpu,config.platform_profile,config.initial_override))return VF_DATA_FAULT;
    cpu.pc=entry;for(unsigned n=0;n<4;n++)cpu.x[n]=initial[n];
    cpu.sp=stack;cpu.sp_el[VF_EL0]=stack;cpu.sp_el[VF_EL1]=stack;cpu.pstate=config.initial_pstate;
    cpu.vbar_el[VF_EL1]=config.vbar;cpu.guest_ram_base=base;
    cpu.sctlr=control.sctlr;cpu.ttbr0=control.ttbr0;cpu.ttbr1=control.ttbr1;cpu.tcr=control.tcr;
    cpu.mair=control.mair;cpu.hcr_el2=control.hcr;cpu.scr_el3=control.scr;
    vf_cpu_set_interrupt_lines(&cpu,(unsigned)config.irq_level,(unsigned)config.fiq_level);
    result->memory.base.provider_status=VF_PROVIDER_OK;
    vf_code buffer={code,code_bytes,0};
    int status=vf_run_memory_provider_dynamic(&cpu,&buffer,budget,protect,protect_opaque,&control,memory,control_callback,owner,result);
    vf_boot_snapshot(&cpu,status,&result->memory.base.execution);result->memory.base.guest_far=cpu.far_el[VF_EL1];
    if(result->memory.base.provider_status)result->memory.base.execution.base.fault_instruction=0;
    return status;
}
