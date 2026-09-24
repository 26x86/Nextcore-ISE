/* SPDX-License-Identifier: BSD-4-Clause
 * Shared firmware JIT protection adapter; host Boot Services remain active.
 */
#include "uefi.h"

typedef struct { EFI_MEMORY_ATTRIBUTE *memory; EFI_CPU_ARCH *cpu; } protection_context;
static EFI_GUID memory_guid={0xf4560cf6,0x40ec,0x4b4a,{0xa1,0x92,0xbf,0x1d,0x57,0xd0,0xb1,0x89}};
static EFI_GUID cpu_guid={0x26baccb1,0x6f42,0x11d4,{0xbc,0xe7,0x00,0x80,0xc7,0x3c,0x88,0x81}};

int vf_efi_jit_protection_open(void *table, void *storage, size_t bytes) {
    if (!table || !storage || bytes != sizeof(protection_context) || ((uintptr_t)storage & 7)) return -1;
    EFI_SYSTEM_TABLE *system = table;
    if (!system->BootServices) return -1;
    protection_context *context=storage;
    context->memory=0;context->cpu=0;
    EFI_STATUS memory=system->BootServices->LocateProtocol(&memory_guid,0,(void**)&context->memory);
    if (memory || !context->memory) {
        context->memory=0;
        if (system->BootServices->LocateProtocol(&cpu_guid,0,(void**)&context->cpu) || !context->cpu) return -1;
    }
    return 0;
}

static int page_permissions(uint64_t address,int executable) {
    uint64_t cr0,cr3,cr4;uint32_t lo,hi;
    __asm__ volatile("mov %%cr0,%0":"=r"(cr0));__asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    __asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0xc0000080));
    (void)hi;
    if(!(cr0&(1u<<16))||!(lo&(1u<<11))||(cr4&(1u<<12)))return -1;
    uint64_t table=cr3&UINT64_C(0x000ffffffffff000);int writable=1,nx=0;
    for(int level=3;level>=0;level--) {
        uint64_t pte=((volatile const uint64_t*)(uintptr_t)table)[(address>>(12+level*9))&511];
        if(!(pte&1))return -1;
        writable=writable&&((pte&2)!=0);nx=nx||((pte>>63)!=0);
        if(level==0 || ((level==1||level==2)&&(pte&128)))return executable?(!writable&&!nx?0:-1):(writable&&nx?0:-1);
        if(level==3&&(pte&128))return -1;
        table=pte&UINT64_C(0x000ffffffffff000);
    }
    return -1;
}

int vf_efi_jit_protect(void *p,size_t n,int executable,void *opaque) {
    if (!p || !opaque || !n || ((uintptr_t)p & 4095) || (n & 4095) || (executable != 0 && executable != 1)) return -1;
    protection_context *context=opaque;EFI_MEMORY_ATTRIBUTE *m=context->memory;
    uint64_t attrs=0,base=(uint64_t)(uintptr_t)p;
    if(!m) {
        if(!context->cpu || context->cpu->SetMemoryAttributes(context->cpu,base,n,8|EFI_MEMORY_RO|EFI_MEMORY_XP))return -1;
        if(context->cpu->SetMemoryAttributes(context->cpu,base,n,8|(executable?EFI_MEMORY_RO:EFI_MEMORY_XP)))return -1;
        for(size_t off=0;off<n;off+=4096)if(page_permissions(base+off,executable))return -1;
        return 0;
    }
    if(executable) {
        if(m->Set(m,base,n,EFI_MEMORY_RO))return -1;
        if(m->Clear(m,base,n,EFI_MEMORY_XP))return -1;
    } else {
        if(m->Set(m,base,n,EFI_MEMORY_XP))return -1;
        if(m->Clear(m,base,n,EFI_MEMORY_RO))return -1;
    }
    if(m->Get(m,base,n,&attrs))return -1;
    if (!vf_memory_attributes_satisfy(attrs,executable)) return -1;
    for(size_t off=0;off<n;off+=4096)if(page_permissions(base+off,executable))return -1;
    return 0;
}
