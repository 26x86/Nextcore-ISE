/* SPDX-License-Identifier: BSD-4-Clause; authored per-destination-bit BFM oracle. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases,invalid;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t instruction(unsigned width,unsigned r,unsigned s,unsigned rn,unsigned rd) {
    return 0x33000000u|((width==64u)<<31)|((width==64u)<<22)|(r<<16)|(s<<10)|(rn<<5)|rd;
}
static const unsigned roles[][2]={{0,2},{31,2},{0,31},{0,0},{31,31}};
static const uint64_t sources[]={0,UINT64_MAX,UINT64_C(0xaaaaaaaa55555555),UINT64_C(0x55555555aaaaaaaa),0x80000000,UINT64_C(0x8000000000000000)};
static const uint64_t destinations[]={0,UINT64_MAX,UINT64_C(0xa5a5a5a55a5a5a5a),UINT64_C(0x0123456789abcdef)};
static uint64_t oracle(unsigned width,unsigned r,unsigned s,uint64_t src,uint64_t old) {
    uint64_t out=0;
    for(unsigned bit=0;bit<width;bit++) {
        unsigned first=s>=r?0:width-r,last=s>=r?s-r:width-r+s;
        unsigned value=bit<first||bit>last?(unsigned)((old>>bit)&1):(unsigned)((src>>(s>=r?bit+r:bit-first))&1);
        if(value)out|=UINT64_C(1)<<bit;
    }
    return out;
}
static void one(vf_code *code,unsigned width,unsigned r,unsigned s,unsigned role,uint64_t source,uint64_t old,unsigned flags) {
    unsigned rn=roles[role][0],rd=roles[role][1];vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.sp=UINT64_C(0x8765432100);
    for(unsigned i=0;i<32;i++)cpu.x[i]=UINT64_C(0xfedcba9800000000)+i;
    if(rd!=31)cpu.x[rd]=old;if(rn!=31)cpu.x[rn]=source;
    cpu.pstate=UINT64_C(0x12345000000003c5)|((uint64_t)flags<<28);uint64_t expected[32];memcpy(expected,cpu.x,sizeof(expected));
    uint64_t result=oracle(width,r,s,rn==31?0:expected[rn],rd==31?0:expected[rd]);if(rd!=31)expected[rd]=result;
    uint32_t w=instruction(width,r,s,rn,rd);uint8_t ram[8];memset(ram,0xa5,8);
    CHECK(vf_run(&cpu,(const uint8_t*)&w,4,ram,8,code,1,perms,0)==VF_BUDGET);
    CHECK(cpu.pc==4 && cpu.retired==1 && cpu.compiled_blocks==1);
    CHECK(!memcmp(expected,cpu.x,sizeof(expected)) && cpu.sp==UINT64_C(0x8765432100));
    CHECK(cpu.pstate==(UINT64_C(0x12345000000003c5)|((uint64_t)flags<<28)));
    for(unsigned i=0;i<8;i++)CHECK(ram[i]==0xa5);cases++;
}
static void reject(vf_code *code,uint32_t word) {
    vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=UINT64_MAX;cpu.x[2]=0x987654321;cpu.sp=0x123456780;cpu.pstate=0xb00003c5;
    uint64_t before[32];memcpy(before,cpu.x,sizeof(before));uint8_t ram[8]={0};
    CHECK(vf_run(&cpu,(const uint8_t*)&word,4,ram,8,code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
    CHECK(cpu.retired==0 && cpu.pc==0 && !memcmp(before,cpu.x,sizeof(before)));
    CHECK(cpu.sp==0x123456780 && cpu.pstate==0xb00003c5);invalid++;
}
int main(void) {
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);
    for(unsigned width=32;width<=64;width+=32)for(unsigned r=0;r<width;r++)for(unsigned s=0;s<width;s++)
    for(unsigned a=0;a<6;a++)for(unsigned d=0;d<4;d++)for(unsigned role=0;role<5;role++)one(&code,width,r,s,role,sources[a],destinations[d],(r+s+a+d+role)&15);
    for(unsigned width=32;width<=64;width+=32)for(unsigned flags=0;flags<16;flags++)for(unsigned role=0;role<5;role++)for(unsigned edge=0;edge<4;edge++)
        one(&code,width,edge&1?width-1:0,edge&2?width-1:0,role,UINT64_MAX,UINT64_C(0x0123456789abcdef),flags);
    for(unsigned sf=0;sf<2;sf++)for(unsigned n=0;n<2;n++)for(unsigned r=0;r<64;r++)for(unsigned s=0;s<64;s++) {
        if(sf==n && (sf || (r<32 && s<32)))continue;
        reject(&code,0x33000002u|(sf<<31)|(n<<22)|(r<<16)|(s<<10));
    }
    const uint32_t neighbors[]={0x33800002,0xb3c00002,0x73000002,0xf3400002,0x13000002,0x93400002};
    for(unsigned i=0;i<sizeof(neighbors)/sizeof(neighbors[0]);i++)reject(&code,neighbors[i]);
    CHECK(munmap(code.bytes,code.capacity)==0);printf("BFM native cases=%u invalid=%u assertions=%u\n",cases,invalid,checks);return 0;
}
