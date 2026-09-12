/* SPDX-License-Identifier: BSD-4-Clause */
#ifndef NEXTCORE_MEMORY_BOOT_V2_H
#define NEXTCORE_MEMORY_BOOT_V2_H
#include "memory_boot.h"
#include "memory_abi_v2.h"
#define VF_PROVIDER_UNAVAILABLE_V2 5u
typedef struct {
    vf_memory_run_result_v1 base;
    vf_memory_reply_v2 last_reply;
} vf_memory_run_result_v2;
_Static_assert(sizeof(vf_memory_run_result_v2)==320,"memory run v2");
_Static_assert(offsetof(vf_memory_run_result_v2,last_reply)==192,"memory reply offset");
static inline int vf_memory_controls_valid_v2(const vf_memory_controls_v2 *c) {
    const uint64_t mask=UINT64_C(0x3f)|(UINT64_C(1)<<7)|(UINT64_C(3)<<14)|
        (UINT64_C(0x3f)<<16)|(UINT64_C(1)<<22)|(UINT64_C(1)<<23)|(UINT64_C(3)<<30)|(UINT64_C(7)<<32);
    if(!c)return 0;
    uint64_t sctlr;
    if(c->profile==VF_MEMORY_V2_FIXED_NC)sctlr=UINT64_C(0x30d00803);
    else if(c->profile==VF_MEMORY_V2_FIXED_NC_UNALIGNED)sctlr=UINT64_C(0x30d00801);
    else return 0;
    if(c->abi_version!=2 || c->struct_size!=80 || c->reserved ||
       c->epoch!=1 || c->mair!=0x44 || c->hcr || c->scr ||
       (c->sctlr&~UINT64_C(0x18))!=sctlr || (c->tcr&~mask) || ((c->tcr>>32)&7)>5)return 0;
    unsigned tg0=(c->tcr>>14)&3,tg1=(c->tcr>>30)&3,t0=c->tcr&63,t1=(c->tcr>>16)&63;
    unsigned min,max,alignment;
    if(tg0==0 && tg1==2){min=16;max=39;alignment=0x1000;}
    else if(tg0==2 && tg1==1){min=17;max=47;alignment=0x4000;}else return 0;
    if(t0<min || t0>max || t1<min || t1>max)return 0;
    uint64_t allowed=(UINT64_C(0x0000ffffffffffff)&~((uint64_t)alignment-1))|UINT64_C(0x00ff000000000000);
    return !(c->ttbr0&~allowed) && !(c->ttbr1&~allowed);
}
/* The original fixed-regime entry retains its no-PAC contract. */
int vf_boot_run_memory_v2(uint64_t ram_base,uint64_t ram_size,
    uint64_t entry,uint64_t args,uint64_t stack,
    uint8_t *code,size_t code_bytes,uint64_t budget,
    vf_protect protect,void *protect_opaque,const uint64_t initial_x0_x3[4],
    const vf_boot_options_v2 *options,const vf_memory_controls_v2 *controls,
    vf_memory_callback_v2 memory,void *owner,vf_memory_run_result_v2 *result);
int vf_run_memory_provider_v2(vf_cpu *,vf_code *,uint64_t,vf_protect,void *,
    const vf_memory_controls_v2 *,vf_memory_callback_v2,void *,vf_memory_run_result_v2 *);
int vf_boot_run_memory_pauth_v2(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
    uint8_t *,size_t,uint64_t,vf_protect,void *,const uint64_t [4],vf_pauth_step,
    const vf_boot_options_v2 *,const vf_memory_controls_v2 *,vf_memory_callback_v2,void *,vf_memory_run_result_v2 *);
int vf_run_memory_provider_pauth_v2(vf_cpu *,vf_code *,uint64_t,vf_protect,void *,
    const vf_memory_controls_v2 *,vf_memory_callback_v2,void *,vf_memory_run_result_v2 *);
#endif
