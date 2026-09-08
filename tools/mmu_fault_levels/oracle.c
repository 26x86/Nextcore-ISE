/* Authored architectural test controller, not a software MMU implementation. */
typedef unsigned long long u64;
struct test_case { const char *name; unsigned granule, tsz, upper, start, level, kind, write, ips; };
#include "cases.h"
static u64 tables[4][2048] __attribute__((aligned(16384)));

static void print(const char *s) {
    register u64 x0 __asm__("x0") = 4;
    register const char *x1 __asm__("x1") = s;
    __asm__ volatile("hlt #0xf000" : "+r"(x0), "+r"(x1) :: "memory");
}
static void hex(u64 value) {
    char buf[18];
    for (unsigned i=0;i<16;i++) buf[i]="0123456789abcdef"[(value >> (60-4*i))&15];
    buf[16]='\n';buf[17]=0;print(buf);
}
static void field(const char *name,const char *key,u64 value) {print(name);print(key);hex(value);}
void oracle_main(void) {
    u64 features;
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1":"=r"(features));
    print("FEATURES ");hex(features);
    __asm__ volatile("mrs %0, hcr_el2":"=r"(features));
    print("HCR ");hex(features);
    __asm__ volatile("mrs %0, CurrentEL":"=r"(features));
    print("CURRENT_EL ");hex(features);
    for(unsigned n=0;n<sizeof(cases)/sizeof(cases[0]);n++) {
        const struct test_case *c=&cases[n];
        for(unsigned i=0;i<4;i++) for(unsigned j=0;j<2048;j++) tables[i][j]=0;
        for(unsigned level=c->start;level<3;level++) tables[level][0]=(u64)tables[level+1]|3;
        u64 descriptor=0x40000000ULL|0x400|(c->level==3?3:1);
        switch(c->kind) {
        case 0:descriptor=0;break; /* invalid */
        case 1:descriptor=2;break; /* invalid encoding */
        case 2:descriptor=0x401;break; /* block encoding at reserved level */
        case 3:descriptor&=~0x400ULL;break; /* AF=0 */
        case 4:descriptor|=0x80;break; /* AP=read-only, privileged access */
        case 5:descriptor=(1ULL<<32)|0x400|(c->level==3?3:1);break;
        case 6:descriptor=(1ULL<<32)|3;break;
        }
#ifdef NEGATIVE_CONTROL
        if(n==0) descriptor=(u64)tables[c->level+1]|3;
#endif
        tables[c->level][0]=descriptor;
        u64 root=(u64)tables[c->start];
        if(c->kind==7) root=1ULL<<32;
        u64 tcr=c->tsz|((u64)c->tsz<<16)|((u64)c->ips<<32);
        tcr|=c->granule==16384?((2ULL<<14)|(1ULL<<30)):(2ULL<<30);
        if(c->kind==8) tcr|=1ULL<<(c->upper?23:7);
        u64 va=c->upper?~((1ULL<<(64-c->tsz))-1):0;
        va+=0x234;
        if(c->kind==10) va=1ULL<<(64-c->tsz); /* neither canonical region */
        field(c->name,".ttbr0 ",root);field(c->name,".ttbr1 ",root);
        field(c->name,".tcr ",tcr);field(c->name,".va ",va);field(c->name,".access ",c->write);
        for(unsigned i=0;i<4;i++) {
            char address[]=".table0 ",value[]=".value0 ";address[6]+=i;value[6]+=i;
            field(c->name,address,(u64)tables[i]);field(c->name,value,tables[i][0]);
        }
        u64 one=1, mair=0xff, par;
        __asm__ volatile("msr sctlr_el1,xzr\n isb\n tlbi vmalle1\n dsb sy\n isb\n"
                         "msr ttbr0_el1,%0\n msr ttbr1_el1,%0\n msr tcr_el1,%1\n"
                         "msr mair_el1,%2\n dsb sy\n isb\n msr sctlr_el1,%3\n isb"
                         ::"r"(root),"r"(tcr),"r"(mair),"r"(one):"memory");
        if(c->write) __asm__ volatile("at s1e1w,%0\n isb"::"r"(va):"memory");
        else __asm__ volatile("at s1e1r,%0\n isb"::"r"(va):"memory");
        __asm__ volatile("mrs %0,par_el1":"=r"(par));
        print(c->name);print(" ");hex(par);
    }
    static u64 status[2]={0x20026,0};
    register u64 x0 __asm__("x0")=0x20;
    register u64 *x1 __asm__("x1")=status;
    __asm__ volatile("hlt #0xf000"::"r"(x0),"r"(x1):"memory");
    for(;;) {}
}
