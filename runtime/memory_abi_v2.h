/* SPDX-License-Identifier: BSD-4-Clause */
#ifndef NEXTCORE_MEMORY_ABI_V2_H
#define NEXTCORE_MEMORY_ABI_V2_H
#include <stdint.h>
#include <stddef.h>
#define VF_MEMORY_V2_VERSION 2u
#define VF_MEMORY_V2_FIXED_NC 1u
#define VF_MEMORY_V2_OK 0u
#define VF_MEMORY_V2_GUEST_FAULT 1u
#define VF_MEMORY_V2_UNSUPPORTED 2u
#define VF_MEMORY_V2_UNAVAILABLE 3u
#define VF_MEMORY_V2_INVALID_REQUEST 4u
#define VF_MEMORY_V2_PC_ALIGNMENT 1u
#define VF_MEMORY_V2_DATA_ALIGNMENT 2u
#define VF_MEMORY_V2_ADDRESS_SIZE 3u
#define VF_MEMORY_V2_TRANSLATION 4u
#define VF_MEMORY_V2_PERMISSION 5u
#define VF_MEMORY_V2_ACCESS_FLAG 6u
#define VF_MEMORY_V2_NO_LEVEL UINT32_MAX
#define VF_MEMORY_V2_INPUT 1u
#define VF_MEMORY_V2_WALK 2u
#define VF_MEMORY_V2_LEAF 3u
#define VF_MEMORY_V2_CACHED_LEAF 4u
#define VF_MEMORY_V2_HAS_DESCRIPTOR 1u
#define VF_MEMORY_V2_HAS_OUTPUT 2u
typedef struct {
    uint32_t abi_version,struct_size,profile,reserved;
    uint64_t sctlr,ttbr0,ttbr1,tcr,mair,hcr,scr,epoch;
} vf_memory_controls_v2;
typedef struct {
    uint32_t abi_version,struct_size,operation,flags,width,count,current_el,reserved0;
    uint64_t pc,address,value0,value1,pstate;
    vf_memory_controls_v2 controls;
    uint64_t reserved1;
} vf_memory_request_v2;
typedef struct {
    uint32_t abi_version,struct_size,result,fault,level,context,fsc,metadata_flags;
    uint64_t value0,value1,address,esr,epoch,descriptor_pa,output_pa,reserved[5];
} vf_memory_reply_v2;
typedef int32_t (*vf_memory_callback_v2)(void *,const vf_memory_request_v2 *,vf_memory_reply_v2 *);
_Static_assert(sizeof(vf_memory_controls_v2)==80,"memory controls v2");
_Static_assert(sizeof(vf_memory_request_v2)==160,"memory request v2");
_Static_assert(sizeof(vf_memory_reply_v2)==128,"memory reply v2");
_Static_assert(offsetof(vf_memory_request_v2,controls)==72,"memory controls offset");
_Static_assert(offsetof(vf_memory_reply_v2,reserved)==88,"memory reply reserve");
#endif
