/* Bounded software control policy: no universal architectural write-trap claim. */
#include "jit.h"
#include "memory_dynamic.h"
#include <stdio.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x) do {checks++;if(!(x)){failures++;fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
int main(void){
 for(unsigned on=0;on<2;on++)for(unsigned combo=0;combo<16;combo++){
  vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.sctlr=on;cpu.pc=0x1234;cpu.sp=0x8800;cpu.pstate=0xf00003c5;cpu.retired=77;cpu.tlb_tag=0x4321;cpu.tlb_pa=0x9876;cpu.tlb_generation=99;for(unsigned i=0;i<32;i++)cpu.x[i]=0x100+i;
  vf_cpu before;memcpy(&before,&cpu,sizeof(cpu));uint64_t value=16|((uint64_t)combo<<39);
  int result=vf_cpu_write_sysreg(&cpu,VF_SYSREG_KEY_TCR_EL1,value);
  if(combo){CHECK(result==VF_SYSREG_INVALID_VALUE);CHECK(memcmp(&cpu,&before,sizeof(cpu))==0);}else{CHECK(result==VF_SYSREG_OK);CHECK(cpu.tcr==16);}
 }
 for(unsigned sixteen=0;sixteen<2;sixteen++)for(unsigned profile=1;profile<=3;profile++){
  vf_memory_controls_v2 c={0};c.abi_version=profile==2?3:2;c.struct_size=80;c.profile=profile;c.sctlr=profile==3?UINT64_C(0x30d00801):UINT64_C(0x30d00803);c.ttbr0=c.ttbr1=UINT64_C(0x10000000);c.mair=0x44;c.epoch=1;
  c.tcr=sixteen?17|(17ULL<<16)|(2ULL<<14)|(1ULL<<30)|(5ULL<<32):16|(16ULL<<16)|(2ULL<<30)|(5ULL<<32);
  CHECK(profile==2?vf_dynamic_controls_valid(&c):vf_memory_controls_valid_v2(&c));
  for(unsigned combo=1;combo<16;combo++){vf_memory_controls_v2 bad=c;bad.tcr|=(uint64_t)combo<<39;CHECK(!(profile==2?vf_dynamic_controls_valid(&bad):vf_memory_controls_valid_v2(&bad)));}
 }
 printf("{\"assertions\":%u,\"failures\":%u,\"optional_combinations_per_mmu_state\":15}\n",checks,failures);return failures?1:0;
}
