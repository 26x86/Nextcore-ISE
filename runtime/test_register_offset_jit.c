/* SPDX-License-Identifier: BSD-4-Clause; authored register-offset scalar proof. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t instruction(unsigned size,unsigned opc,unsigned option,unsigned scale,unsigned rn,unsigned rm,unsigned rt) {
    return 0x38200800u|(size<<30)|(opc<<22)|(rm<<16)|(option<<13)|(scale<<12)|(rn<<5)|rt;
}
static int valid(unsigned size,unsigned opc){return opc<2 || (size<3 && !(size==2 && opc==3));}
static uint64_t offset(uint64_t raw,unsigned option,unsigned shift) {
    __int128 value=(option&1)?(__int128)raw:(__int128)(raw&UINT32_MAX);
    if(option==6 && (raw&0x80000000u))value-=((__int128)1<<32);
    return (uint64_t)(value*((__int128)1<<shift));
}
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
    const uint64_t indices[]={0,1,UINT64_MAX,0x80000000,UINT64_C(0xdeadbeefffffffff),UINT64_C(0xdeadbeef00000001),
        UINT64_C(0x8000000000000000),UINT64_C(0x1234000000000040)};
    const unsigned options[]={2,3,6,7},targets[]={2,3,4,31};
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)if(valid(size,opc))
    for(unsigned opt=0;opt<4;opt++)for(unsigned scale=0;scale<2;scale++)for(unsigned sample=0;sample<8;sample++)
    for(unsigned sp=0;sp<2;sp++)for(unsigned zr=0;zr<2;zr++)for(unsigned target=0;target<4;target++) {
        unsigned bytes=1u<<size,option=options[opt],rn=sp?31:3,rm=zr?31:4,rt=targets[target];
        uint64_t raw=indices[sample],disp=offset(zr?0:raw,option,scale?size:0),base=BASE+512-disp;
        vf_cpu cpu;reset(&cpu);cpu.x[3]=base;cpu.sp=base;cpu.x[4]=raw;
        uint8_t ram[1024],expected[1024];for(unsigned i=0;i<1024;i++)ram[i]=(uint8_t)(i*19+sample*37);
        memcpy(expected,ram,sizeof(ram));uint64_t regs[32];memcpy(regs,cpu.x,sizeof(regs));
        if(opc) {if(rt!=31)regs[rt]=load_expected(ram+512,bytes,opc);}
        else {uint64_t value=rt==31?0:cpu.x[rt];for(unsigned i=0;i<bytes;i++)expected[512+i]=(uint8_t)(value>>(i*8));}
        CHECK(run(&cpu,&code,instruction(size,opc,option,scale,rn,rm,rt),ram,sizeof(ram))==VF_BUDGET);
        CHECK(cpu.pc==BASE+4 && cpu.retired==1 && cpu.compiled_blocks==1);
        CHECK(cpu.pstate==0xb00003c5 && cpu.sp==base && !memcmp(regs,cpu.x,sizeof(regs)));
        CHECK(!memcmp(expected,ram,sizeof(ram)));cases++;
    }
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)for(unsigned option=0;option<8;option++)
    for(unsigned scale=0;scale<2;scale++)for(unsigned vector=0;vector<2;vector++)if(!valid(size,opc)||!(option&2)||vector) {
        vf_cpu cpu;reset(&cpu);cpu.x[3]=BASE+128;cpu.x[4]=1;cpu.sp=BASE+256;
        uint8_t ram[512],before[512];memset(ram,0xa5,sizeof(ram));memcpy(before,ram,sizeof(ram));
        uint64_t regs[32];memcpy(regs,cpu.x,sizeof(regs));
        uint32_t word=instruction(size,opc,option,scale,3,4,2)|(vector<<26);
        CHECK(run(&cpu,&code,word,ram,sizeof(ram))==VF_UNDEFINED_INSTRUCTION);
        CHECK(cpu.retired==0 && cpu.pc==BASE && cpu.esr==(1u<<25) && cpu.instruction==word);
        CHECK(cpu.pstate==0xb00003c5 && cpu.sp==BASE+256 && !memcmp(regs,cpu.x,sizeof(regs)) && !memcmp(ram,before,sizeof(ram)));
    }
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)if(valid(size,opc))
    for(unsigned opt=0;opt<4;opt++)for(unsigned scale=0;scale<2;scale++)for(unsigned el=0;el<2;el++)for(unsigned fault=0;fault<4;fault++) {
        unsigned bytes=1u<<size;if(fault==3 && bytes==1)continue;
        unsigned option=options[opt];uint64_t disp=offset(UINT64_MAX,option,scale?size:0);
        uint64_t address=fault==0?BASE-bytes:fault==1?BASE+512:fault==2?BASE+128:BASE+129;
        size_t length=fault==2?128+bytes-1:512;
        vf_cpu cpu;reset(&cpu);CHECK(vf_cpu_set_current_el(&cpu,el)==0);cpu.x[3]=address-disp;cpu.x[4]=UINT64_MAX;cpu.sp=BASE+256;
        uint64_t flags=cpu.pstate,regs[32];memcpy(regs,cpu.x,sizeof(regs));
        uint8_t ram[512],before[512];memset(ram,0xa5,sizeof(ram));memcpy(before,ram,sizeof(ram));
        CHECK(run(&cpu,&code,instruction(size,opc,option,scale,3,4,4),ram,length)==(fault==3?VF_ALIGNMENT_FAULT:VF_DATA_ABORT));
        CHECK(cpu.pc==BASE && cpu.retired==0 && cpu.far==address);
        CHECK(cpu.esr==(((UINT64_C(0x24)+el)<<26)|(1u<<25)|(opc?0:64)|(fault==3?0x21:7)));
        CHECK(cpu.pstate==flags && cpu.sp==BASE+256 && !memcmp(regs,cpu.x,sizeof(regs)) && !memcmp(ram,before,sizeof(ram)));
    }
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)if(valid(size,opc))for(unsigned el=0;el<2;el++) {
        vf_cpu cpu;reset(&cpu);CHECK(vf_cpu_set_current_el(&cpu,el)==0);cpu.sctlr=el?8:16;cpu.sp=BASE+129;cpu.x[4]=UINT64_MAX;
        uint8_t ram[512],before[512];memset(ram,0xa5,sizeof(ram));memcpy(before,ram,sizeof(ram));
        uint64_t regs[32];memcpy(regs,cpu.x,sizeof(regs));
        CHECK(run(&cpu,&code,instruction(size,opc,3,0,31,4,4),ram,512)==VF_SP_ALIGNMENT_FAULT);
        CHECK(cpu.esr==0x9a000000 && cpu.far==BASE+129 && cpu.sp==BASE+129 && cpu.pc==BASE && cpu.retired==0);
        CHECK(!memcmp(regs,cpu.x,sizeof(regs)) && !memcmp(ram,before,sizeof(ram)));
    }
    CHECK(munmap(code.bytes,4096)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"register_offset_cases\":%u}\n",checks,cases);
}
