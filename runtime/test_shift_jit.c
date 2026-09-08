/* SPDX-License-Identifier: BSD-4-Clause */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void *p,size_t n,int x,void *o) {(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
int main(void) {
    uint8_t ram[8]={0};vf_cpu cpu;
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};
    CHECK(code.bytes!=MAP_FAILED);
    const uint64_t pairs[][2]={{0,1},{UINT64_MAX,UINT64_MAX},
        {UINT64_C(0x7fffffff7fffffff),1},{UINT64_C(0x8000000080000000),UINT64_C(0xffffffff80000001)}};
    for(unsigned width=32;width<=64;width+=32)for(unsigned shift=0;shift<3;shift++)
    for(unsigned amount=0;amount<width;amount++)for(unsigned sub=0;sub<2;sub++)
    for(unsigned flags=0;flags<2;flags++)for(unsigned p=0;p<4;p++) {
        uint64_t mask=width==64?UINT64_MAX:UINT64_C(0xffffffff);
        uint64_t a=pairs[p][0]&mask,b=pairs[p][1]&mask;
        uint64_t sign=UINT64_C(1)<<(width-1);
        if(shift==0)b=(b<<amount)&mask;
        else {int neg=shift==2 && (b&sign);b>>=amount;if(neg && amount)b|=mask^(mask>>amount);}
        uint64_t result=(sub?a-b:a+b)&mask;
        unsigned carry=sub?(a>=b):(((__uint128_t)a+b)>>width)!=0;
        unsigned overflow=((a^result)&(sub?(a^b):~(a^b))&sign)!=0;
        unsigned nzcv=flags?((result&sign)?8u:0u)|((result==0)?4u:0u)|(carry<<1)|overflow:15u;
        uint32_t word=0x0b000002u|((width==64?1u:0u)<<31)|(sub<<30)|(flags<<29)|
            (shift<<22)|(1u<<16)|(amount<<10);
        vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=pairs[p][0];cpu.x[1]=pairs[p][1];cpu.sp=0x9876;
        cpu.pstate|=UINT64_C(0xf00003c0);
        CHECK(vf_run(&cpu,(uint8_t*)&word,4,ram,sizeof(ram),&code,1,perms,0)==VF_BUDGET);
        CHECK(cpu.x[2]==result && cpu.retired==1 && cpu.pc==4 && cpu.sp==0x9876);
        CHECK((cpu.pstate>>28&15)==nzcv && (cpu.pstate&0x3cf)==0x3c5);cases++;
    }
    const uint32_t invalid[]={0x0bc00000,0x8bc00000,0x0b008000,0x2b00fc00};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=UINT64_MAX;cpu.pstate|=UINT64_C(0xf0000000);
        CHECK(vf_run(&cpu,(uint8_t*)&invalid[i],4,ram,sizeof(ram),&code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
        CHECK(cpu.retired==0 && cpu.pc==0 && cpu.x[0]==UINT64_MAX && cpu.pstate>>28==15);
    }
    CHECK(munmap(code.bytes,code.capacity)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"shift_cases\":%u}\n",checks,cases);
}
