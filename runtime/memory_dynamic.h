/* SPDX-License-Identifier: BSD-4-Clause */
#ifndef NEXTCORE_MEMORY_DYNAMIC_H
#define NEXTCORE_MEMORY_DYNAMIC_H
#include "memory_boot_v2.h"
#define VF_DYNAMIC_DATA_VERSION 3u
#define VF_DYNAMIC_PROFILE 2u
#define VF_DYNAMIC_VERSION 1u
#define VF_DYNAMIC_PREPARE 1u
#define VF_DYNAMIC_COMMIT 2u
#define VF_DYNAMIC_CANCEL 3u
#define VF_DYNAMIC_WRITE 1u
#define VF_DYNAMIC_ISB 2u
#define VF_DYNAMIC_DSB 3u
#define VF_DYNAMIC_TLBI 4u
#define VF_DYNAMIC_SCTLR 1u
#define VF_DYNAMIC_TTBR0 2u
#define VF_DYNAMIC_TTBR1 3u
#define VF_DYNAMIC_TCR 4u
#define VF_DYNAMIC_MAIR 5u
#define VF_DYNAMIC_OK 0u
#define VF_DYNAMIC_UNSUPPORTED 1u
#define VF_DYNAMIC_INVALID 2u
#define VF_DYNAMIC_UNAVAILABLE 3u
#define VF_DYNAMIC_EMPTY 0u
#define VF_DYNAMIC_PROPOSED 1u
#define VF_DYNAMIC_KNOWN 2u
#define VF_DYNAMIC_UNCERTAIN 3u
#define VF_DYNAMIC_ISB_WORD UINT32_C(0xd5033fdf)
#define VF_DYNAMIC_DSB_WORD UINT32_C(0xd5033f9f)
#define VF_DYNAMIC_TLBI_WORD UINT32_C(0xd508871f)
typedef struct {
    uint64_t sctlr,ttbr0,ttbr1,tcr,mair,hcr,scr,reserved;
} vf_dynamic_snapshot;
typedef struct {
    uint32_t abi_version,struct_size,phase,operation,selector,current_el,flags,reserved;
    uint64_t pc,operand,revision,epoch;
    vf_dynamic_snapshot before,candidate;
} vf_dynamic_request;
typedef struct {
    uint32_t abi_version,struct_size,result,detail,phase,operation,selector,state_tag;
    uint64_t token,revision,epoch,invalidations;
    vf_dynamic_snapshot architectural,effective;
} vf_dynamic_reply;
typedef int (*vf_dynamic_callback)(void *,const vf_dynamic_request *,vf_dynamic_reply *);
typedef struct {
    vf_memory_run_result_v2 memory;
    vf_dynamic_reply final_control;
} vf_memory_run_result_dynamic;
_Static_assert(sizeof(vf_dynamic_snapshot)==64,"dynamic snapshot");
_Static_assert(sizeof(vf_dynamic_request)==192,"dynamic request");
_Static_assert(sizeof(vf_dynamic_reply)==192,"dynamic reply");
_Static_assert(sizeof(vf_memory_run_result_dynamic)==512,"dynamic result");
_Static_assert(offsetof(vf_dynamic_request,before)==64,"dynamic before");
_Static_assert(offsetof(vf_dynamic_request,candidate)==128,"dynamic candidate");
_Static_assert(offsetof(vf_dynamic_reply,architectural)==64,"dynamic architectural");
_Static_assert(offsetof(vf_dynamic_reply,effective)==128,"dynamic effective");
_Static_assert(offsetof(vf_memory_run_result_dynamic,final_control)==320,"dynamic final");
static inline int vf_dynamic_controls_valid(const vf_memory_controls_v2 *c) {
    if(!c || c->abi_version!=3 || c->struct_size!=80 || c->profile!=2 || c->reserved || !c->epoch)return 0;
    vf_memory_controls_v2 mapped=*c;
    mapped.abi_version=2;mapped.profile=1;mapped.epoch=1;mapped.sctlr|=1;
    return vf_memory_controls_valid_v2(&mapped);
}
int vf_boot_run_memory_dynamic(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
    uint8_t *,size_t,uint64_t,vf_protect,void *,const uint64_t [4],
    const vf_boot_options_v2 *,const vf_memory_controls_v2 *,vf_memory_callback_v2,
    vf_dynamic_callback,void *,vf_memory_run_result_dynamic *);
int vf_run_memory_provider_dynamic(vf_cpu *,vf_code *,uint64_t,vf_protect,void *,
    const vf_memory_controls_v2 *,vf_memory_callback_v2,vf_dynamic_callback,void *,vf_memory_run_result_dynamic *);
#endif
