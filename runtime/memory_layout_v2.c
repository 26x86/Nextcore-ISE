/* SPDX-License-Identifier: BSD-4-Clause */
#include "memory_boot_v2.h"
#define LAYOUT(T) sizeof(T),_Alignof(T)
#define FIELD(T,F) offsetof(T,F)
size_t vf_stage1_layout(uint64_t *out,size_t capacity) {
    const uint64_t values[]={
        LAYOUT(vf_memory_controls_v2),
        FIELD(vf_memory_controls_v2,abi_version),FIELD(vf_memory_controls_v2,struct_size),FIELD(vf_memory_controls_v2,profile),FIELD(vf_memory_controls_v2,reserved),
        FIELD(vf_memory_controls_v2,sctlr),FIELD(vf_memory_controls_v2,ttbr0),FIELD(vf_memory_controls_v2,ttbr1),FIELD(vf_memory_controls_v2,tcr),
        FIELD(vf_memory_controls_v2,mair),FIELD(vf_memory_controls_v2,hcr),FIELD(vf_memory_controls_v2,scr),FIELD(vf_memory_controls_v2,epoch),
        LAYOUT(vf_memory_request_v2),
        FIELD(vf_memory_request_v2,abi_version),FIELD(vf_memory_request_v2,struct_size),FIELD(vf_memory_request_v2,operation),FIELD(vf_memory_request_v2,flags),
        FIELD(vf_memory_request_v2,width),FIELD(vf_memory_request_v2,count),FIELD(vf_memory_request_v2,current_el),FIELD(vf_memory_request_v2,reserved0),
        FIELD(vf_memory_request_v2,pc),FIELD(vf_memory_request_v2,address),FIELD(vf_memory_request_v2,value0),FIELD(vf_memory_request_v2,value1),
        FIELD(vf_memory_request_v2,pstate),FIELD(vf_memory_request_v2,controls),FIELD(vf_memory_request_v2,reserved1),
        LAYOUT(vf_memory_reply_v2),
        FIELD(vf_memory_reply_v2,abi_version),FIELD(vf_memory_reply_v2,struct_size),FIELD(vf_memory_reply_v2,result),FIELD(vf_memory_reply_v2,fault),
        FIELD(vf_memory_reply_v2,level),FIELD(vf_memory_reply_v2,context),FIELD(vf_memory_reply_v2,fsc),FIELD(vf_memory_reply_v2,metadata_flags),
        FIELD(vf_memory_reply_v2,value0),FIELD(vf_memory_reply_v2,value1),FIELD(vf_memory_reply_v2,address),FIELD(vf_memory_reply_v2,esr),
        FIELD(vf_memory_reply_v2,epoch),FIELD(vf_memory_reply_v2,descriptor_pa),FIELD(vf_memory_reply_v2,output_pa),FIELD(vf_memory_reply_v2,reserved),
        LAYOUT(vf_memory_run_result_v2),FIELD(vf_memory_run_result_v2,base),FIELD(vf_memory_run_result_v2,last_reply),sizeof(vf_cpu)
    };
    size_t count=sizeof(values)/sizeof(values[0]);if(!out || capacity<count)return count;
    for(size_t n=0;n<count;n++)out[n]=values[n];return count;
}
int vf_stage1_controls_probe(const vf_memory_controls_v2 *controls) {return vf_memory_controls_valid_v2(controls);}
