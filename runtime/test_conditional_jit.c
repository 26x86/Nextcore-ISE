/* SPDX-License-Identifier: BSD-4-Clause; authored arithmetic/condition oracle. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static unsigned predicate(unsigned condition,unsigned flags){
    unsigned n=(flags>>3)&1,z=(flags>>2)&1,c=(flags>>1)&1,v=flags&1;
    const unsigned table[]={z,!z,c,!c,n,!n,v,!v,c&&!z,!c||z,n==v,n!=v,!z&&n==v,z||n!=v,1,1};
    return table[condition];
}
static unsigned expected_flags(uint64_t a,uint64_t b,unsigned width,unsigned sub){
    uint64_t mask=width==64?UINT64_MAX:UINT32_MAX,sign=UINT64_C(1)<<(width-1);a&=mask;b&=mask;
    uint64_t value=(sub?a-b:a+b)&mask;
    unsigned carry=sub?a>=b:((__uint128_t)a+b)>mask;
    __int128 signed_a=(__int128)a-((a&sign)?((__int128)1<<width):0);
    __int128 signed_b=(__int128)b-((b&sign)?((__int128)1<<width):0);
    __int128 signed_result=sub?signed_a-signed_b:signed_a+signed_b;
    unsigned overflow=signed_result<-(__int128)sign || signed_result>=(__int128)sign;
    return ((value&sign)?8:0)|(value==0?4:0)|(carry<<1)|overflow;
}
static uint32_t instruction(unsigned width,unsigned sub,unsigned immediate,unsigned cond,unsigned fallback,unsigned rn,unsigned rm){
    return UINT32_C(0x3a400000)|((width==64?1u:0u)<<31)|(sub<<30)|(immediate<<11)|(cond<<12)|fallback|(rn<<5)|(rm<<16);
}
static void one(vf_code*code,unsigned width,unsigned sub,unsigned immediate,unsigned cond,unsigned old,unsigned fallback,uint64_t a,uint64_t b,unsigned rn,unsigned rm){
    vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.guest_ram_base=UINT64_C(0x40000000);cpu.pc=cpu.guest_ram_base;
    for(unsigned r=0;r<32;r++)cpu.x[r]=UINT64_C(0xfedc000000000000)+r;
    cpu.x[0]=a;cpu.x[1]=b;cpu.sp=UINT64_C(0x98765430);cpu.pstate=UINT64_C(0xabcdef00000003c5)|((uint64_t)old<<28);
    uint64_t registers[32];memcpy(registers,cpu.x,sizeof(registers));
    uint32_t word=instruction(width,sub,immediate,cond,fallback,rn,immediate?(unsigned)b:rm);
    uint64_t left=rn==31?0:a,right=immediate?b:rm==31?0:b;
    unsigned expected=predicate(cond,old)?expected_flags(left,right,width,sub):fallback;
    uint8_t ram[8]={0};
    CHECK(vf_run(&cpu,(const uint8_t*)&word,4,ram,sizeof(ram),code,1,perms,0)==VF_BUDGET);
    CHECK(cpu.retired==1 && cpu.compiled_blocks==1 && cpu.pc==UINT64_C(0x40000004));
    CHECK(cpu.pstate==(UINT64_C(0xabcdef00000003c5)|((uint64_t)expected<<28)));
    CHECK(cpu.sp==UINT64_C(0x98765430) && !memcmp(registers,cpu.x,sizeof(registers)));cases++;
}
int main(void){
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);
    const uint64_t edges[]={0,1,31,UINT64_C(0x7fffffff),UINT64_C(0x80000000),UINT64_C(0xffffffff),
        UINT64_C(0x7fffffffffffffff),UINT64_C(0x8000000000000000),UINT64_MAX};
    for(unsigned width=32;width<=64;width+=32)for(unsigned sub=0;sub<2;sub++)for(unsigned immediate=0;immediate<2;immediate++)
    for(unsigned cond=0;cond<16;cond++)for(unsigned old=0;old<16;old++)for(unsigned fallback=0;fallback<16;fallback++){
        unsigned i=(old+fallback+cond)%9;
        one(&code,width,sub,immediate,cond,old,fallback,edges[i],immediate?(old+fallback)%32:edges[(i+3)%9],0,1);
    }
    for(unsigned width=32;width<=64;width+=32)for(unsigned sub=0;sub<2;sub++){
        for(unsigned imm=0;imm<32;imm++)for(unsigned a=0;a<9;a++)one(&code,width,sub,1,15,15,0,edges[a],imm,0,1);
        for(unsigned a=0;a<9;a++)for(unsigned b=0;b<9;b++)one(&code,width,sub,0,14,15,0,edges[a],edges[b],0,1);
        for(unsigned rn=0;rn<2;rn++)for(unsigned rm=0;rm<2;rm++)one(&code,width,sub,0,15,0,15,UINT64_MAX,UINT64_MAX,rn?31:0,rm?31:1);
        one(&code,width,sub,1,15,0,15,UINT64_MAX,31,31,1);
    }
    {
        uint32_t program[]={0xfa41e000,0x3a41100f,0xfa5ff800,0x54000040,0,0xd4400000};
        vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=31;cpu.x[1]=31;cpu.sp=0x9870;
        uint8_t ram[8]={0};CHECK(vf_run(&cpu,(const uint8_t*)program,sizeof(program),ram,8,&code,8,perms,0)==VF_HALT);
        CHECK(cpu.retired==5 && cpu.compiled_blocks==2 && cpu.pc==24 && cpu.pstate>>28==6);
        CHECK(cpu.x[0]==31 && cpu.x[1]==31 && cpu.sp==0x9870);
    }
    for(unsigned bit=0;bit<3;bit++){
        const unsigned positions[]={29,10,4};uint32_t word=instruction(64,1,0,0,15,0,1)^(1u<<positions[bit]);
        vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=11;cpu.x[1]=12;cpu.sp=0x9870;cpu.pstate=UINT64_C(0xb00003c5);
        uint8_t ram[8]={0};CHECK(vf_run(&cpu,(const uint8_t*)&word,4,ram,8,&code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
        CHECK(cpu.retired==0 && cpu.pc==0 && cpu.x[0]==11 && cpu.x[1]==12 && cpu.sp==0x9870 && cpu.pstate==UINT64_C(0xb00003c5));
        CHECK(cpu.esr_el[VF_EL1]==0);
    }
    CHECK(munmap(code.bytes,4096)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"conditional_cases\":%u}\n",checks,cases);
}
