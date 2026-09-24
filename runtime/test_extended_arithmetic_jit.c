/* SPDX-License-Identifier: BSD-4-Clause; independently authored A64 arithmetic proof. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t instruction(unsigned width,unsigned op,unsigned option,unsigned shift,unsigned rn,unsigned rm,unsigned rd) {
    return 0x0b200000u|((width==64?1u:0u)<<31)|(op<<29)|(rm<<16)|(option<<13)|(shift<<10)|(rn<<5)|rd;
}
static uint64_t extend(uint64_t source,unsigned width,unsigned option,unsigned shift) {
    unsigned bits=8u<<(option&3);if(bits>width)bits=width;
    uint64_t out=0;
    for(unsigned i=0;i<width;i++) {
        unsigned source_bit=i<bits?i:bits-1;
        if(i>=bits && !(option&4))continue;
        if(i+shift<width && ((source>>source_bit)&1))out|=UINT64_C(1)<<(i+shift);
    }
    return out;
}
static void one(vf_code*code,unsigned width,unsigned op,unsigned option,unsigned shift,uint64_t a,uint64_t b,unsigned mode) {
    unsigned rn=mode==1?31:0,rm=mode==2?31:1,rd=mode==3?31:mode==4?0:mode==5?1:2;
    vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.pc=0;
    for(unsigned i=0;i<32;i++)cpu.x[i]=UINT64_C(0xabcdef1200000000)+i;
    cpu.x[0]=a;cpu.x[1]=b;cpu.sp=a;cpu.pstate=UINT64_C(0x12345000f00003c5);
    uint64_t registers[32];memcpy(registers,cpu.x,sizeof(registers));
    uint64_t mask=width==64?UINT64_MAX:UINT32_MAX,sign=UINT64_C(1)<<(width-1);
    a&=mask;b=extend(rm==31?0:b,width,option,shift);
    uint64_t result=(op&2?a-b:a+b)&mask;
    unsigned carry=op&2?a>=b:((__uint128_t)a+b)>mask;
    unsigned overflow=!!((a^result)&(op&2?a^b:~(a^b))&sign);
    uint64_t pstate=cpu.pstate;
    if(op&1)pstate=(pstate&~UINT64_C(0xf0000000))|((uint64_t)((!!(result&sign)<<3)|((result==0)<<2)|(carry<<1)|overflow)<<28);
    uint64_t sp=cpu.sp;
    if(rd!=31)registers[rd]=result;else if(!(op&1))sp=result;
    uint32_t word=instruction(width,op,option,shift,rn,rm,rd);uint8_t ram[8];memset(ram,0xa5,sizeof(ram));
    CHECK(vf_run(&cpu,(const uint8_t*)&word,4,ram,8,code,1,perms,0)==VF_BUDGET);
    CHECK(cpu.retired==1 && cpu.compiled_blocks==1 && cpu.pc==4);
    CHECK(cpu.pstate==pstate);CHECK(cpu.sp==sp);CHECK(!memcmp(registers,cpu.x,sizeof(registers)));
    for(unsigned i=0;i<8;i++)CHECK(ram[i]==0xa5);
    cases++;
}
int main(void) {
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);
    const uint64_t values[]={0,1,UINT64_MAX,0x7f,0x80,0xff,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,0xffffffff,
        UINT64_C(0x7fffffffffffffff),UINT64_C(0x8000000000000000),UINT64_C(0xabcdef0180000081)};
    for(unsigned width=32;width<=64;width+=32)for(unsigned op=0;op<4;op++)for(unsigned option=0;option<8;option++)for(unsigned shift=0;shift<=4;shift++)
        for(unsigned a=0;a<15;a++)for(unsigned b=0;b<15;b++)one(&code,width,op,option,shift,values[a],values[b],0);
    for(unsigned width=32;width<=64;width+=32)for(unsigned op=0;op<4;op++)for(unsigned option=0;option<8;option++)for(unsigned shift=0;shift<=4;shift++)
        for(unsigned mode=1;mode<=5;mode++)for(unsigned edge=0;edge<15;edge++)one(&code,width,op,option,shift,values[edge],values[14-edge],mode);
    for(unsigned width=32;width<=64;width+=32)for(unsigned op=0;op<4;op++)for(unsigned option=0;option<8;option++)for(unsigned bad=0;bad<6;bad++) {
        uint32_t word=instruction(width,op,option,bad<3?bad+5:0,31,1,31);
        if(bad>=3)word|=(bad-2)<<22;
        vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[1]=UINT64_MAX;cpu.sp=0x12345678;cpu.pstate=0xf00003c5;
        uint8_t ram[8]={0};CHECK(vf_run(&cpu,(const uint8_t*)&word,4,ram,8,&code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
        CHECK(cpu.pc==0 && cpu.retired==0 && cpu.x[1]==UINT64_MAX && cpu.sp==0x12345678 && cpu.pstate==0xf00003c5);
        CHECK(cpu.instruction==word && cpu.esr_el[VF_EL1]==(1u<<25));
    }
    CHECK(munmap(code.bytes,4096)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"extended_cases\":%u}\n",checks,cases);
}
