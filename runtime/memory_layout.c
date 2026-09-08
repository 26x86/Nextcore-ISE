/* SPDX-License-Identifier: BSD-4-Clause; actual C layout exported to Rust tests. */
#include "memory_boot.h"
#define O(t,f) offsetof(t,f)
size_t vf_memory_layout(uint64_t *out,size_t capacity) {
    const uint64_t values[]={
        sizeof(vf_memory_request_v1),_Alignof(vf_memory_request_v1),
        O(vf_memory_request_v1,abi_version),O(vf_memory_request_v1,struct_size),O(vf_memory_request_v1,operation),O(vf_memory_request_v1,flags),
        O(vf_memory_request_v1,pc),O(vf_memory_request_v1,address),O(vf_memory_request_v1,value0),O(vf_memory_request_v1,value1),
        O(vf_memory_request_v1,sctlr),O(vf_memory_request_v1,epoch),O(vf_memory_request_v1,width),O(vf_memory_request_v1,count),
        O(vf_memory_request_v1,current_el),O(vf_memory_request_v1,reserved),
        sizeof(vf_memory_reply_v1),_Alignof(vf_memory_reply_v1),
        O(vf_memory_reply_v1,abi_version),O(vf_memory_reply_v1,struct_size),O(vf_memory_reply_v1,result),O(vf_memory_reply_v1,fault),
        O(vf_memory_reply_v1,value0),O(vf_memory_reply_v1,value1),O(vf_memory_reply_v1,address),O(vf_memory_reply_v1,esr),
        O(vf_memory_reply_v1,epoch),O(vf_memory_reply_v1,reserved),
        sizeof(vf_memory_run_result_v1),_Alignof(vf_memory_run_result_v1),
        O(vf_memory_run_result_v1,abi_version),O(vf_memory_run_result_v1,struct_size),O(vf_memory_run_result_v1,provider_status),O(vf_memory_run_result_v1,reserved0),
        O(vf_memory_run_result_v1,execution),O(vf_memory_run_result_v1,guest_far),O(vf_memory_run_result_v1,last_address),
        O(vf_memory_run_result_v1,fetch_requests),O(vf_memory_run_result_v1,data_requests),O(vf_memory_run_result_v1,completed_data_operations),O(vf_memory_run_result_v1,reserved1)};
    size_t length=sizeof(values)/sizeof(values[0]);
    if(out && capacity>=length)for(size_t i=0;i<length;i++)out[i]=values[i];
    return length;
}
static int calls;
static int protect_unused(void*p,size_t n,int x,void*o){(void)p;(void)n;(void)x;(void)o;calls++;return 0;}
static int32_t memory_unused(void*o,const vf_memory_request_v1*r,vf_memory_reply_v1*s){(void)o;(void)r;(void)s;calls++;return -1;}
int vf_memory_mmu_gate_probe(void) {
    vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.sctlr=1;cpu.pc=0x40000000;
    uint8_t buffer[4096];vf_code code={buffer,sizeof(buffer),0};vf_memory_run_result_v1 result={0};calls=0;
    int status=vf_run_memory_provider(&cpu,&code,1,protect_unused,0,memory_unused,&cpu,&result);
    return status==VF_SYSTEM_REGISTER_TRAP && calls==0 && cpu.retired==0 && cpu.pc==0x40000000 && cpu.exception_pending==0;
}
/* Test-only entry: architectural states inaccessible through EL1 boot options. */
int vf_memory_state_probe(unsigned mode,uint8_t *buffer,vf_protect protect,
    vf_memory_callback_v1 memory,void *owner,vf_memory_run_result_v1 *result) {
    vf_cpu cpu;vf_cpu_reset(&cpu,mode==0?VF_EL0:VF_EL1);
    cpu.pc=0x40000000;cpu.guest_ram_base=cpu.pc;cpu.sp=0x40000088;
    cpu.x[0]=11;cpu.x[1]=12;cpu.x[3]=0x40000081;
    cpu.sctlr=mode==0?0:8;
    cpu.pstate=mode==0?0x3c0:0x3c5;
    vf_code code={buffer,4096,0};*result=(vf_memory_run_result_v1){0};
    result->abi_version=1;result->struct_size=sizeof(*result);
    int status=vf_run_memory_provider(&cpu,&code,8,protect,0,memory,owner,result);
    vf_boot_snapshot(&cpu,status,&result->execution);result->guest_far=cpu.far_el[VF_EL1];
    return status;
}
