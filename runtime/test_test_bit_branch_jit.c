/* SPDX-License-Identifier: BSD-4-Clause; authored native TBZ/TBNZ proof. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t instruction(unsigned op,unsigned bit,int immediate,unsigned rt) {
    return 0x36000000u|(op<<24)|((bit>>5)<<31)|((bit&31)<<19)|(((unsigned)immediate&0x3fff)<<5)|rt;
}
static uint64_t expected_pc(uint64_t pc,unsigned op,unsigned bit,int immediate,uint64_t source,unsigned rt) {
    unsigned value=rt==31?0:(unsigned)((source/(UINT64_C(1)<<bit))&1);
    return pc+(value==op?(uint64_t)((int64_t)immediate*4):4);
}
static void one(vf_code *code,uint64_t pc,unsigned op,unsigned bit,int immediate,uint64_t source,unsigned rt,unsigned flags) {
    vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.pc=pc;cpu.guest_ram_base=pc;cpu.sp=UINT64_C(0x9876543210);
    for(unsigned i=0;i<32;i++)cpu.x[i]=UINT64_C(0xabcdef1200000000)+i;
    cpu.x[0]=source;cpu.pstate=UINT64_C(0x12345000000003c5)|((uint64_t)flags<<28);
    uint64_t regs[32];memcpy(regs,cpu.x,sizeof(regs));uint8_t ram[8];memset(ram,0xa5,8);
    uint32_t word=instruction(op,bit,immediate,rt);
    CHECK(vf_run(&cpu,(const uint8_t*)&word,4,ram,8,code,1,perms,0)==VF_BUDGET);
    CHECK(cpu.pc==expected_pc(pc,op,bit,immediate,source,rt));
    CHECK(cpu.retired==1 && cpu.compiled_blocks==1 && cpu.sp==UINT64_C(0x9876543210));
    CHECK(cpu.pstate==(UINT64_C(0x12345000000003c5)|((uint64_t)flags<<28)) && !memcmp(regs,cpu.x,sizeof(regs)));
    for(unsigned i=0;i<8;i++)CHECK(ram[i]==0xa5);
    cases++;
}
int main(void) {
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);
    const int offsets[]={-8192,-8191,-1,0,1,2,8191};
    for(unsigned bit=0;bit<64;bit++)for(unsigned op=0;op<2;op++)for(unsigned flags=0;flags<16;flags++)for(unsigned data=0;data<6;data++)
    for(unsigned at=0;at<7;at++)for(unsigned zr=0;zr<2;zr++) {
        uint64_t source=data==0?0:data==1?UINT64_MAX:data==2?UINT64_C(1)<<bit:data==3?~(UINT64_C(1)<<bit):data==4?UINT64_C(0xffffffff00000000):UINT64_C(0x0123456789abcdef);
        one(&code,0x40000000,op,bit,offsets[at],source,zr?31:0,flags);
    }
    const unsigned edge_bits[]={0,31,32,63};
    for(int immediate=-8192;immediate<8192;immediate++)for(unsigned i=0;i<4;i++)for(unsigned op=0;op<2;op++)for(unsigned set=0;set<2;set++)
        one(&code,0x40000000,op,edge_bits[i],immediate,set?UINT64_C(1)<<edge_bits[i]:0,0,(unsigned)immediate&15);
    const uint64_t pcs[]={0,4,UINT64_MAX-7,UINT64_MAX-3};
    for(unsigned i=0;i<4;i++)for(unsigned bit=0;bit<64;bit++)for(unsigned op=0;op<2;op++)for(unsigned set=0;set<2;set++)for(unsigned at=0;at<7;at++)
        one(&code,pcs[i],op,bit,offsets[at],set?UINT64_C(1)<<bit:0,0,15);
    for(unsigned op=0;op<2;op++) {
        uint32_t program[]={instruction(op,63,2,31),0,0xd4400000};uint8_t ram[8]={0};vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);
        CHECK(vf_run(&cpu,(const uint8_t*)program,sizeof(program),ram,8,&code,8,perms,0)==(op?VF_UNDEFINED_INSTRUCTION:VF_HALT));
        CHECK(cpu.retired==(op?1u:2u) && cpu.compiled_blocks==2 && cpu.pc==(op?4u:12u));
    }
    for(unsigned op=0;op<2;op++)for(unsigned missing=0;missing<2;missing++) {
        uint32_t word=instruction(op,7,missing?8191:0,0);uint8_t ram[8]={0};vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=op?128:0;
        CHECK(vf_run(&cpu,(const uint8_t*)&word,4,ram,8,&code,7,perms,0)==(missing?VF_INSTRUCTION_ABORT:VF_BUDGET));
        CHECK(cpu.retired==(missing?1u:7u) && cpu.compiled_blocks==(missing?2u:7u) && cpu.pc==(missing?32764u:0));
        if(missing)CHECK(cpu.far==32764 && cpu.esr==0x84000007 && cpu.instruction==0);
    }
    {uint32_t word=instruction(0,0,1,31);uint8_t ram[8]={0};vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.pc=UINT64_MAX-3;cpu.guest_ram_base=cpu.pc;
     CHECK(vf_run(&cpu,(const uint8_t*)&word,3,ram,8,&code,1,perms,0)==VF_INSTRUCTION_ABORT);CHECK(cpu.retired==0 && cpu.pc==UINT64_MAX-3);}
    CHECK(munmap(code.bytes,4096)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"test_bit_branch_cases\":%u}\n",checks,cases);
}
