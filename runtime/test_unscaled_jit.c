/* SPDX-License-Identifier: BSD-4-Clause; authored unscaled scalar proof. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t instruction(unsigned size,unsigned opc,int displacement,unsigned rn,unsigned rt) {
    return 0x38000000u|(size<<30)|(opc<<22)|(((unsigned)displacement&511)<<12)|(rn<<5)|rt;
}
static int valid(unsigned size,unsigned opc){return opc<2 || (size<3 && !(size==2 && opc==3));}
static uint64_t load_expected(const uint8_t *ram,unsigned bytes,unsigned opc) {
    uint64_t raw=0;for(unsigned i=0;i<bytes;i++)raw|=(uint64_t)ram[i]<<(i*8);
    __int128 value=raw;
    if(opc>=2 && (raw&(UINT64_C(1)<<(bytes*8-1))))value-=((__int128)1<<(bytes*8));
    return opc==3 || (opc==1 && bytes<8)?(uint32_t)value:(uint64_t)value;
}
static const uint64_t BASE=UINT64_C(0x40000000);
static void reset(vf_cpu *cpu) {
    vf_cpu_reset(cpu,VF_EL1);cpu->pc=BASE;cpu->guest_ram_base=BASE;cpu->pstate=0xb00003c5;
    for(unsigned i=0;i<32;i++)cpu->x[i]=UINT64_C(0xabcdef1200000000)+i;
}
static int run(vf_cpu *cpu,vf_code *code,uint32_t word,uint8_t *ram,size_t size) {
    return vf_run(cpu,(const uint8_t*)&word,4,ram,size,code,1,perms,0);
}
int main(void) {
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);

    const int displacements[]={-256,-255,-1,0,1,254,255};
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)if(valid(size,opc))
    for(unsigned d=0;d<7;d++)for(unsigned sp=0;sp<2;sp++)for(unsigned target=0;target<3;target++) {
        unsigned bytes=1u<<size,rn=sp?31:3,rt=target==0?2:target==1?3:31;
        uint64_t base=BASE+512-displacements[d];vf_cpu cpu;reset(&cpu);cpu.x[3]=base;cpu.sp=base;
        uint8_t ram[1024],expected[1024];for(unsigned i=0;i<1024;i++)ram[i]=(uint8_t)(i*19+0x85);
        memcpy(expected,ram,sizeof(ram));uint64_t regs[32];memcpy(regs,cpu.x,sizeof(regs));
        if(opc){if(rt!=31)regs[rt]=load_expected(ram+512,bytes,opc);}
        else {uint64_t value=rt==31?0:cpu.x[rt];for(unsigned i=0;i<bytes;i++)expected[512+i]=(uint8_t)(value>>(i*8));}
        CHECK(run(&cpu,&code,instruction(size,opc,displacements[d],rn,rt),ram,sizeof(ram))==VF_BUDGET);
        CHECK(cpu.retired==1 && cpu.pc==BASE+4 && cpu.pstate==0xb00003c5 && cpu.sp==base);
        CHECK(!memcmp(cpu.x,regs,sizeof(regs)) && !memcmp(ram,expected,sizeof(ram)));cases++;
    }
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)if(valid(size,opc))
    for(unsigned fault=0;fault<3;fault++) {
        unsigned bytes=1u<<size;if(fault==2 && bytes==1)continue;
        uint64_t address=fault==0?BASE-8:fault==1?BASE+512:BASE+129;
        vf_cpu cpu;reset(&cpu);cpu.x[3]=address+256;uint8_t ram[512],before[512];memset(ram,0xa5,512);memcpy(before,ram,512);
        CHECK(run(&cpu,&code,instruction(size,opc,-256,3,2),ram,512)==(fault==2?VF_ALIGNMENT_FAULT:VF_DATA_ABORT));
        CHECK(cpu.pc==BASE && cpu.retired==0 && cpu.far==address && !memcmp(ram,before,512));
        CHECK(cpu.esr==(0x96000000u|(opc?0:64)|(fault==2?0x21:7)));
    }
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)for(unsigned variant=0;variant<5;variant++) {
        if(valid(size,opc)&&variant==0)continue;
        uint32_t word=instruction(size,opc,-1,3,2)|(variant==1?1u<<26:variant==2?2u<<10:variant==3?1u<<10:variant==4?3u<<10:0);
        vf_cpu cpu;reset(&cpu);cpu.x[3]=BASE+128;uint8_t ram[512],before[512];memset(ram,0xa5,512);memcpy(before,ram,512);
        CHECK(run(&cpu,&code,word,ram,512)==VF_UNDEFINED_INSTRUCTION);
        CHECK(cpu.retired==0 && cpu.pc==BASE && !memcmp(ram,before,512));
    }
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)if(valid(size,opc)) {
        vf_cpu cpu;reset(&cpu);cpu.sctlr=8;cpu.sp=BASE+129;uint8_t ram[512],before[512];memset(ram,0xa5,512);memcpy(before,ram,512);
        CHECK(run(&cpu,&code,instruction(size,opc,-1,31,2),ram,512)==VF_SP_ALIGNMENT_FAULT);
        CHECK(cpu.retired==0 && cpu.pc==BASE && cpu.sp==BASE+129 && cpu.esr==0x9a000000 && !memcmp(ram,before,512));
    }
    CHECK(munmap(code.bytes,4096)==0);
    printf("{\"passed\":true,\"cases\":%u,\"assertions\":%u}\n",cases,checks);
}
