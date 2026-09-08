/* SPDX-License-Identifier: BSD-4-Clause; wholly authored UBFM execution proof. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t instruction(unsigned width,unsigned r,unsigned s,unsigned rn,unsigned rd) {
    return UINT32_C(0x53000000)|((width==64?1u:0u)<<31)|((width==64?1u:0u)<<22)|(r<<16)|(s<<10)|(rn<<5)|rd;
}
static uint64_t expected(uint64_t source,unsigned width,unsigned r,unsigned s) {
    uint64_t value=0;
    for(unsigned bit=0;bit<width;bit++) {
        if(!(source&(UINT64_C(1)<<bit)))continue;
        if(s>=r && bit>=r && bit<=s)value|=UINT64_C(1)<<(bit-r);
        if(s<r && bit<=s)value|=UINT64_C(1)<<(bit+width-r);
    }
    return value;
}
static void one(vf_code*code,unsigned width,unsigned r,unsigned s,uint64_t source,unsigned rn,unsigned rd,unsigned flags) {
    vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.guest_ram_base=0x40000000;cpu.pc=cpu.guest_ram_base;
    for(unsigned reg=0;reg<32;reg++)cpu.x[reg]=UINT64_C(0xfedcba9800000000)+reg;
    if(rn!=31)cpu.x[rn]=source;
    cpu.sp=UINT64_C(0x98765430);cpu.pstate=UINT64_C(0xabcdef00000003c5)|((uint64_t)flags<<28);
    uint64_t registers[32];memcpy(registers,cpu.x,sizeof(registers));
    if(rd!=31)registers[rd]=expected(rn==31?0:source,width,r,s);
    uint32_t word=instruction(width,r,s,rn,rd);uint8_t ram[8];memset(ram,0xa5,sizeof(ram));
    CHECK(vf_run(&cpu,(const uint8_t*)&word,4,ram,sizeof(ram),code,1,perms,0)==VF_BUDGET);
    CHECK(cpu.retired==1 && cpu.compiled_blocks==1 && cpu.pc==UINT64_C(0x40000004));
    CHECK(cpu.pstate==(UINT64_C(0xabcdef00000003c5)|((uint64_t)flags<<28)));
    CHECK(cpu.sp==UINT64_C(0x98765430) && !memcmp(registers,cpu.x,sizeof(registers)));
    for(unsigned i=0;i<sizeof(ram);i++)CHECK(ram[i]==0xa5);
    cases++;
}
int main(void) {
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);
    const uint64_t sources[]={0,UINT64_MAX,1,UINT64_C(0x8000000000000000),UINT64_C(0x80000000),
        UINT64_C(0x0123456789abcdef),UINT64_C(0xdeadbeef00000001)};
    for(unsigned width=32;width<=64;width+=32)for(unsigned r=0;r<width;r++)for(unsigned s=0;s<width;s++)
        for(unsigned i=0;i<sizeof(sources)/sizeof(sources[0]);i++)one(&code,width,r,s,sources[i],0,2,(r+s+i)&15);
    for(unsigned width=32;width<=64;width+=32)for(unsigned flags=0;flags<16;flags++)
        for(unsigned mode=0;mode<4;mode++)for(unsigned edge=0;edge<4;edge++)
            one(&code,width,edge&1?width-1:0,edge&2?width-1:0,UINT64_MAX,mode&1?31:0,mode&2?31:0,flags);
    const uint32_t invalid[]={0xd3000002,0x53400002,0x53200002,0x53008002,0x53608002,
        0x33000002,0xb3400002,0x13000002,0x93400002,0x73000002,0xf3400002};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=UINT64_MAX;cpu.x[2]=0x76543210;cpu.sp=0x9870;cpu.pstate=0xb00003c5;
        uint8_t ram[8]={0};CHECK(vf_run(&cpu,(const uint8_t*)&invalid[i],4,ram,8,&code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
        CHECK(cpu.retired==0 && cpu.pc==0 && cpu.x[0]==UINT64_MAX && cpu.x[2]==0x76543210 && cpu.sp==0x9870 && cpu.pstate==0xb00003c5);
        CHECK(cpu.esr_el[VF_EL1]==(1u<<25) && cpu.instruction==invalid[i]);
    }
    {
        uint32_t program[]={0x7100141f,0xd341fc02,0x54000040,0,0xd4400000};
        vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=5;uint8_t ram[8]={0};
        CHECK(vf_run(&cpu,(const uint8_t*)program,sizeof(program),ram,8,&code,8,perms,0)==VF_HALT);
        CHECK(cpu.retired==4 && cpu.pc==20 && cpu.x[0]==5 && cpu.x[2]==2 && (cpu.pstate>>28)==6);
    }
    CHECK(munmap(code.bytes,4096)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"ubfm_cases\":%u}\n",checks,cases);
}
