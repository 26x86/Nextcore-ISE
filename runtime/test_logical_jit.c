/* SPDX-License-Identifier: BSD-4-Clause */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void *p,size_t n,int x,void *o) {(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t encoding(unsigned width,unsigned op,unsigned size,unsigned ones,unsigned rot,unsigned rn,unsigned rd) {
    unsigned imms=(((~(size-1))<<1)&63)|(ones-1);
    return 0x12000000u|((width==64?1u:0u)<<31)|(op<<29)|((size==64?1u:0u)<<22)|(rot<<16)|(imms<<10)|(rn<<5)|rd;
}
static uint64_t expected_mask(unsigned width,unsigned size,unsigned ones,unsigned rot) {
    uint64_t value=0;
    for(unsigned bit=0;bit<width;bit++)if((bit+rot)%size<ones)value|=UINT64_C(1)<<bit;
    return value;
}
int main(void) {
    uint8_t ram[8]={0};vf_cpu cpu;
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};
    CHECK(code.bytes!=MAP_FAILED);
    const uint64_t input=UINT64_C(0x91a2b3c4d5e6f780);
    for(unsigned width=32;width<=64;width+=32)for(unsigned size=2;size<=width;size*=2)
    for(unsigned ones=1;ones<size;ones++)for(unsigned rot=0;rot<size;rot++)for(unsigned op=0;op<4;op++) {
        uint32_t w=encoding(width,op,size,ones,rot,0,1);
        uint64_t mask=expected_mask(width,size,ones,rot);
        uint64_t result=op==1?input|mask:op==2?input^mask:input&mask;
        if(width==32)result&=UINT64_C(0xffffffff);
        vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=input;cpu.pstate|=UINT64_C(0xf0000000);
        CHECK(vf_run(&cpu,(uint8_t*)&w,4,ram,sizeof(ram),&code,1,perms,0)==VF_BUDGET);
        CHECK(cpu.x[1]==result && cpu.pc==4 && cpu.retired==1 && cpu.compiled_blocks==1);
        unsigned nzcv=op==3?((unsigned)(result>>(width-1))<<3)|((result==0)?4u:0u):15u;
        CHECK((cpu.pstate>>28&15)==nzcv);cases++;
    }
    for(unsigned width=32;width<=64;width+=32)for(unsigned op=0;op<4;op++) {
        vf_cpu_reset(&cpu,VF_EL1);cpu.sp=UINT64_MAX;cpu.x[31]=UINT64_MAX;cpu.pstate|=UINT64_C(0xf0000000);
        uint32_t w=encoding(width,op,8,3,2,31,31);
        uint64_t mask=expected_mask(width,8,3,2);
        CHECK(vf_run(&cpu,(uint8_t*)&w,4,ram,sizeof(ram),&code,1,perms,0)==VF_BUDGET);
        CHECK(cpu.sp==(op==3?UINT64_MAX:(op==1 || op==2)?mask:0));
        CHECK(cpu.x[31]==UINT64_MAX && (cpu.pstate>>28&15)==(op==3?4u:15u));
    }
    /* immr's unused high bits are legal aliases for narrow elements. */
    uint32_t alias=encoding(64,1,2,1,63,31,0);
    vf_cpu_reset(&cpu,VF_EL1);
    CHECK(vf_run(&cpu,(uint8_t*)&alias,4,ram,sizeof(ram),&code,1,perms,0)==VF_BUDGET);
    CHECK(cpu.x[0]==UINT64_C(0xaaaaaaaaaaaaaaaa));
    uint32_t invalid[]={0x12400000,0x9240fc00,0x9200fc00,0x9200f800};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=input;cpu.pstate|=UINT64_C(0xf0000000);
        CHECK(vf_run(&cpu,(uint8_t*)&invalid[i],4,ram,sizeof(ram),&code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
        CHECK(cpu.x[0]==input && cpu.pc==0 && cpu.retired==0 && (cpu.pstate>>28&15)==15);
    }
    CHECK(munmap(code.bytes,code.capacity)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"logical_cases\":%u}\n",checks,cases);
}
