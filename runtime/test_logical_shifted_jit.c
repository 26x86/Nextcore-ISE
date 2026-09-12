/* SPDX-License-Identifier: BSD-4-Clause; independently authored bit-origin oracle. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t word(unsigned w,unsigned op,unsigned shift,unsigned amount,unsigned rn,unsigned rm,unsigned rd){return 0x0a000000u|(w==64?1u<<31:0)|(op>>1)<<29|(op&1)<<21|shift<<22|rm<<16|amount<<10|rn<<5|rd;}
static uint64_t expected(unsigned w,unsigned op,unsigned shift,unsigned amount,uint64_t a,uint64_t b){
 uint64_t out=0;
 for(unsigned bit=0;bit<w;bit++){
  unsigned v=0;
  if(shift==0){if(bit>=amount)v=(b>>(bit-amount))&1;}
  else if(shift==1){if(bit+amount<w)v=(b>>(bit+amount))&1;}
  else if(shift==2)v=(b>>(bit+amount<w?bit+amount:w-1))&1;
  else v=(b>>((bit+amount)%w))&1;
  v^=op&1;unsigned av=(a>>bit)&1;unsigned result=(op>>1)==1?av|v:(op>>1)==2?av^v:av&v;
  out|=(uint64_t)result<<bit;
 }return out;
}
int main(void){
 vf_code code={mmap(0,4096,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);
 const unsigned aliases[][3]={{0,1,2},{0,1,0},{0,1,1},{0,0,0},{31,1,2},{0,31,2},{31,31,31}};
 const uint64_t inputs[][2]={{0x91a2b3c4d5e6f780ULL,0x8123456789abcdefULL},{0,0},{~0ULL,~0ULL}};
 for(unsigned w=32;w<=64;w+=32)for(unsigned op=0;op<8;op++)for(unsigned shift=0;shift<4;shift++)
 for(unsigned amount=0;amount<w;amount++)for(unsigned alias=0;alias<7;alias++)for(unsigned sample=0;sample<3;sample++){
  unsigned rn=aliases[alias][0],rm=aliases[alias][1],rd=aliases[alias][2];vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);
  cpu.x[0]=inputs[sample][0];cpu.x[1]=inputs[sample][1];cpu.x[2]=0xface;cpu.x[31]=0xfeed;cpu.sp=0x12345678;cpu.pstate=0xf00003c5;
  uint64_t regs[32];memcpy(regs,cpu.x,sizeof(regs));uint64_t value=expected(w,op,shift,amount,rn==31?0:cpu.x[rn],rm==31?0:cpu.x[rm]);
  if(rd!=31)regs[rd]=value;uint32_t insn=word(w,op,shift,amount,rn,rm,rd);uint8_t ram[8]={0},before[8]={0};
  CHECK(vf_run(&cpu,(uint8_t*)&insn,4,ram,8,&code,1,perms,0)==VF_BUDGET);
  CHECK(!memcmp(regs,cpu.x,sizeof(regs)) && cpu.sp==0x12345678 && !memcmp(ram,before,8));
  uint64_t flags=(op>>1)==3?((value>>(w-1))<<31)|(value==0?1u<<30:0):0xf0000000;
  CHECK(cpu.pstate==(flags|0x3c5) && cpu.pc==4 && cpu.retired==1);cases++;
 }
 for(unsigned op=0;op<8;op++)for(unsigned shift=0;shift<4;shift++)for(unsigned amount=32;amount<64;amount++){
  vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=0xabcdef;cpu.sp=0x800;cpu.pstate=0xf00003c5;uint8_t ram[8]={0};uint32_t insn=word(32,op,shift,amount,0,0,0);
  CHECK(vf_run(&cpu,(uint8_t*)&insn,4,ram,8,&code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
  CHECK(cpu.x[0]==0xabcdef && cpu.sp==0x800 && cpu.pstate==0xf00003c5 && cpu.pc==0 && cpu.retired==0);
 }
 /* ORACLE_INSERT */
 CHECK(munmap(code.bytes,4096)==0);printf("{\"passed\":true,\"cases\":%u,\"assertions\":%u}\n",cases,checks);
}
