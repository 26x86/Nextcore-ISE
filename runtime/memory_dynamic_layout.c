/* SPDX-License-Identifier: BSD-4-Clause */
#include "memory_dynamic.h"
size_t vf_dynamic_layout(uint64_t *out,size_t capacity) {
    const uint64_t values[]={sizeof(vf_cpu),sizeof(vf_memory_controls_v2),sizeof(vf_memory_request_v2),sizeof(vf_memory_reply_v2),
        sizeof(vf_dynamic_snapshot),sizeof(vf_dynamic_request),sizeof(vf_dynamic_reply),sizeof(vf_memory_run_result_dynamic),
        _Alignof(vf_dynamic_request),_Alignof(vf_dynamic_reply),_Alignof(vf_memory_run_result_dynamic),
        offsetof(vf_dynamic_request,pc),offsetof(vf_dynamic_request,before),offsetof(vf_dynamic_request,candidate),
        offsetof(vf_dynamic_reply,token),offsetof(vf_dynamic_reply,architectural),offsetof(vf_dynamic_reply,effective),
        offsetof(vf_memory_run_result_dynamic,final_control)};
    size_t n=sizeof(values)/sizeof(values[0]);if(!out||capacity<n)return n;
    for(size_t i=0;i<n;i++)out[i]=values[i];return n;
}

int vf_dynamic_cpu_probe(uint8_t *code,size_t code_bytes,const vf_memory_controls_v2 *controls,
    vf_memory_callback_v2 memory,vf_dynamic_callback control,void *owner,vf_protect protect,
    uint64_t entry,uint64_t enable,uint64_t *observed,vf_memory_run_result_dynamic *result) {
    vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.pc=entry;cpu.x[0]=enable;cpu.pstate=0x3c5;
    cpu.sctlr=controls->sctlr;cpu.ttbr0=controls->ttbr0;cpu.ttbr1=controls->ttbr1;cpu.tcr=controls->tcr;
    cpu.mair=controls->mair;cpu.hcr_el2=controls->hcr;cpu.scr_el3=controls->scr;
    vf_code buffer={code,code_bytes,0};
    int status=vf_run_memory_provider_dynamic(&cpu,&buffer,16,protect,0,controls,memory,control,owner,result);
    observed[0]=cpu.sctlr;observed[1]=cpu.ttbr0;observed[2]=cpu.ttbr1;observed[3]=cpu.tcr;
    observed[4]=cpu.mair;observed[5]=cpu.pc;observed[6]=cpu.retired;observed[7]=cpu.esr_el[VF_EL1];
    return status;
}
