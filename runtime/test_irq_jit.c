/* SPDX-License-Identifier: BSD-4-Clause
 * Authored inputs prove actual native execution and interrupt state effects.
 */
#include "boot_jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void *p,size_t n,int x,void *opaque) {(void)opaque;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t platform_word(int read,unsigned rt) {
    return (read?0xd5200000u:0xd5000000u)|(VF_PLATFORM_OVERRIDE_KEY<<5)|rt;
}
int main(void) {
    uint8_t ram[0x4000]={0};
    vf_code code={mmap(0,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),16384,0};
    CHECK(code.bytes!=MAP_FAILED);
    vf_cpu cpu;
    const uint32_t halt=0xd4400000;
    /* Every input/mask combination at EL0, EL1t and EL1h. Software profile
     * changes the input path, independently of PSTATE's I/F selection. */
    for(unsigned mode=0;mode<3;mode++)for(unsigned daif=0;daif<4;daif++)
    for(unsigned ov=0;ov<4;ov++)for(unsigned lines=0;lines<4;lines++) {
        vf_cpu_reset(&cpu,mode?VF_EL1:VF_EL0);
        cpu.pstate=(mode?(mode==1?4:5):0)|((uint64_t)daif<<6)|UINT64_C(0xa0000000);
        uint64_t saved=cpu.pstate;
        cpu.sp=0x1230;cpu.sp_el[1]=0x3000;cpu.vbar_el[1]=0x2000;
        cpu.esr_el[1]=0xabcdef;cpu.far_el[1]=0x6789;
        uint64_t override=((ov&1)?UINT64_C(2)<<22:0)|((ov&2)?UINT64_C(2)<<20:0);
        CHECK(vf_cpu_configure_platform(&cpu,1,override)==VF_SYSREG_OK);
        CHECK(vf_cpu_set_interrupt_lines(&cpu,lines&1,lines>>1)==0);
        int fiq=(lines&2) && !(daif&1) && !(ov&2);
        int irq=(lines&1) && !(daif&2) && !(ov&1);
        int expected=fiq?VF_FIQ_INTERRUPT:irq?VF_EXTERNAL_INTERRUPT:VF_HALT;
        CHECK(vf_run(&cpu,(uint8_t*)&halt,4,ram,sizeof(ram),&code,2,perms,0)==expected);
        CHECK(cpu.irq_level==(lines&1) && cpu.fiq_level==(lines>>1));
        CHECK(cpu.esr_el[1]==0xabcdef && cpu.far_el[1]==0x6789);
        if(fiq || irq) {
            uint64_t offset=(mode==0?0x400:mode==1?0:0x200)+(fiq?0x100:0x80);
            CHECK(cpu.pc==0x2000+offset && cpu.exception_vector==cpu.pc);
            CHECK(cpu.current_el==VF_EL1 && cpu.pstate==(UINT64_C(0xa0000000)|0x3c5));
            CHECK(cpu.spsr_el[1]==saved && cpu.elr_el[1]==0 && cpu.retired==0);
            CHECK(cpu.sp==(mode==2?0x1230u:0x3000u) && cpu.instruction==0);
            cpu.pstate=(cpu.pstate&~UINT64_C(0xc0))|(saved&0xc0);
            int repeat=vf_cpu_poll_interrupt(&cpu);
            CHECK(repeat==expected); /* Source stayed asserted. */
            CHECK(vf_cpu_set_interrupt_lines(&cpu,0,0)==0);
            cpu.pstate&=~UINT64_C(0xc0);
            CHECK(vf_cpu_poll_interrupt(&cpu)==VF_NEXT);
        } else CHECK(cpu.retired==1 && cpu.compiled_blocks==1 && cpu.pc==4);
    }
    /* A native instruction then a validated override write makes an already
     * pending level eligible. The following HLT must not execute. */
    uint32_t unmask[]={0xd2800000,platform_word(0,0),halt};
    vf_cpu_reset(&cpu,VF_EL1);cpu.vbar_el[1]=0x2000;
    CHECK(vf_cpu_configure_platform(&cpu,1,UINT64_C(0xa00000))==0);
    CHECK(vf_cpu_set_interrupt_lines(&cpu,1,0)==0);
    CHECK(vf_run(&cpu,(uint8_t*)unmask,sizeof(unmask),ram,sizeof(ram),&code,8,perms,0)==VF_EXTERNAL_INTERRUPT);
    CHECK(cpu.retired==2 && cpu.compiled_blocks==2 && cpu.platform_override==0);
    CHECK(cpu.elr_el[1]==8 && cpu.irq_level==1);
    /* DAIF mask changes also return to dispatch before the next instruction. */
    const uint32_t daif[]={0xd2802464,0xd50343ff,halt};
    vf_cpu_reset(&cpu,VF_EL1);cpu.pstate|=0x3c0;cpu.vbar_el[1]=0x2000;
    CHECK(vf_cpu_set_interrupt_lines(&cpu,0,1)==0);
    CHECK(vf_run(&cpu,(uint8_t*)daif,sizeof(daif),ram,sizeof(ram),&code,8,perms,0)==VF_FIQ_INTERRUPT);
    CHECK(cpu.retired==2 && cpu.compiled_blocks==2 && cpu.x[4]==0x123);
    CHECK(cpu.elr_el[1]==8 && cpu.spsr_el[1]==0x305);
    /* Native retirement advances the deterministic counter; a deadline
     * between instructions must interrupt before the third instruction. */
    const uint32_t timer_program[]={0xd2800025,0xd2800045,halt};
    vf_cpu_reset(&cpu,VF_EL1);cpu.vbar_el[1]=0x2000;
    CHECK(vf_cpu_write_sysreg(&cpu,VF_SYSREG_KEY_CNTP_CVAL_EL0,2)==0);
    CHECK(vf_cpu_write_sysreg(&cpu,VF_SYSREG_KEY_CNTP_CTL_EL0,VF_TIMER_CTL_ENABLE)==0);
    CHECK(vf_run(&cpu,(uint8_t*)timer_program,sizeof(timer_program),ram,sizeof(ram),&code,8,perms,0)==VF_TIMER_INTERRUPT);
    CHECK(cpu.retired==2 && cpu.cntpct==2 && cpu.x[5]==2 && cpu.elr_el[1]==8);
    CHECK(vf_cpu_timer_pending(&cpu) && cpu.exception_vector==0x2280);
    /* SPSel saves the active bank; switching twice restores the first SP. */
    const uint32_t spsel[]={0xd50040bf,0x910043ff,0xd50041bf,halt};
    vf_cpu_reset(&cpu,VF_EL1);cpu.sp=0x500;cpu.sp_el[0]=0x800;
    CHECK(vf_run(&cpu,(uint8_t*)spsel,sizeof(spsel),ram,sizeof(ram),&code,8,perms,0)==VF_HALT);
    CHECK(cpu.sp==0x500 && cpu.sp_el[0]==0x810 && cpu.sp_el[1]==0x500);
    const uint64_t bad[]={1,UINT64_C(1)<<20,UINT64_C(3)<<22,UINT64_C(1)<<24,UINT64_MAX};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        uint32_t wr=platform_word(0,0);
        vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=bad[i];
        CHECK(vf_cpu_configure_platform(&cpu,1,UINT64_C(0xa00000))==0);
        CHECK(vf_run(&cpu,(uint8_t*)&wr,4,ram,sizeof(ram),&code,1,perms,0)==VF_SYSTEM_REGISTER_TRAP);
        CHECK(cpu.retired==0 && cpu.pc==0 && cpu.platform_override==UINT64_C(0xa00000));
    }
    uint32_t rd=platform_word(1,0);
    vf_cpu_reset(&cpu,VF_EL1);
    CHECK(vf_run(&cpu,(uint8_t*)&rd,4,ram,sizeof(ram),&code,1,perms,0)==VF_SYSTEM_REGISTER_TRAP);
    CHECK(cpu.retired==0 && cpu.pc==0);
    /* Enabled guest translation cannot fall through to physical execution. */
    vf_cpu_reset(&cpu,VF_EL1);cpu.sctlr=1;
    CHECK(vf_run(&cpu,(uint8_t*)&halt,4,ram,sizeof(ram),&code,1,perms,0)==VF_SYSTEM_REGISTER_TRAP);
    CHECK(cpu.retired==0 && cpu.compiled_blocks==0 && cpu.pc==0 && cpu.instruction==0);
    for(unsigned route=0;route<3;route++) {
        vf_cpu_reset(&cpu,VF_EL1);vf_cpu_set_interrupt_lines(&cpu,1,1);
        if(route==0)cpu.hcr_el2=0x18;
        else if(route==1)cpu.scr_el3=6;
        else CHECK(vf_cpu_set_current_el(&cpu,VF_EL2)==0);
        CHECK(vf_run(&cpu,(uint8_t*)&halt,4,ram,sizeof(ram),&code,1,perms,0)==VF_SYSTEM_REGISTER_TRAP);
        CHECK(cpu.retired==0 && cpu.pc==0 && cpu.irq_level && cpu.fiq_level && cpu.instruction==0);
    }
    /* Exercise the stable v2 ABI, including pending lines and saved state. */
    uint64_t base=UINT64_C(0x800000000),registers[4]={5,6,7,8};
    memcpy(ram+0x100,daif,sizeof(daif));
    vf_boot_options_v2 options={2,64,1,0,0,0x3c4,base+0x800,0,1,0};
    vf_boot_result_v2 result;
    CHECK(vf_boot_run_v2(ram,sizeof(ram),base,base+0x100,base+0x1000,base+0x4000,
        code.bytes,code.capacity,8,perms,0,registers,0,&options,&result)==VF_FIQ_INTERRUPT);
    CHECK(result.base.retired==2 && result.base.compiled_blocks==2 && result.base.fault_instruction==0);
    CHECK(result.elr==base+0x108 && result.spsr==0x304 && result.exception_vector==base+0x900);
    CHECK(result.platform_profile==1 && result.pending_lines==2 && result.pstate==0x3c5);
    options.struct_size=63;
    CHECK(vf_boot_run_v2(ram,sizeof(ram),base,base+0x100,base+0x1000,base+0x4000,
        code.bytes,code.capacity,8,perms,0,registers,0,&options,&result)==VF_DATA_FAULT);
    CHECK(result.base.retired==0 && result.base.compiled_blocks==0);
    CHECK(munmap(code.bytes,code.capacity)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"irq_fiq_levels\":true,\"platform_profile\":1}\n",checks);
}
