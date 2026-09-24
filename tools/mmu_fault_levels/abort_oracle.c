typedef unsigned long long u64;
struct test_case {const char *name;unsigned granule,tsz,upper,start,level,kind,write,ips;};
#include "abort_cases.h"
static u64 tables[4][2048] __attribute__((aligned(16384)));
static u64 code[4][2048] __attribute__((aligned(16384)));
u64 abort_result[4];
extern void enter_el1(u64 va,u64 access);
extern char read_access[],write_access[];
static void print(const char *s) {
 register u64 x0 __asm__("x0")=4;register const char *x1 __asm__("x1")=s;
 __asm__ volatile("hlt #0xf000":"+r"(x0),"+r"(x1)::"memory");
}
static void hex(u64 value) {
 char s[18];for(unsigned i=0;i<16;i++)s[i]="0123456789abcdef"[(value>>(60-4*i))&15];s[16]=' ';s[17]=0;print(s);
}
static void field(const char *name,const char *key,u64 value) {print(name);print(key);hex(value);print("\n");}
void abort_main(void) {
 u64 state;__asm__ volatile("mrs %0,hcr_el2":"=r"(state));print("HCR ");hex(state);print("\n");
 __asm__ volatile("mrs %0,CurrentEL":"=r"(state));print("CURRENT_EL ");hex(state);print("\n");
 for(unsigned n=0;n<sizeof(cases)/sizeof(cases[0]);n++) {
  const struct test_case *c=&cases[n];
  __asm__ volatile("msr sctlr_el1,xzr\n isb\n tlbi vmalle1\n dsb sy\n isb":::"memory");
  for(unsigned i=0;i<4;i++)for(unsigned j=0;j<2048;j++){tables[i][j]=0;code[i][j]=0;}
  for(unsigned level=c->start;level<3;level++)tables[level][0]=(u64)tables[level+1]|3;
  /* Separate identity code/vector mapping: 4K L1 1GiB or16K L2 32MiB. */
  if(c->granule==4096){code[0][0]=(u64)code[1]|3;code[1][1]=0x40000401ULL;}
  else{code[1][0]=(u64)code[2]|3;code[2][32]=0x40000401ULL;}
  u64 descriptor=0x40000400ULL|(c->level==3?3:1);
  switch(c->kind){
   case 0:descriptor=0;break;
   case 2:descriptor=0x401;break;
   case 3:descriptor&=~0x400ULL;break;
   case 4:descriptor|=0x80;break;
   case 5:descriptor=(1ULL<<32)|0x400|(c->level==3?3:1);break;
   case 6:descriptor=(1ULL<<32)|3;break;
   case 11:descriptor|=1ULL<<53;break;
  }
#ifdef NEGATIVE_CONTROL
  if(n==0)descriptor=(u64)tables[c->level+1]|3;
#endif
  tables[c->level][0]=descriptor;
  u64 root=(u64)tables[c->start];if(c->kind==7)root=1ULL<<32;
  u64 lower=(u64)code[c->start];
  u64 tcr=c->tsz|((u64)c->tsz<<16);
  tcr|=c->granule==16384?((2ULL<<14)|(1ULL<<30)):(2ULL<<30);
  if(c->kind==8)tcr|=1ULL<<23;
  u64 va=~((1ULL<<(64-c->tsz))-1);va+=0x230;
  field(c->name,".ttbr0 ",lower);field(c->name,".ttbr1 ",root);field(c->name,".tcr ",tcr);
  field(c->name,".va ",va);field(c->name,".access ",c->write);
  for(unsigned i=0;i<4;i++){char address[]=".table0 ",value[]=".value0 ";address[6]+=i;value[6]+=i;
      field(c->name,address,(u64)tables[i]);field(c->name,value,tables[i][0]);}
  u64 one=1,mair=0xff;
  __asm__ volatile("dsb sy\n msr ttbr0_el1,%0\n msr ttbr1_el1,%1\n msr tcr_el1,%2\n"
                   "msr mair_el1,%3\n isb\n msr sctlr_el1,%4\n isb"
                   ::"r"(lower),"r"(root),"r"(tcr),"r"(mair),"r"(one):"memory");
  enter_el1(va,c->write);
  print(c->name);print(" ");
  for(unsigned i=0;i<4;i++)hex(abort_result[i]);
  hex(va);hex(c->write==2?va:(u64)(c->write?write_access:read_access));print("\n");
 }
 static u64 status[2]={0x20026,0};register u64 x0 __asm__("x0")=0x20;register u64 *x1 __asm__("x1")=status;
 __asm__ volatile("hlt #0xf000"::"r"(x0),"r"(x1):"memory");for(;;){}
}
