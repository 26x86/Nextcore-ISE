/* SPDX-License-Identifier: BSD-4-Clause
 * Independently authored architectural cases; no vendor instruction sequences.
 */
#include "boot_jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void *p,size_t n,int executable,void *opaque) {
    (void)opaque;return mprotect(p,n,PROT_READ|(executable?PROT_EXEC:PROT_WRITE));
}
static int condition(unsigned code,unsigned nzcv) {
    int n=(nzcv>>3)&1,z=(nzcv>>2)&1,c=(nzcv>>1)&1,v=nzcv&1;
    switch(code) {
    case 0:return z; case 1:return !z;
    case 2:return c; case 3:return !c;
    case 4:return n; case 5:return !n;
    case 6:return v; case 7:return !v;
    case 8:return c&&!z; case 9:return !c||z;
    case 10:return n==v; case 11:return n!=v;
    case 12:return !z&&n==v; case 13:return z||n!=v;
    default:return 1; /* AL and NV both hold for A64 B.cond. */
    }
}

int main(void) {
    uint8_t ram[0x4000]={0};
    vf_code buffer={mmap(0,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),16384,0};
    CHECK(buffer.bytes!=MAP_FAILED);
    vf_cpu cpu;
    const struct {unsigned width,subtract;uint64_t a,b,result;unsigned flags;} reg_cases[]={
        {64,1,0,1,UINT64_MAX,8},{64,1,5,5,0,6},
        {64,1,UINT64_C(0x8000000000000000),1,UINT64_C(0x7fffffffffffffff),3},
        {64,0,UINT64_MAX,1,0,6},{32,0,0x7fffffff,1,0x80000000,9},
        {32,1,UINT64_C(0x1234567800000000),1,UINT64_C(0xffffffff),8},
    };
    for(unsigned i=0;i<sizeof(reg_cases)/sizeof(reg_cases[0]);i++) {
        uint32_t program[]={0x2b000000u|((reg_cases[i].width==64?1u:0u)<<31)|
            (reg_cases[i].subtract<<30)|(1u<<16)|2,0xd4400000};
        vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=reg_cases[i].a;cpu.x[1]=reg_cases[i].b;
        CHECK(vf_run(&cpu,(uint8_t*)program,sizeof(program),ram,sizeof(ram),&buffer,8,perms,0)==VF_HALT);
        CHECK(cpu.x[2]==reg_cases[i].result && (cpu.pstate>>28&15)==reg_cases[i].flags);
    }
    uint32_t cmp_register[]={0xeb0003ff,0xd4400000}; /* CMP XZR, X0 */
    vf_cpu_reset(&cpu,VF_EL1);cpu.sp=UINT64_MAX;cpu.x[0]=0;
    CHECK(vf_run(&cpu,(uint8_t*)cmp_register,sizeof(cmp_register),ram,sizeof(ram),&buffer,8,perms,0)==VF_HALT);
    CHECK(cpu.sp==UINT64_MAX && cpu.x[31]==0 && (cpu.pstate>>28&15)==6);
    const struct {uint32_t word;uint64_t input,output;unsigned nzcv;} cases[]={
        {0xb1000401,0,1,0},
        {0xb1000401,UINT64_MAX,0,6},
        {0xb1000401,UINT64_C(0x7fffffffffffffff),UINT64_C(0x8000000000000000),9},
        {0xb1000401,UINT64_C(0x8000000000000000),UINT64_C(0x8000000000000001),8},
        {0xf1000401,0,UINT64_MAX,8},
        {0xf1000401,1,0,6},
        {0xf1000401,UINT64_C(0x8000000000000000),UINT64_C(0x7fffffffffffffff),3},
        {0xf1000401,UINT64_C(0x7fffffffffffffff),UINT64_C(0x7ffffffffffffffe),2},
        {0x31000401,UINT64_C(0xabcdef00ffffffff),0,6},
        {0x31000401,UINT64_C(0xabcdef007fffffff),UINT64_C(0x80000000),9},
        {0x71000401,UINT64_C(0xabcdef0080000000),UINT64_C(0x7fffffff),3},
        {0x71000401,UINT64_C(0xabcdef0000000000),UINT64_C(0xffffffff),8},
        {0xb1400401,UINT64_MAX-4095,0,6},
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        uint32_t program[]={cases[i].word,0xd4400000};
        vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=cases[i].input;
        cpu.pstate|=UINT64_C(0xabcdef00000003c0);
        uint64_t other=cpu.pstate&~UINT64_C(0xf0000000);
        CHECK(vf_run(&cpu,(uint8_t*)program,sizeof(program),ram,sizeof(ram),&buffer,8,perms,0)==VF_HALT);
        CHECK(cpu.x[1]==cases[i].output && (cpu.pstate>>28&15)==cases[i].nzcv);
        CHECK((cpu.pstate&~UINT64_C(0xf0000000))==other && cpu.retired==2);
    }

    /* All condition encodings against every NZCV combination. A native block
     * boundary and the next block's flag-clobbering helpers intervene. */
    for(unsigned nzcv=0;nzcv<16;nzcv++)for(unsigned cond=0;cond<16;cond++) {
        uint32_t program[]={0x54000060|cond,0xd2800162,0xd4400000,0xd28002c2,0xd4400000};
        vf_cpu_reset(&cpu,VF_EL1);cpu.pstate|=(uint64_t)nzcv<<28;
        CHECK(vf_run(&cpu,(uint8_t*)program,sizeof(program),ram,sizeof(ram),&buffer,8,perms,0)==VF_HALT);
        CHECK(cpu.x[2]==(condition(cond,nzcv)?22u:11u));
        CHECK((cpu.pstate>>28&15)==nzcv && cpu.retired==3 && cpu.compiled_blocks==2);
    }

    /* CMP aliases discard Rd31 while Rn31 still reads SP. */
    const uint32_t cmp_sp[]={0xf14007ff,0xd4400000};
    vf_cpu_reset(&cpu,VF_EL1);cpu.sp=4096;cpu.x[31]=0;
    CHECK(vf_run(&cpu,(uint8_t*)cmp_sp,sizeof(cmp_sp),ram,sizeof(ram),&buffer,8,perms,0)==VF_HALT);
    CHECK(cpu.sp==4096 && cpu.x[31]==0 && (cpu.pstate>>28&15)==6);
    const uint32_t cmn_sp[]={0xb10007ff,0xd4400000};
    vf_cpu_reset(&cpu,VF_EL1);cpu.sp=UINT64_MAX;
    CHECK(vf_run(&cpu,(uint8_t*)cmn_sp,sizeof(cmn_sp),ram,sizeof(ram),&buffer,8,perms,0)==VF_HALT);
    CHECK(cpu.sp==UINT64_MAX && (cpu.pstate>>28&15)==6);

    /* Repeated negative-displacement branches observe a freshly committed
     * SUBS result, then terminate using its zero flag. */
    const uint32_t loop[]={0xf1000400,0x54ffffe1,0xd4400000};
    vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=3;
    CHECK(vf_run(&cpu,(uint8_t*)loop,sizeof(loop),ram,sizeof(ram),&buffer,16,perms,0)==VF_HALT);
    CHECK(cpu.x[0]==0 && cpu.retired==7 && (cpu.pstate>>28&15)==6);

    /* Non-S arithmetic may clobber host flags, never guest NZCV. */
    const uint32_t preserved[]={0xf100001f,0x8b1f03e4,0x54000040,0xd2800023,0xd4400000};
    vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=0;
    CHECK(vf_run(&cpu,(uint8_t*)preserved,sizeof(preserved),ram,sizeof(ram),&buffer,8,perms,0)==VF_HALT);
    CHECK(cpu.x[3]==0 && cpu.x[4]==0 && (cpu.pstate>>28&15)==6);

    /* Bit 4 is reserved for the separately unsupported FEAT_HBC encoding. */
    const uint32_t hbc[]={0x54000010};
    vf_cpu_reset(&cpu,VF_EL1);
    CHECK(vf_run(&cpu,(uint8_t*)hbc,sizeof(hbc),ram,sizeof(ram),&buffer,8,perms,0)==VF_UNDEFINED_INSTRUCTION);
    CHECK(cpu.retired==0 && cpu.pc==0);

    /* Explicit-register diagnostics do not dereference arbitrary x2/x3 or
     * silently replace x0 with the legacy boot_args pointer. */
    const uint64_t base=UINT64_C(0x800000000),entry=base+0x100,args=base+0x1000;
    const uint32_t prefix[]={0xf1001c1f,0x54000041,0xd4400000,0xd53be005};
    memcpy(ram+0x100,prefix,sizeof(prefix));
    const uint64_t initial[4]={0,args,UINT64_C(0xf123456789abcdef),37};
    vf_boot_result result;
    CHECK(vf_boot_run_with_registers(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
        buffer.bytes,buffer.capacity,8,perms,0,initial,0,&result)==VF_SYSTEM_REGISTER_TRAP);
    CHECK(result.retired==2 && result.pc==entry+12 && result.fault_instruction==prefix[3]);
    CHECK(result.x0==0 && result.x1==args && result.x2==initial[2] && result.x3==37);
    CHECK(memcmp(ram+0x100,prefix,sizeof(prefix))==0 && result.compiled_blocks==2);
    CHECK(vf_boot_run_with_registers(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
        buffer.bytes,buffer.capacity,1,perms,0,initial,0,&result)==VF_BUDGET);
    CHECK(result.retired==1 && result.pc==entry+4 && result.x0==0);
    CHECK(vf_boot_run_with_registers(ram,sizeof(ram),base,entry,args,base+sizeof(ram),
        buffer.bytes,buffer.capacity,8,perms,0,0,0,&result)==VF_DATA_FAULT);
    CHECK(result.retired==0 && result.compiled_blocks==0);
    CHECK(munmap(buffer.bytes,buffer.capacity)==0);
    printf("{\"passed\":true,\"assertions\":%u,\"native_jit_executed\":true,\"nzcv_and_conditions\":true,\"explicit_initial_registers\":true,\"wx_enforced\":true}\n",checks);
    return 0;
}
