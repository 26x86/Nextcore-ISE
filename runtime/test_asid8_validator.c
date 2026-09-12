/* Authored C admission probe; production header implementation is linked unchanged. */
#include "memory_dynamic.h"
int test_asid8_dynamic(const vf_memory_controls_v2 *c) { return vf_dynamic_controls_valid(c); }

#include <string.h>
static int never_protect(void *p,size_t n,int x,void *owner){(void)p;(void)n;(void)x;*(int*)owner=1;return -1;}
int test_asid8_cpu_mismatch(const vf_memory_controls_v2 *c,uint8_t *bytes,vf_memory_callback_v2 cb,void *owner,unsigned kind) {
 vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.pc=0x10000;cpu.sp=0x10400;cpu.x[0]=0xdead;cpu.x[1]=0x11000;
 cpu.sctlr=c->sctlr;cpu.ttbr0=c->ttbr0;cpu.ttbr1=c->ttbr1;cpu.tcr=c->tcr;cpu.mair=c->mair;cpu.hcr_el2=c->hcr;cpu.scr_el3=c->scr;
 if(kind==0)cpu.ttbr0^=UINT64_C(1)<<48;else if(kind==1)cpu.ttbr1^=UINT64_C(1)<<48;else cpu.tcr^=UINT64_C(1)<<22;
 vf_cpu before=cpu;vf_code code={bytes,4096,0};vf_memory_run_result_v2 result={0};
 int called=0;int status=vf_run_memory_provider_v2(&cpu,&code,2,never_protect,&called,c,cb,owner,&result);
 cpu.status=before.status;
 return status==VF_DATA_FAULT && result.base.provider_status==VF_PROVIDER_INVALID_REQUEST && !called && !memcmp(&cpu,&before,sizeof(cpu));
}
