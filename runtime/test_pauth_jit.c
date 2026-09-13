/* SPDX-License-Identifier: BSD-4-Clause
 * Link with the real software Rust vf_preos_pauth_step provider.
 */
#include "boot_jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tests/pauth_width_oracle.h"

extern int vf_preos_pauth_step(vf_pauth_context *,uint32_t);
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void *p,size_t n,int executable,void *opaque) {
    (void)opaque;return mprotect(p,n,PROT_READ|(executable?PROT_EXEC:PROT_WRITE));
}

int main(void) {
    vf_pauth_context c={0};
    c.current_el=1;c.sctlr=UINT64_C(1)<<31;c.tcr=16;
    c.keys[0][0]=UINT64_C(0x48ad369c24681357);
    c.keys[0][1]=UINT64_C(0xb752c963db97eca8);
    c.x[1]=0x130;c.x[2]=0x9876;
    CHECK(vf_preos_pauth_step(&c,0xdac10041)==0);
    CHECK(c.x[1]==UINT64_C(0xbf36000000000130));
    CHECK(vf_preos_pauth_step(&c,0xdac11041)==0 && c.x[1]==0x130);
    c.x[30]=0x130;c.sp=0x9876;
    CHECK(vf_preos_pauth_step(&c,0xd503233f)==0);
    CHECK(c.x[30]==UINT64_C(0xbf36000000000130));
    CHECK(vf_preos_pauth_step(&c,0xd50323bf)==0 && c.x[30]==0x130);
    c.x[1]=UINT64_C(0xbf37000000000130);
    CHECK(vf_preos_pauth_step(&c,0xdac11041)==0);
    CHECK(c.x[1]==UINT64_C(0x2000000000000130));
    c.tcr=18;vf_pauth_context before=c;
    CHECK(vf_preos_pauth_step(&c,0xdac10041)!=0 && memcmp(&before,&c,sizeof(c))==0);
    const unsigned enables[]={31,30,27,13};
    for(size_t i=0;i<sizeof(width_oracle)/sizeof(width_oracle[0]);i++) {
        const uint64_t *v=width_oracle[i];unsigned key=(unsigned)v[1];
        memset(&c,0,sizeof(c));c.current_el=1;c.sctlr=UINT64_C(1)<<enables[key];c.tcr=v[6];
        c.keys[key][0]=v[4];c.keys[key][1]=v[5];c.x[1]=v[2];c.x[2]=v[3];
        uint32_t sign=0xdac10041|(key<<10),auth=0xdac11041|(key<<10);
        uint32_t strip=key<2?0xdac143e1:0xdac147e1;
        /* QEMU APA5 adds pointer XOR above the address bits; baseline APA1
         * agrees only for these lower-range pointers. Preserve upper captures
         * for XPAC/disabled comparisons, never normalize their signed values. */
        if ((v[2] >> 63) == 0) {
            CHECK(vf_preos_pauth_step(&c,sign)==0 && c.x[1]==v[7]);
            CHECK(vf_preos_pauth_step(&c,auth)==0 && c.x[1]==v[8]);
        }
        c.x[1]=v[7];CHECK(vf_preos_pauth_step(&c,strip)==0 && c.x[1]==v[9]);
        c.sctlr=0;c.x[1]=v[2];CHECK(vf_preos_pauth_step(&c,sign)==0 && c.x[1]==v[10]);
        c.x[1]=v[7];CHECK(vf_preos_pauth_step(&c,auth)==0 && c.x[1]==v[11]);
    }

    uint8_t ram[0x4000]={0};
    uint8_t *code=mmap(0,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(code!=MAP_FAILED);
    const uint64_t base=UINT64_C(0x800000000),entry=base+0x100,args=base+0x1000;
    const uint32_t program[]={
        0xd2826aea,0xf2a48d0a,0xf2c6d38a,0xf2e915aa,
        0xd29d950b,0xf2bb72eb,0xf2d92c6b,0xf2f6ea4b,
        0xd518210a,0xd518212b,
        0xd2b0000c,0xd518100c,0xd280020c,0xd518204c,
        0xd2802601,0xd2930ec3,0x91000022,
        0xdac10062,0xf9000402,0xdac11062,0xf9000802,0xd4400000,
    };
    memcpy(ram+0x100,program,sizeof(program));
    vf_boot_result result;
    CHECK(vf_boot_run_with_pauth(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     code,16384,64,perms,0,vf_preos_pauth_step,&result)==VF_HALT);
    uint64_t signed_word,restored;
    memcpy(&signed_word,ram+0x1008,8);memcpy(&restored,ram+0x1010,8);
    CHECK(signed_word==UINT64_C(0xbf36000000000130) && restored==0x130);
    CHECK(result.retired==sizeof(program)/4 && result.compiled_blocks>1);
    CHECK(result.x2==0x130 && memcmp(ram+0x100,program,sizeof(program))==0);
    CHECK(vf_boot_run(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
                     code,16384,64,perms,0,&result)==VF_SYSTEM_REGISTER_TRAP);
    CHECK(result.retired==8 && result.pc==entry+32);
    /* The same real Rust system-register callback enables SA, then actual
     * native ADD/SP and STP expose the new SP-fault result through ABI v2. */
    const uint32_t sp_fault_program[]={0xd2800104,0xd5181004,0x910007ff,0xa90007e0};
    memcpy(ram+0x100,sp_fault_program,sizeof(sp_fault_program));
    const uint64_t registers[4]={0,0,0,0};vf_boot_result_v2 extended;
    CHECK(vf_boot_run_v2(ram,sizeof(ram),base,entry,args,base+sizeof(ram),code,16384,
              8,perms,0,registers,vf_preos_pauth_step,0,&extended)==VF_SP_ALIGNMENT_FAULT);
    CHECK(extended.base.status==19 && extended.base.fault_instruction==sp_fault_program[3]);
    CHECK(extended.base.retired==3 && extended.base.pc==entry+12 && extended.base.compiled_blocks>0);
    CHECK(extended.esr==UINT64_C(0x9a000000) && extended.sp==base+sizeof(ram)+1);
    CHECK(munmap(code,16384)==0);
    printf("{\"passed\":true,\"assertions\":%u,\"native_jit_executed\":true,\"software_qarma5\":true,\"wx_enforced\":true}\n",checks);
    return 0;
}
