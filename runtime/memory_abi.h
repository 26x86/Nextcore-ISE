/* SPDX-License-Identifier: BSD-4-Clause */
#ifndef NEXTCORE_MEMORY_ABI_H
#define NEXTCORE_MEMORY_ABI_H
#include <stdint.h>
#define VF_MEMORY_ABI_VERSION 1u
enum vf_memory_operation { VF_MEMORY_FETCH=1, VF_MEMORY_LOAD=2, VF_MEMORY_STORE=3 };
enum vf_memory_reply_status { VF_MEMORY_OK=0, VF_MEMORY_GUEST_FAULT=1,
    VF_MEMORY_UNSUPPORTED=2, VF_MEMORY_INVALID_REQUEST=3 };
enum vf_memory_fault { VF_MEMORY_NO_FAULT=0, VF_MEMORY_PC_ALIGNMENT=1, VF_MEMORY_DATA_ALIGNMENT=2 };
enum vf_memory_provider_status { VF_PROVIDER_OK=0, VF_PROVIDER_UNSUPPORTED=1,
    VF_PROVIDER_INVALID_REQUEST=2, VF_PROVIDER_INVALID_REPLY=3, VF_PROVIDER_CALLBACK_FAILURE=4 };
typedef struct {
    uint32_t abi_version,struct_size,operation,flags;
    uint64_t pc,address,value0,value1,sctlr,epoch;
    uint32_t width,count,current_el,reserved;
} vf_memory_request_v1;
typedef struct {
    uint32_t abi_version,struct_size,result,fault;
    uint64_t value0,value1,address,esr,epoch,reserved[3];
} vf_memory_reply_v1;
typedef int32_t (*vf_memory_callback_v1)(void *,const vf_memory_request_v1 *,vf_memory_reply_v1 *);
#endif
