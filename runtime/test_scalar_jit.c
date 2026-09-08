/* SPDX-License-Identifier: BSD-4-Clause; independently authored ISA tests. */
#include "boot_jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t scalar(unsigned size,unsigned opc,unsigned displacement,unsigned rn,unsigned rt){
    return UINT32_C(0x39000000)|(size<<30)|(opc<<22)|(displacement<<10)|(rn<<5)|rt;
}
static int valid(unsigned size,unsigned opc){return opc<2 || (size<3 && !(size==2 && opc==3));}
static uint64_t read_value(const uint8_t*p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static uint64_t extended(uint64_t v,unsigned bytes,unsigned opc){
    if(opc>=2){uint64_t mask=(UINT64_C(1)<<(bytes*8))-1;
        if(v&(UINT64_C(1)<<(bytes*8-1)))v|=~mask;}
    return opc==3 || (opc==1 && bytes<8)?(uint32_t)v:v;
}
static const uint64_t base=UINT64_C(0x40000000),source=UINT64_C(0x89abcdef76543280);
static void reset(vf_cpu*c){vf_cpu_reset(c,VF_EL1);c->pc=base;c->guest_ram_base=base;c->pstate=UINT64_C(0xb00003c5);}
static int run(vf_cpu*c,uint32_t w,uint8_t*ram,size_t size,vf_code*code){
    return vf_run(c,(const uint8_t*)&w,4,ram,size,code,1,perms,0);
}
int main(void){
    uint8_t ram[65536],before[65536];vf_cpu c;
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};
    CHECK(code.bytes!=MAP_FAILED);memset(ram,0xa5,sizeof(ram));
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)if(valid(size,opc))
    for(unsigned displacement=0;displacement<4096;displacement++)for(unsigned use_sp=0;use_sp<2;use_sp++){
        unsigned bytes=1u<<size,at=1024+displacement*bytes;
        for(unsigned i=0;i<bytes;i++)ram[at+i]=(uint8_t)(displacement*37u+i*19u);
        ram[at-1]=0x52;ram[at+bytes]=0xe7;
        uint64_t wanted=extended(read_value(ram+at,bytes),bytes,opc);
        reset(&c);c.sp=base+1024;c.x[3]=base+1024;c.x[4]=source;
        CHECK(run(&c,scalar(size,opc,displacement,use_sp?31:3,4),ram,sizeof(ram),&code)==VF_BUDGET);
        CHECK(c.retired==1 && c.compiled_blocks==1 && c.pc==base+4 && c.pstate==UINT64_C(0xb00003c5));
        CHECK(c.sp==base+1024 && c.x[3]==base+1024 && ram[at-1]==0x52 && ram[at+bytes]==0xe7);
        if(opc)CHECK(c.x[4]==wanted);
        else {uint64_t mask=bytes==8?UINT64_MAX:(UINT64_C(1)<<(bytes*8))-1;
            CHECK(c.x[4]==source && read_value(ram+at,bytes)==(source&mask));}
        cases++;
    }
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)if(valid(size,opc)){
        unsigned bytes=1u<<size;
        memset(ram+128,0xff,16);reset(&c);c.x[3]=base+128;c.sp=0x9870;
        CHECK(run(&c,scalar(size,opc,0,3,31),ram,sizeof(ram),&code)==VF_BUDGET);
        CHECK(c.sp==0x9870 && c.x[3]==base+128);
        CHECK(read_value(ram+128,bytes)==(opc?(bytes==8?UINT64_MAX:(UINT64_C(1)<<(bytes*8))-1):0));
        if(opc){memset(ram+128,0x80,8);reset(&c);c.x[3]=base+128;
            uint64_t wanted=extended(read_value(ram+128,bytes),bytes,opc);
            CHECK(run(&c,scalar(size,opc,0,3,3),ram,sizeof(ram),&code)==VF_BUDGET);
            CHECK(c.x[3]==wanted);}
        for(unsigned el=0;el<2;el++)for(unsigned where=0;where<3;where++){
            uint64_t address=where==0?base-bytes:where==1?base+65536:base+128;
            size_t length=where==2?128+bytes-1:sizeof(ram);
            reset(&c);CHECK(vf_cpu_set_current_el(&c,el)==0);c.x[3]=address;c.x[4]=source;
            memcpy(before,ram,sizeof(ram));
            CHECK(run(&c,scalar(size,opc,0,3,4),ram,length,&code)==VF_DATA_ABORT);
            CHECK(c.pc==base && c.retired==0 && c.far==address && c.x[3]==address && c.x[4]==source);
            CHECK(!memcmp(before,ram,sizeof(ram)));
            CHECK(c.esr==((UINT64_C(0x24)+el)<<26 | (1u<<25) | (opc?0:64) | 7));
        }
        for(unsigned a=0;a<2;a++)for(unsigned offset=1;offset<bytes;offset++){
            reset(&c);c.sctlr=a?2:0;c.x[3]=base+128+offset;c.x[4]=source;memcpy(before,ram,sizeof(ram));
            CHECK(run(&c,scalar(size,opc,1,3,4),ram,sizeof(ram),&code)==VF_ALIGNMENT_FAULT);
            CHECK(c.pc==base && c.retired==0 && c.far==base+128+offset+bytes && c.x[4]==source);
            CHECK(c.x[3]==base+128+offset && !memcmp(before,ram,sizeof(ram)));
            CHECK(c.esr==((UINT64_C(0x25)<<26)|(1u<<25)|(opc?0:64)|0x21));
        }
        for(unsigned el=0;el<2;el++){
            reset(&c);CHECK(vf_cpu_set_current_el(&c,el)==0);c.sctlr=el?8:16;c.sp=base+129;c.x[4]=source;
            memcpy(before,ram,sizeof(ram));
            CHECK(run(&c,scalar(size,opc,1,31,4),ram,sizeof(ram),&code)==VF_SP_ALIGNMENT_FAULT);
            CHECK(c.esr==UINT64_C(0x9a000000) && c.pc==base && c.retired==0 && c.far==base+129);
            CHECK(c.sp==base+129 && c.x[4]==source && !memcmp(before,ram,sizeof(ram)));
        }
        for(unsigned regime=0;regime<5;regime++){
            reset(&c);c.x[3]=base+128;c.x[4]=source;
            if(regime==0)c.sctlr=1;
            if(regime==1)c.sctlr=1u<<25;
            if(regime==2)c.hcr_el2=8;
            if(regime==3)c.scr_el3=2;
            if(regime==4)CHECK(vf_cpu_set_current_el(&c,VF_EL2)==0);
            memcpy(before,ram,sizeof(ram));
            CHECK(run(&c,scalar(size,opc,0,3,4),ram,sizeof(ram),&code)==VF_SYSTEM_REGISTER_TRAP);
            CHECK(c.retired==0 && c.pc==base && c.x[4]==source && !memcmp(before,ram,sizeof(ram)));
        }
        /* Address addition wraps before the bounded host offset is formed. */
        reset(&c);c.pc=0;c.guest_ram_base=0;c.x[3]=UINT64_MAX-(bytes-1);c.x[4]=source;
        CHECK(run(&c,scalar(size,opc,1,3,4),ram,sizeof(ram),&code)==VF_BUDGET);
        CHECK(c.x[3]==UINT64_MAX-(bytes-1) && c.retired==1 && c.pc==4);
    }
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)for(unsigned vector=0;vector<2;vector++)
    if(vector || !valid(size,opc)){
        reset(&c);c.x[3]=base+128;c.x[4]=source;memcpy(before,ram,sizeof(ram));
        uint32_t w=scalar(size,opc,0,3,4)|(vector<<26);
        CHECK(run(&c,w,ram,sizeof(ram),&code)==VF_UNDEFINED_INSTRUCTION);
        CHECK(c.retired==0 && c.pc==base && c.esr==0 && c.instruction==w);
        CHECK(c.x[3]==base+128 && c.x[4]==source && !memcmp(before,ram,sizeof(ram)));
    }
    reset(&c);CHECK(vf_cpu_raise_exception(&c,VF_EXCEPTION_INSTRUCTION_ABORT,base,base,0,scalar(3,0,0,3,4))==0);
    CHECK(c.esr==((UINT64_C(0x21)<<26)|7));
    CHECK(munmap(code.bytes,code.capacity)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"scalar_cases\":%u}\n",checks,cases);
}
