/* SPDX-License-Identifier: BSD-4-Clause; independent low-width multiply oracle. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t instruction(unsigned wide,unsigned sub,unsigned rn,unsigned rm,unsigned ra,unsigned rd) {
    return 0x1b000000u|(wide<<31)|(sub<<15)|(rm<<16)|(ra<<10)|(rn<<5)|rd;
}
static const unsigned roles[][4]={{0,1,2,3},{31,1,2,3},{0,31,2,3},{0,1,31,3},{0,1,2,31},{0,1,2,0},
    {0,1,2,1},{0,1,2,2},{0,0,0,0},{31,31,31,31},{30,29,28,27},{1,1,1,1}};
static const uint64_t values[]={0,1,UINT64_MAX,0x7fffffff,0x80000000,0xffffffff,UINT64_C(0x8000000000000000),UINT64_C(0xabcdef0187654321)};
static void one(vf_code *code,unsigned wide,unsigned sub,unsigned flags,unsigned role,uint64_t a,uint64_t b,uint64_t c) {
    unsigned rn=roles[role][0],rm=roles[role][1],ra=roles[role][2],rd=roles[role][3];
    vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.sp=UINT64_C(0x8765432100);
    for(unsigned i=0;i<32;i++)cpu.x[i]=UINT64_C(0xfedcba9800000000)+i;
    if(rn!=31)cpu.x[rn]=a;if(rm!=31)cpu.x[rm]=b;if(ra!=31)cpu.x[ra]=c;
    cpu.pstate=UINT64_C(0x12345000000003c5)|((uint64_t)flags<<28);
    uint64_t expected[32];memcpy(expected,cpu.x,sizeof(expected));uint64_t mask=wide?UINT64_MAX:UINT32_MAX;
    __uint128_t n=rn==31?0:expected[rn]&mask,m=rm==31?0:expected[rm]&mask,acc=ra==31?0:expected[ra]&mask;
    __uint128_t product=n*m,total=sub?((__uint128_t)mask+1+acc-(product&mask)):acc+product;
    if(rd!=31)expected[rd]=(uint64_t)total&mask;
    uint32_t w=instruction(wide,sub,rn,rm,ra,rd);uint8_t ram[8];memset(ram,0xa5,8);
    CHECK(vf_run(&cpu,(const uint8_t*)&w,4,ram,8,code,1,perms,0)==VF_BUDGET);
    CHECK(cpu.pc==4 && cpu.retired==1 && cpu.compiled_blocks==1);
    CHECK(!memcmp(expected,cpu.x,sizeof(expected)) && cpu.sp==UINT64_C(0x8765432100));
    CHECK(cpu.pstate==(UINT64_C(0x12345000000003c5)|((uint64_t)flags<<28)));
    for(unsigned i=0;i<8;i++)CHECK(ram[i]==0xa5);cases++;
}
int main(void) {
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);
    for(unsigned wide=0;wide<2;wide++)for(unsigned sub=0;sub<2;sub++)for(unsigned flags=0;flags<16;flags++)
    for(unsigned r=0;r<12;r++)for(unsigned a=0;a<8;a++)for(unsigned b=0;b<8;b++)for(unsigned c=0;c<8;c++)one(&code,wide,sub,flags,r,values[a],values[b],values[c]);
    for(unsigned sf=0;sf<2;sf++)for(unsigned op54=0;op54<4;op54++)for(unsigned op31=0;op31<8;op31++)for(unsigned sub=0;sub<2;sub++) {
        if(!op54&&!op31)continue;
        uint32_t w=instruction(sf,sub,0,1,31,2)|(op54<<29)|(op31<<21);vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);
        cpu.x[0]=17;cpu.x[1]=23;cpu.x[2]=31;cpu.sp=0x1000;cpu.pstate=0xb00003c5;uint8_t ram[8]={0};
        CHECK(vf_run(&cpu,(const uint8_t*)&w,4,ram,8,&code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
        CHECK(cpu.retired==0 && cpu.pc==0 && cpu.x[0]==17 && cpu.x[1]==23 && cpu.x[2]==31 && cpu.sp==0x1000 && cpu.pstate==0xb00003c5);
    }
    CHECK(munmap(code.bytes,code.capacity)==0);printf("multiply-add native cases=%u assertions=%u\n",cases,checks);return 0;
}
