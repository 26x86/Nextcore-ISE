/* SPDX-License-Identifier: BSD-4-Clause; authored architectural state checks. */
#include "jit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    for(unsigned el=0;el<2;el++) {
        vf_cpu cpu;vf_cpu_reset(&cpu,el);uint64_t value=UINT64_MAX;
        assert(vf_cpu_read_sysreg(&cpu,VF_SYSREG_KEY_CPACR_EL1,&value)!=0);
        assert(cpu.cpacr_el1==0 && value==UINT64_MAX);
        cpu.fp_execution_profile=VF_FP_EXECUTION_PARTIAL;
        for(unsigned mode=0;mode<4;mode++) {
            vf_cpu before=cpu;
            int status=vf_cpu_write_sysreg(&cpu,VF_SYSREG_KEY_CPACR_EL1,(uint64_t)mode<<20);
            if(el==0) {assert(status==VF_SYSREG_UNDEFINED);assert(!memcmp(&cpu,&before,sizeof(cpu)));}
            else {
                assert(status==0);
                assert(vf_cpu_read_sysreg(&cpu,VF_SYSREG_KEY_CPACR_EL1,&value)==0);
                assert(value==((uint64_t)mode<<20));
            }
        }
        vf_cpu before=cpu;
        assert(vf_cpu_write_sysreg(&cpu,VF_SYSREG_KEY_CPACR_EL1,1)!=0);
        assert(!memcmp(&cpu,&before,sizeof(cpu)));
        if(el==1) {
            assert(vf_cpu_read_sysreg(&cpu,VF_SYSREG_KEY_ID_AA64PFR0_EL1,&value)==0);
            assert(value==VF_PFR0_FP_RESEARCH);
        }
        cpu.pc=0x4000;cpu.instruction=0xd53b4400;cpu.vbar_el[1]=0x8000;
        assert(vf_cpu_commit_status(&cpu,VF_IMPLEMENTATION_GAP)==0);
        assert(cpu.exception_pending==VF_EXCEPTION_NONE && cpu.retired==0);
        assert(vf_cpu_commit_status(&cpu,VF_FP_ACCESS_TRAP)==0);
        assert(cpu.exception_pending==VF_EXCEPTION_FP_ACCESS);
        assert(cpu.exception_syndrome==0x1fe00000 && cpu.exception_pc==0x4000);
        assert(cpu.exception_target_el==1 && cpu.pc==0x4000 && cpu.retired==0);
        assert(vf_cpu_take_exception(&cpu)==0);
        assert(cpu.pc==0x8000+(el==0?0x400:0x200));
        assert(cpu.esr_el[1]==0x1fe00000 && cpu.elr_el[1]==0x4000);
    }
    puts("{\"passed\":true,\"scope\":\"CPACR bank, FP identity, exact A64 trap and host gap\"}");
}
