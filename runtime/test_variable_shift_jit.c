/* SPDX-License-Identifier: BSD-4-Clause; independently authored bit-origin oracle. */
#include "jit.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int perms(void*p,size_t n,int x,void*o){(void)o;return mprotect(p,n,PROT_READ|(x?PROT_EXEC:PROT_WRITE));}
static uint32_t word(unsigned w,unsigned op,unsigned rn,unsigned rm,unsigned rd){return 0x1ac02000u|(w==64?1u<<31:0)|op<<10|rm<<16|rn<<5|rd;}
/* Independent per-bit origin model; avoids host shift-by-width undefined behavior. */
static uint64_t expected(unsigned w,unsigned op,uint64_t a,uint64_t b){
 unsigned amount=(unsigned)(b%w);uint64_t out=0;
 for(unsigned bit=0;bit<w;bit++){
  unsigned v=0;
  if(op==0){if(bit>=amount)v=(a>>(bit-amount))&1;}
  else if(op==1){if(bit+amount<w)v=(a>>(bit+amount))&1;}
  else if(op==2)v=(a>>(bit+amount<w?bit+amount:w-1))&1;
  else v=(a>>((bit+amount)%w))&1;
  out|=(uint64_t)v<<bit;
 }return out;
}
int main(void){
 vf_code code={mmap(0,4096,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};CHECK(code.bytes!=MAP_FAILED);
 const unsigned aliases[][3]={{0,1,2},{0,1,0},{0,1,1},{0,0,0},{31,1,2},{0,31,2},{31,31,31}};
 const uint64_t inputs[][2]={{0x91a2b3c4d5e6f780ULL,0x8123456789abcdefULL},{0,0},{~0ULL,~0ULL}};
 for(unsigned w=32;w<=64;w+=32)for(unsigned op=0;op<4;op++)
 for(unsigned amount=0;amount<2*w+2;amount++)for(unsigned alias=0;alias<7;alias++)for(unsigned sample=0;sample<3;sample++){
  unsigned rn=aliases[alias][0],rm=aliases[alias][1],rd=aliases[alias][2];vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);
  cpu.x[0]=inputs[sample][0];cpu.x[1]=(inputs[sample][1]&~255ULL)|amount;cpu.x[2]=0xface;cpu.x[31]=0xfeed;cpu.sp=0x12345678;cpu.pstate=0xf00003c5;
  uint64_t regs[32];memcpy(regs,cpu.x,sizeof(regs));uint64_t value=expected(w,op,rn==31?0:cpu.x[rn],rm==31?0:cpu.x[rm]);
  if(rd!=31)regs[rd]=value;uint32_t insn=word(w,op,rn,rm,rd);uint8_t ram[8]={0},before[8]={0};
  CHECK(vf_run(&cpu,(uint8_t*)&insn,4,ram,8,&code,1,perms,0)==VF_BUDGET);
  CHECK(!memcmp(regs,cpu.x,sizeof(regs)) && cpu.sp==0x12345678 && !memcmp(ram,before,8));
  CHECK(cpu.pstate==0xf00003c5 && cpu.pc==4 && cpu.retired==1);cases++;
 }
 for(unsigned wide=0;wide<2;wide++)for(unsigned extra=0;extra<3;extra++){
  vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=0xabcdef;cpu.sp=0x800;cpu.pstate=0xf00003c5;uint8_t ram[8]={0};uint32_t insn=word(wide?64:32,0,0,0,0)|(extra==0?1u<<29:extra==1?1u<<30:1u<<15);
  CHECK(vf_run(&cpu,(uint8_t*)&insn,4,ram,8,&code,1,perms,0)==VF_UNDEFINED_INSTRUCTION);
  CHECK(cpu.x[0]==0xabcdef && cpu.sp==0x800 && cpu.pstate==0xf00003c5 && cpu.pc==0 && cpu.retired==0);
 }
 /* ORACLE_INSERT */
 CHECK(munmap(code.bytes,4096)==0);printf("{\"passed\":true,\"cases\":%u,\"assertions\":%u}\n",cases,checks);
}
