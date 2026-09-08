/* SPDX-License-Identifier: BSD-4-Clause */
#include "memory_boot.h"
_Static_assert(sizeof(vf_memory_request_v1)==80,"memory request size");
_Static_assert(sizeof(vf_memory_reply_v1)==80,"memory reply size");
_Static_assert(sizeof(vf_memory_run_result_v1)==192,"memory run result size");
static int span(uintptr_t p,size_t n){return p && n<=UINTPTR_MAX-p;}
static int overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn){return a<b+bn && b<a+an;}
int vf_boot_run_memory_v1(uint64_t base,uint64_t size,uint64_t entry,uint64_t args,uint64_t stack,
    uint8_t *code,size_t code_bytes,uint64_t budget,vf_protect protect,void *protect_opaque,
    const uint64_t initial[4],vf_pauth_step pauth,const vf_boot_options_v2 *options,
    vf_memory_callback_v1 memory,void *owner,vf_memory_run_result_v1 *result) {
    uintptr_t r=(uintptr_t)result,c=(uintptr_t)code,i=(uintptr_t)initial,o=(uintptr_t)options;
    if(!span(r,sizeof(*result)) || r%_Alignof(vf_memory_run_result_v1))return VF_DATA_FAULT;
    /* Reject known writable aliases before even clearing the result record. */
    if((span(c,code_bytes) && overlap(r,sizeof(*result),c,code_bytes)) ||
       (span(i,32) && overlap(r,sizeof(*result),i,32)) ||
       (options && span(o,sizeof(*options)) && overlap(r,sizeof(*result),o,sizeof(*options))))
        return VF_DATA_FAULT;
    *result=(vf_memory_run_result_v1){0};result->abi_version=1;result->struct_size=sizeof(*result);
    result->execution.base.pc=entry;result->execution.base.x0=args;
    result->execution.base.status=VF_DATA_FAULT;result->provider_status=VF_PROVIDER_INVALID_REQUEST;
    if(!span(c,code_bytes) || code_bytes<64 || !span(i,32) || i%_Alignof(uint64_t) ||
       (options && (!span(o,sizeof(*options)) || o%_Alignof(vf_boot_options_v2))) ||
       !protect || !memory || !owner || ((uintptr_t)owner&7) ||
       overlap(c,code_bytes,i,32) ||
       (options && overlap(c,code_bytes,o,sizeof(*options))))return VF_DATA_FAULT;
    vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);
    int status=vf_cpu_prepare_boot(&cpu,size,base,entry,args,stack,initial,pauth,options,budget);
    if(status!=VF_NEXT)return VF_DATA_FAULT;
    result->provider_status=VF_PROVIDER_OK;
    vf_code buffer={code,code_bytes,0};
    status=vf_run_memory_provider(&cpu,&buffer,budget,protect,protect_opaque,memory,owner,result);
    vf_boot_snapshot(&cpu,status,&result->execution);
    result->guest_far=cpu.far_el[VF_EL1];
    if(result->provider_status)result->execution.base.fault_instruction=0;
    return status;
}
