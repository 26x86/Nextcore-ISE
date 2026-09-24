/* Authored bounded MMFR0 policy test, not a universal EL0 architecture claim. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
int main(void){
 vf_code code={mmap(0,4096,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);
 for(unsigned rd=0;rd<32;rd++)for(unsigned failure=0;failure<11;failure++){
  vf_cpu cpu;unsigned el=failure==0?0:failure==9?2:failure==10?3:1;vf_cpu_reset(&cpu,el);cpu.sp=0x12345678;cpu.pstate=0xf00003c0|(el?el*4+1:0);
  for(unsigned j=0;j<32;j++)cpu.x[j]=0x100+j;uint64_t regs[32];memcpy(regs,cpu.x,sizeof(regs));uint32_t state=cpu.pstate;
  if(failure==1)cpu.hcr_el2=1;if(failure==2)cpu.scr_el3=1;if(failure==7)cpu.hcr_el2=1ULL<<63;if(failure==8)cpu.scr_el3=1ULL<<63;
  uint32_t word=0xd5380700|rd;if(failure==3)word^=1u<<21;if(failure==4)word^=1u<<5;if(failure==5)word^=1u<<12;if(failure==6)word^=1u<<16;
  uint8_t ram[8]={1,2,3},before[8];memcpy(before,ram,8);
  CHECK(vf_run(&cpu,(uint8_t*)&word,4,ram,8,&code,1,perms,0)==VF_SYSTEM_REGISTER_TRAP);
  CHECK(!memcmp(cpu.x,regs,sizeof(regs))&&!memcmp(ram,before,8)&&cpu.sp==0x12345678&&cpu.pstate==state&&cpu.pc==0&&cpu.retired==0);
 }

 for(unsigned el=0;el<4;el++)for(unsigned selector=0;selector<5;selector++){
  vf_cpu cpu;vf_cpu_reset(&cpu,el);CHECK(cpu.id_aa64mmfr0==0x0f100005);cpu.id_aa64mmfr0=0xfeed1234;if(selector==1)cpu.hcr_el2=1;if(selector==2)cpu.scr_el3=1;if(selector==3)cpu.hcr_el2=1ULL<<63;if(selector==4)cpu.scr_el3=1ULL<<63;uint64_t value=0xfeed;
  int good=el==1&&selector==0;CHECK(vf_cpu_read_sysreg(&cpu,0x4038,&value)==(good?VF_SYSREG_OK:VF_SYSREG_UNKNOWN));CHECK(value==(good?0x0f100005:0xfeed));CHECK(vf_cpu_write_sysreg(&cpu,0x4038,1)!=VF_SYSREG_OK);
 }
 for(unsigned selector=0;selector<5;selector++)for(unsigned reverse=0;reverse<2;reverse++){
  vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.id_aa64mmfr0=0xfeed1234;uint32_t word=0xd5380700;uint8_t ram[8]={0};
  if(reverse){if(selector==0||selector==3)cpu.hcr_el2=selector==3?1ULL<<63:1;else if(selector==1||selector==4)cpu.scr_el3=selector==4?1ULL<<63:1;else{cpu.current_el=0;cpu.pstate=0x3c0;}}
  CHECK(perms(code.bytes,4096,0,0)==0);CHECK(vf_translate_cpu(&code,&cpu,(uint8_t*)&word,4,0,1)==VF_NEXT);CHECK(perms(code.bytes,4096,1,0)==0);
  cpu.hcr_el2=reverse?0:selector==3?1ULL<<63:selector==0;cpu.scr_el3=reverse?0:selector==4?1ULL<<63:selector==1;cpu.current_el=(!reverse&&selector==2)?0:1;cpu.pstate=cpu.current_el==0?0xf00003c0:0xf00003c5;cpu.x[0]=0xfeed;cpu.sp=0x800;uint64_t flags=cpu.pstate;
  CHECK(((vf_entry)(void*)code.bytes)(&cpu,ram,8)==(reverse?VF_NEXT:VF_SYSTEM_REGISTER_TRAP));CHECK(cpu.x[0]==(reverse?0x0f100005:0xfeed)&&cpu.pc==(reverse?4:0)&&cpu.retired==(reverse?1:0)&&cpu.sp==0x800&&cpu.pstate==flags);
 }

 /* ORACLE_INSERT */
 CHECK(munmap(code.bytes,4096)==0);printf("{\"passed\":true,\"assertions\":%u}\n",checks);
}
