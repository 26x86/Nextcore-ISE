/* SPDX-License-Identifier: BSD-4-Clause; authored conditional-select native proof. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t instruction(unsigned width,unsigned op,unsigned cond,unsigned rn,unsigned rm,unsigned rd) {
    return 0x1a800000u|((width==64?1u:0u)<<31)|((op>>1)<<30)|((op&1)<<10)|(rm<<16)|(cond<<12)|(rn<<5)|rd;
}
static int truth(unsigned flags,unsigned cond) {
    int n=!!(flags&8),z=!!(flags&4),c=!!(flags&2),v=!!(flags&1);
    const int table[]={z,!z,c,!c,n,!n,v,!v,c&&!z,!c||z,n==v,n!=v,!z&&n==v,z||n!=v,1,1};
    return table[cond];
}
static void one(vf_code*code,unsigned width,unsigned op,unsigned cond,unsigned flags,uint64_t a,uint64_t b,unsigned mode) {
    const unsigned roles[][3]={{0,1,2},{31,1,2},{0,31,2},{31,31,2},{0,1,31},{0,1,0},{0,1,1},{0,0,0}};
    unsigned rn=roles[mode][0],rm=roles[mode][1],rd=roles[mode][2];
    vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);
    for(unsigned i=0;i<32;i++)cpu.x[i]=UINT64_C(0xabcdef1200000000)+i;
    cpu.x[0]=a;cpu.x[1]=b;cpu.sp=UINT64_C(0x9876543210);cpu.pstate=UINT64_C(0x12345000000003c5)|((uint64_t)flags<<28);
    uint64_t registers[32];memcpy(registers,cpu.x,sizeof(registers));
    uint64_t mask=width==64?UINT64_MAX:UINT32_MAX;
    uint64_t left=rn==31?0:cpu.x[rn]&mask,right=rm==31?0:cpu.x[rm]&mask;
    uint64_t otherwise=op==0?right:op==1?right+1:op==2?mask-right:(0-right);
    if(rd!=31)registers[rd]=(truth(flags,cond)?left:otherwise)&mask;
    uint32_t word=instruction(width,op,cond,rn,rm,rd);uint8_t ram[8];memset(ram,0xa5,sizeof(ram));
    CHECK(vf_run(&cpu,(const uint8_t*)&word,4,ram,8,code,1,perms,0)==VF_BUDGET);
    CHECK(cpu.retired==1 && cpu.compiled_blocks==1 && cpu.pc==4);
    CHECK(cpu.pstate==(UINT64_C(0x12345000000003c5)|((uint64_t)flags<<28)));
    CHECK(cpu.sp==UINT64_C(0x9876543210));CHECK(!memcmp(registers,cpu.x,sizeof(registers)));
    for(unsigned i=0;i<8;i++)CHECK(ram[i]==0xa5);
    cases++;
}
int main(void) {
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);
    const uint64_t values[]={0,1,UINT64_MAX,0x7fffffff,0x80000000,0xffffffff,
        UINT64_C(0x8000000000000000),UINT64_C(0xabcdef0180000081)};
    for(unsigned width=32;width<=64;width+=32)for(unsigned op=0;op<4;op++)for(unsigned cond=0;cond<16;cond++)for(unsigned flags=0;flags<16;flags++)
        for(unsigned i=0;i<8;i++)for(unsigned mode=0;mode<8;mode++)one(&code,width,op,cond,flags,values[i],values[7-i],mode);
    for(unsigned width=32;width<=64;width+=32)for(unsigned op=0;op<4;op++)for(unsigned cond=0;cond<16;cond++)for(unsigned flags=0;flags<16;flags++)
        for(unsigned bad=1;bad<4;bad++) {
            uint32_t word=instruction(width,op,cond,0,1,2)|((bad&1)?1u<<29:0)|((bad&2)?1u<<11:0);
            vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=UINT64_MAX;cpu.x[1]=3;cpu.x[2]=7;cpu.sp=0x12345678;cpu.pstate=((uint64_t)flags<<28)|0x3c5;
            uint8_t ram[8]={0};CHECK(vf_run(&cpu,(const uint8_t*)&word,4,ram,8,&code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
            CHECK(cpu.pc==0 && cpu.retired==0 && cpu.x[0]==UINT64_MAX && cpu.x[1]==3 && cpu.x[2]==7 && cpu.sp==0x12345678);
            CHECK(cpu.pstate==(((uint64_t)flags<<28)|0x3c5));CHECK(cpu.instruction==word && cpu.esr_el[VF_EL1]==(1u<<25));
        }
    CHECK(munmap(code.bytes,4096)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"conditional_select_cases\":%u}\n",checks,cases);
}
