/* SPDX-License-Identifier: BSD-4-Clause; independently authored ISA tests. */
#include "boot_jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t pair(unsigned bytes,unsigned read,unsigned mode,int displacement,unsigned rn,unsigned rt,unsigned rt2){
    return UINT32_C(0x28000000)|(bytes==8?UINT32_C(0x80000000):0)|(mode<<23)|(read<<22)|
        (((unsigned)displacement&127)<<15)|(rt2<<10)|(rn<<5)|rt;
}
static uint64_t read_value(const uint8_t*p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static int run(vf_cpu*c,uint32_t w,uint8_t*ram,size_t size,vf_code*code){
    return vf_run(c,(const uint8_t*)&w,4,ram,size,code,1,perms,0);
}
static const uint64_t base=UINT64_C(0x40000000);
static void reset(vf_cpu*c){vf_cpu_reset(c,VF_EL1);c->pc=base;c->guest_ram_base=base;c->pstate|=UINT64_C(0xb00003c0);}
int main(void){
    uint8_t ram[4096],before[4096];vf_cpu c;
    vf_code code={mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};
    CHECK(code.bytes!=MAP_FAILED);
    for(unsigned bytes=4;bytes<=8;bytes+=4)for(unsigned read=0;read<2;read++)
    for(unsigned mode=1;mode<=3;mode++)for(int displacement=-64;displacement<64;displacement++)
    for(unsigned use_sp=0;use_sp<2;use_sp++){
        for(unsigned i=0;i<sizeof(ram);i++)ram[i]=(uint8_t)(i*37u+19u);
        reset(&c);c.sp=base+1024;c.x[3]=base+1024;
        c.x[4]=UINT64_C(0xfedcba9876543210);c.x[5]=UINT64_C(0x89abcdef12345678);
        uint64_t expected[2]={c.x[4],c.x[5]};
        unsigned address=1024+(mode==1?0:displacement*(int)bytes);
        if(read){expected[0]=read_value(ram+address,bytes);expected[1]=read_value(ram+address+bytes,bytes);}
        uint32_t w=pair(bytes,read,mode,displacement,use_sp?31:3,4,5);
        CHECK(run(&c,w,ram,sizeof(ram),&code)==VF_BUDGET);
        uint64_t updated=base+1024+(mode==2?0:(int64_t)displacement*bytes);
        CHECK(c.pc==base+4 && c.retired==1 && c.compiled_blocks==1 && c.pstate==UINT64_C(0xb00003c5));
        CHECK(c.sp==(use_sp?updated:base+1024) && c.x[3]==(use_sp?base+1024:updated));
        if(read){CHECK(c.x[4]==expected[0] && c.x[5]==expected[1]);}
        else {uint64_t mask=bytes==8?UINT64_MAX:UINT32_MAX;
            CHECK(read_value(ram+address,bytes)==(expected[0]&mask) && read_value(ram+address+bytes,bytes)==(expected[1]&mask));
            CHECK(c.x[4]==expected[0] && c.x[5]==expected[1]);}
        cases++;
    }
    /* Equal STP sources are defined; zero sources and discarded loads are
       independent of SP. Offset loads may overwrite their base safely. */
    memset(ram,0xff,sizeof(ram));reset(&c);c.x[3]=base+128;c.sp=0x9870;c.x[4]=0x42;
    CHECK(run(&c,pair(8,0,2,0,3,31,31),ram,sizeof(ram),&code)==VF_BUDGET);
    CHECK(read_value(ram+128,8)==0 && read_value(ram+136,8)==0 && c.sp==0x9870);
    reset(&c);c.x[3]=base+128;c.x[4]=0x42;
    CHECK(run(&c,pair(8,0,2,0,3,4,4),ram,sizeof(ram),&code)==VF_BUDGET);
    CHECK(read_value(ram+128,8)==0x42 && read_value(ram+136,8)==0x42);
    reset(&c);c.x[3]=base+128;c.sp=0x9870;
    CHECK(run(&c,pair(8,1,2,0,3,3,31),ram,sizeof(ram),&code)==VF_BUDGET);
    CHECK(c.x[3]==0x42 && c.sp==0x9870);
    const uint32_t invalid[]={0x68000440,0xe8000440,0x2c000440,
        0x28000440,0xa9401084,0xa9801484,0xa8c01484};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        reset(&c);c.x[4]=base+128;memcpy(before,ram,sizeof(ram));
        CHECK(run(&c,invalid[i],ram,sizeof(ram),&code)==VF_UNDEFINED_INSTRUCTION);
        CHECK(c.pc==base && c.retired==0 && c.x[4]==base+128 && !memcmp(before,ram,sizeof(ram)));
        CHECK(c.esr==(1u<<25) && c.esr_el[VF_EL1]==(1u<<25) && c.instruction==invalid[i]);
    }
    /* A saved pair-shaped instruction is not a data-access indication for
       unrelated exception kinds. Unknown ISS remains RES0 in both backends. */
    reset(&c);
    CHECK(vf_cpu_raise_exception(&c,VF_EXCEPTION_INSTRUCTION_ABORT,base,base,0,
              pair(8,0,2,0,3,4,5))==0);
    CHECK(c.esr==((UINT64_C(0x21)<<26)|7));
    /* Both elements are preflighted: a second-element abort cannot commit
       the first store or a pre/post writeback. */
    for(unsigned read=0;read<2;read++)for(unsigned mode=1;mode<=3;mode++)
    for(unsigned where=0;where<3;where++){
        uint64_t address=where==0?base-8:where==1?base+4096:base+4088;
        reset(&c);c.x[3]=address;c.x[4]=0x1111;c.x[5]=0x2222;memcpy(before,ram,sizeof(ram));
        CHECK(run(&c,pair(8,read,mode,0,3,4,5),ram,sizeof(ram),&code)==VF_DATA_ABORT);
        CHECK(c.far==(where==2?address+8:address) && c.pc==base && c.retired==0);
        CHECK(c.x[3]==address && c.x[4]==0x1111 && c.x[5]==0x2222 && !memcmp(before,ram,sizeof(ram)));
        CHECK(c.esr==((UINT64_C(0x25)<<26)|(1u<<25)|(read?0:64)|7));
    }
    for(unsigned el=0;el<2;el++)for(unsigned sa=0;sa<2;sa++)for(unsigned a=0;a<2;a++){
        reset(&c);CHECK(vf_cpu_set_current_el(&c,el)==0);c.sp=base+129;
        c.sctlr=(sa?(el==0?16:8):0)|(a?2:0);memcpy(before,ram,sizeof(ram));
        int status=run(&c,pair(8,0,2,0,31,4,5),ram,sizeof(ram),&code);
        CHECK(status==(sa?VF_SP_ALIGNMENT_FAULT:VF_ALIGNMENT_FAULT));
        CHECK(c.retired==0 && c.sp==base+129 && !memcmp(before,ram,sizeof(ram)));
        CHECK(sa?c.esr==((UINT64_C(0x26)<<26)|(1u<<25)):(c.esr&63)==0x21);
    }
    /* Device-nGnRnE alignment applies to every element width and mode,
       regardless of SCTLR.A. Faults cannot commit a load or writeback. */
    for(unsigned bytes=4;bytes<=8;bytes+=4)for(unsigned read=0;read<2;read++)
    for(unsigned mode=1;mode<=3;mode++)for(unsigned a=0;a<2;a++)
    for(unsigned offset=1;offset<bytes;offset++){
        reset(&c);c.sctlr=a?2:0;c.x[3]=base+128+offset;c.x[4]=0x1111;c.x[5]=0x2222;
        memcpy(before,ram,sizeof(ram));
        CHECK(run(&c,pair(bytes,read,mode,1,3,4,5),ram,sizeof(ram),&code)==VF_ALIGNMENT_FAULT);
        CHECK(c.pc==base && c.retired==0 && c.x[3]==base+128+offset && c.x[4]==0x1111 && c.x[5]==0x2222);
        CHECK(!memcmp(before,ram,sizeof(ram)) && c.far==base+128+offset+(mode==1?0:bytes));
        CHECK(c.esr==((UINT64_C(0x25)<<26)|(1u<<25)|(read?0:64)|0x21));
    }
    for(unsigned regime=0;regime<5;regime++){
        reset(&c);c.x[3]=base+128;
        if(regime==0)c.sctlr=1;
        if(regime==1)c.sctlr=1u<<25;
        if(regime==2)c.hcr_el2=8;
        if(regime==3)c.scr_el3=2;
        if(regime==4)CHECK(vf_cpu_set_current_el(&c,VF_EL2)==0);
        memcpy(before,ram,sizeof(ram));
        CHECK(run(&c,pair(8,0,2,0,3,4,5),ram,sizeof(ram),&code)==VF_SYSTEM_REGISTER_TRAP);
        CHECK(!memcmp(before,ram,sizeof(ram)) && c.retired==0 && c.pc==base);
    }
    /* A native pair uses modulo-64 address calculation, then range checks. */
    reset(&c);c.guest_ram_base=0;c.pc=0;c.x[3]=UINT64_MAX-7;c.x[4]=0x55;c.x[5]=0x66;
    CHECK(run(&c,pair(8,0,3,1,3,4,5),ram,sizeof(ram),&code)==VF_BUDGET);
    CHECK(c.x[3]==0 && read_value(ram,8)==0x55 && read_value(ram+8,8)==0x66);
    CHECK(munmap(code.bytes,code.capacity)==0);
    printf("{\"passed\":true,\"native_jit_executed\":true,\"assertions\":%u,\"pair_cases\":%u}\n",checks,cases);
}
