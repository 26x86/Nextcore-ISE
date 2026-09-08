/* SPDX-License-Identifier: BSD-4-Clause */
#include "boot_jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void *p,size_t n,int executable,void *opaque) {
    (void)opaque;return mprotect(p,n,PROT_READ|(executable?PROT_EXEC:PROT_WRITE));
}
static int deny(void *p,size_t n,int executable,void *opaque) {
    (void)p;(void)n;(void)executable;(void)opaque;return -1;
}
static int deny_second_block(void *p,size_t n,int executable,void *opaque) {
    unsigned *calls=opaque;
    if(++*calls==3)return -1;
    return perms(p,n,executable,0);
}

int main(void) {
    uint8_t ram[0x4000]={0};
    uint8_t *code=mmap(0,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(code!=MAP_FAILED);
    const uint64_t base=UINT64_C(0x800000000),entry=base+0x100,args=base+0x1000;
    const uint32_t program[]={0xf9400001,0x91001c22,0xf9000402,0xf9400403,0xd4400000};
    memcpy(ram+0x100,program,sizeof(program));
    uint64_t value=35;memcpy(ram+0x1000,&value,8);
    vf_boot_result result;
    CHECK(vf_boot_run(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     code,16384,32,perms,0,&result)==VF_HALT);
    CHECK(result.retired==5 && result.pc==entry+20 && result.x0==args);
    CHECK(result.x1==35 && result.x2==42 && result.x3==42 && result.compiled_blocks==1);
    CHECK(result.fault_instruction==0); /* Last LDR is not a fault on HALT. */
    memcpy(&value,ram+0x1008,8);CHECK(value==42);
    CHECK(memcmp(ram+0x100,program,sizeof(program))==0);

    CHECK(vf_boot_run(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     code,16384,1,perms,0,&result)==VF_BUDGET);
    CHECK(result.retired==1 && result.x1==35 && result.fault_instruction==0);

    const uint32_t stale_then_fault[]={0xf9400001,0xffffffff};
    memcpy(ram+0x100,stale_then_fault,sizeof(stale_then_fault));
    CHECK(vf_boot_run(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     code,16384,8,perms,0,&result)==VF_UNDEFINED_INSTRUCTION);
    CHECK(result.retired==1 && result.fault_instruction==UINT32_MAX);

    /* A successful first block leaves a decoded LDR behind. Neither a later
     * code-capacity limit nor a host protection failure may report it as a
     * guest instruction fault. */
    const uint32_t blocks[]={0xf9400001,0x14000001,
        0xb1000442,0xb1000442,0xb1000442,0xb1000442,
        0xb1000442,0xb1000442,0xb1000442,0xb1000442,0xd4400000};
    memcpy(ram+0x100,blocks,sizeof(blocks));
    CHECK(vf_boot_run(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     code,512,32,perms,0,&result)==VF_CODE_FULL);
    CHECK(result.retired==2 && result.compiled_blocks==1 && result.fault_instruction==0);
    unsigned calls=0;
    CHECK(vf_boot_run(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     code,16384,32,deny_second_block,&calls,&result)==VF_PROTECTION);
    CHECK(result.retired==2 && result.compiled_blocks==1 && calls==3 && result.fault_instruction==0);

    const uint32_t final_load=0xf9400001;
    memcpy(ram+sizeof(ram)-4,&final_load,4);
    CHECK(vf_boot_run(ram,sizeof(ram),base,base+sizeof(ram)-4,args,base+sizeof(ram),
                     code,16384,8,perms,0,&result)==VF_INSTRUCTION_ABORT);
    CHECK(result.retired==1 && result.fault_instruction==0);

    const uint32_t call_program[]={0x100000c4,0xd63f0080,0x91000422,0x90000003,
                                   0xd4400000,0xd503201f,0x12800001,0xd65f03c0};
    memcpy(ram+0x100,call_program,sizeof(call_program));
    CHECK(vf_boot_run(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     code,16384,32,perms,0,&result)==VF_HALT);
    CHECK(result.x1==UINT64_C(0xffffffff) && result.x2==UINT64_C(0x100000000));
    CHECK(result.x3==base && result.retired==7 && result.compiled_blocks==3);

    /* Host memory remains inaccessible through below/above-range guest PAs. */
    vf_cpu cpu;vf_code buffer={code,16384,0};
    const uint32_t store[]={0xf9000020,0xd4400000};
    memcpy(ram+0x100,store,sizeof(store));
    vf_cpu_reset(&cpu,VF_EL1);cpu.guest_ram_base=base;cpu.pc=entry;
    cpu.x[1]=base-8;cpu.x[0]=0xfedc;
    CHECK(vf_run(&cpu,ram,sizeof(ram),ram,sizeof(ram),&buffer,8,perms,0)==VF_DATA_ABORT);
    CHECK(cpu.far==base-8 && cpu.retired==0 && ram[0]==0);
    vf_cpu_reset(&cpu,VF_EL1);cpu.guest_ram_base=base;cpu.pc=entry;
    cpu.x[1]=base+sizeof(ram);cpu.x[0]=0xfedc;
    CHECK(vf_run(&cpu,ram,sizeof(ram),ram,sizeof(ram),&buffer,8,perms,0)==VF_DATA_ABORT);
    CHECK(cpu.far==base+sizeof(ram) && cpu.retired==0);
    vf_cpu_reset(&cpu,VF_EL1);cpu.guest_ram_base=base;cpu.pc=entry;
    cpu.x[1]=base+1;
    CHECK(vf_run(&cpu,ram,sizeof(ram),ram,sizeof(ram),&buffer,8,perms,0)==VF_ALIGNMENT_FAULT);
    CHECK(cpu.far==base+1);

    const uint32_t loop=0x14000000;memcpy(ram+0x100,&loop,4);
    CHECK(vf_boot_run(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     code,16384,7,perms,0,&result)==VF_BUDGET);
    CHECK(result.pc==entry && result.retired==7 && result.compiled_blocks==7);
    CHECK(vf_boot_run(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     code,16384,7,deny,0,&result)==VF_PROTECTION);
    CHECK(result.retired==0 && result.compiled_blocks==0);
    CHECK(vf_boot_run(ram,sizeof(ram),base,base-4,args,base+sizeof(ram),
                     code,16384,7,perms,0,&result)==VF_DATA_FAULT);
    CHECK(vf_boot_run(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     ram,sizeof(ram),7,perms,0,&result)==VF_DATA_FAULT);
    CHECK(vf_boot_run(ram,sizeof(ram),UINT64_MAX-0x3fff,entry,args,base+sizeof(ram),
                     code,16384,7,perms,0,&result)==VF_DATA_FAULT);
    CHECK(munmap(code,16384)==0);
    printf("{\"passed\":true,\"assertions\":%u,\"native_jit_executed\":true,\"nonzero_guest_ram_base\":true,\"wx_enforced\":true}\n",checks);
    return 0;
}
