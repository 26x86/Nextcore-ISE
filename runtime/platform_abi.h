/* SPDX-License-Identifier: BSD-4-Clause */
#ifndef NEXTCORE_PLATFORM_ABI_H
#define NEXTCORE_PLATFORM_ABI_H
#include <stdint.h>
#define VF_PLATFORM_NONE 0u
#define VF_PLATFORM_IRQ_COMPAT_V1 1u
#define VF_PLATFORM_OVERRIDE_KEY UINT32_C(0x6fa8)
#define VF_PLATFORM_OVERRIDE_MASK UINT64_C(0x00f00000)
typedef struct {
    uint32_t abi_version,struct_size,platform_profile,flags;
    uint64_t initial_override,initial_pstate,vbar,irq_level,fiq_level,reserved;
} vf_boot_options_v2;
#endif
