/* SPDX-License-Identifier: BSD-4-Clause
 * Authored ARM register encodings and x86 native execution tests. No vendor
 * instruction stream or original firmware is needed by this test.
 */
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
static uint32_t sysreg(unsigned read,unsigned op1,unsigned op2,unsigned rt) {
    return (read?UINT32_C(0xd5200000):UINT32_C(0xd5000000)) |
        (3u<<19)|(op1<<16)|(13u<<12)|(op2<<5)|rt;
}
int main(void) {
    uint8_t ram[0x4000]={0};
    vf_code code={mmap(0,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),16384,0};
    CHECK(code.bytes!=MAP_FAILED);
    const struct {unsigned op1,op2,key;} registers[]={
        {3,2,VF_SYSREG_KEY_TPIDR_EL0}, {3,3,VF_SYSREG_KEY_TPIDRRO_EL0},
        {0,4,VF_SYSREG_KEY_TPIDR_EL1}
    };
    const uint64_t values[]={0,1,UINT64_MAX,UINT64_C(0x8000000000000000),
        UINT64_C(0xfedcba9876543210),UINT64_C(0x0000ffff0000ffff)};
    vf_cpu cpu,other;
    for(unsigned el=0;el<4;el++)for(unsigned r=0;r<3;r++) {
        vf_cpu_reset(&cpu,el);
        CHECK(cpu.tpidr_el0==0 && cpu.tpidrro_el0==0 && cpu.tpidr_el1==0);
        int readable=el!=VF_EL0 || r!=2,writable=el!=VF_EL0 || r==0;
        uint64_t output=123;
        CHECK(vf_cpu_read_sysreg(&cpu,registers[r].key,&output)==
            (readable?VF_SYSREG_OK:VF_SYSREG_UNDEFINED));
        CHECK(output==(readable?0u:123u));
        for(unsigned v=0;v<sizeof(values)/sizeof(values[0]);v++) {
            vf_cpu_reset(&cpu,el);cpu.x[0]=values[v];cpu.sp=0x9876;
            cpu.pstate|=UINT64_C(0xa00003c0);uint64_t pstate=cpu.pstate;
            cpu.tlb_generation=17;
            uint32_t program[]={sysreg(0,registers[r].op1,registers[r].op2,0),
                                sysreg(1,registers[r].op1,registers[r].op2,2)};
            int status=vf_run(&cpu,(uint8_t*)program,sizeof(program),ram,sizeof(ram),&code,2,perms,0);
            CHECK(status==(writable?VF_BUDGET:VF_UNDEFINED_INSTRUCTION));
            CHECK(cpu.retired==(writable?2u:0u) && cpu.compiled_blocks==1);
            CHECK(cpu.pc==(writable?8u:0u) && cpu.x[2]==(writable?values[v]:0));
            CHECK(cpu.sp==0x9876 && cpu.pstate==pstate && cpu.tlb_generation==17);
            CHECK(vf_cpu_read_sysreg(&cpu,registers[r].key,&output)==
                (readable?VF_SYSREG_OK:VF_SYSREG_UNDEFINED));
            if(readable)CHECK(output==(writable?values[v]:0));
            CHECK(vf_cpu_write_sysreg(&cpu,registers[r].key,values[v])==
                (writable?VF_SYSREG_OK:VF_SYSREG_UNDEFINED));
            if(!writable)CHECK(cpu.exception_pending==VF_EXCEPTION_UNDEFINED_INSTRUCTION);
        }
        /* An OS-written read-only EL0 value remains visible after changing
         * exception level. Changing EL must not select another thread bank. */
        vf_cpu_reset(&cpu,VF_EL1);
        CHECK(vf_cpu_write_sysreg(&cpu,registers[r].key,values[4])==VF_SYSREG_OK);
        CHECK(vf_cpu_set_current_el(&cpu,el)==0);
        uint32_t read=sysreg(1,registers[r].op1,registers[r].op2,7);
        CHECK(vf_run(&cpu,(uint8_t*)&read,sizeof(read),ram,sizeof(ram),&code,1,perms,0)==
            (readable?VF_BUDGET:VF_UNDEFINED_INSTRUCTION));
        CHECK(cpu.x[7]==(readable?values[4]:0));
    }
    /* Rt31 is XZR even with nonzero SP and private padding. Reads discard
     * without clearing the register; writes store zero in the chosen bank. */
    for(unsigned r=0;r<3;r++) {
        vf_cpu_reset(&cpu,VF_EL1);cpu.sp=0x999;cpu.x[31]=UINT64_MAX;
        CHECK(vf_cpu_write_sysreg(&cpu,registers[r].key,0x7654)==VF_SYSREG_OK);
        uint32_t discard=sysreg(1,registers[r].op1,registers[r].op2,31);
        CHECK(vf_run(&cpu,(uint8_t*)&discard,4,ram,sizeof(ram),&code,1,perms,0)==VF_BUDGET);
        uint64_t output;
        CHECK(vf_cpu_read_sysreg(&cpu,registers[r].key,&output)==VF_SYSREG_OK && output==0x7654);
        cpu.pc=0;uint32_t zero=sysreg(0,registers[r].op1,registers[r].op2,31);
        CHECK(vf_run(&cpu,(uint8_t*)&zero,4,ram,sizeof(ram),&code,1,perms,0)==VF_BUDGET);
        CHECK(vf_cpu_read_sysreg(&cpu,registers[r].key,&output)==VF_SYSREG_OK && output==0);
        CHECK(cpu.sp==0x999 && cpu.x[31]==UINT64_MAX);
    }
    /* Per-CPU banks are independent; unrelated/extension registers stay
     * explicit boundaries. A bounded block must never retire one of them. */
    vf_cpu_reset(&cpu,VF_EL1);vf_cpu_reset(&other,VF_EL1);
    CHECK(vf_cpu_write_sysreg(&cpu,VF_SYSREG_KEY_TPIDR_EL0,UINT64_MAX)==VF_SYSREG_OK);
    CHECK(other.tpidr_el0==0 && cpu.tpidrro_el0==0 && cpu.tpidr_el1==0);
    const uint32_t unsupported[]={sysreg(1,0,1,0),sysreg(1,3,5,0),sysreg(1,0,7,0)};
    for(unsigned i=0;i<3;i++) {
        vf_cpu_reset(&cpu,VF_EL1);
        CHECK(vf_run(&cpu,(uint8_t*)&unsupported[i],4,ram,sizeof(ram),&code,1,perms,0)==VF_SYSTEM_REGISTER_TRAP);
        CHECK(cpu.retired==0 && cpu.pc==0 && cpu.instruction==unsupported[i]);
    }
    CHECK(sizeof(vf_boot_result)==64 && sizeof(vf_pauth_context)==368);
    CHECK(offsetof(vf_pauth_context,current_el)==360 && offsetof(vf_boot_result,compiled_blocks)==56);
    CHECK(munmap(code.bytes,code.capacity)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,"
        "\"thread_registers\":3,\"exception_levels\":4,\"opaque_cpu_size\":%zu,"
        "\"boot_result_size\":%zu,\"pauth_context_size\":%zu}\n",
        checks,sizeof(vf_cpu),sizeof(vf_boot_result),sizeof(vf_pauth_context));
    return 0;
}
