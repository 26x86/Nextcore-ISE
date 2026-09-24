/* SPDX-License-Identifier: BSD-4-Clause */
#ifndef NEXTCORE_MEMORY_BOOT_H
#define NEXTCORE_MEMORY_BOOT_H
#include "boot_jit.h"
#include "memory_abi.h"
typedef struct {
    uint32_t abi_version,struct_size,provider_status,reserved0;
    vf_boot_result_v2 execution;
    uint64_t guest_far,last_address,fetch_requests,data_requests,completed_data_operations,reserved1;
} vf_memory_run_result_v1;
int vf_boot_run_memory_v1(uint64_t ram_base,uint64_t ram_size,
    uint64_t entry,uint64_t args,uint64_t stack,
    uint8_t *code,size_t code_bytes,uint64_t budget,
    vf_protect protect,void *protect_opaque,
    const uint64_t initial_x0_x3[4],vf_pauth_step pauth,
    const vf_boot_options_v2 *options,vf_memory_callback_v1 memory,void *owner,
    vf_memory_run_result_v1 *result);
/* C-private runner used by the bridge and architectural state tests. */
int vf_run_memory_provider(vf_cpu *,vf_code *,uint64_t,vf_protect,void *,
    vf_memory_callback_v1,void *,vf_memory_run_result_v1 *);
#endif
