/* SPDX-License-Identifier: BSD-4-Clause; authored native cache acceptance. */
#include "memory_boot.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define RAM_SIZE 16384
#define MAX_REQUESTS 4096
static const uint64_t base=UINT64_C(0x40000000);
typedef struct {vf_memory_request_v1 request;vf_memory_reply_v1 reply;int32_t returned;} exchange;
typedef struct {
    uint8_t ram[RAM_SIZE];exchange requests[MAX_REQUESTS];size_t count,fetches;
    vf_cpu *cpu;unsigned fetch_failure,malformed,change_el,data_fault;
} memory;
typedef struct {unsigned calls,rw,rx,fail_call;void *base;size_t capacity;} protection;
static int protect(void *p,size_t n,int executable,void *opaque) {
    protection *state=opaque;CHECK(p==state->base && n==state->capacity);
    state->calls++;if(executable)state->rx++;else state->rw++;
    if(state->calls==state->fail_call)return -1;
    /* Actual host W^X: executable views are never writable, and vice versa. */
    return mprotect(p,n,PROT_READ|(executable?PROT_EXEC:PROT_WRITE));
}
static uint64_t read_le(const uint8_t *p,unsigned width) {
    uint64_t value=0;for(unsigned i=0;i<width;i++)value|=(uint64_t)p[i]<<(i*8);return value;
}
static void write_le(uint8_t *p,uint64_t value,unsigned width) {
    for(unsigned i=0;i<width;i++)p[i]=(uint8_t)(value>>(i*8));
}
static int32_t callback(void *opaque,const vf_memory_request_v1 *request,vf_memory_reply_v1 *reply) {
    memory *m=opaque;CHECK(m->count<MAX_REQUESTS);
    exchange *event=&m->requests[m->count++];event->request=*request;
    *reply=(vf_memory_reply_v1){0};reply->abi_version=1;reply->struct_size=sizeof(*reply);
    CHECK(request->abi_version==1 && request->struct_size==sizeof(*request));
    CHECK(request->width && request->width<=8 && request->count>=1 && request->count<=2);
    if(request->operation==VF_MEMORY_FETCH) {
        m->fetches++;
        if(m->change_el && m->fetches==3) {
            /* Test-only perturbation isolates specialization, not an SPTM service. */
            CHECK(vf_cpu_set_current_el(m->cpu,VF_EL0)==0);
        }
        if(m->fetch_failure && m->fetches==5) {
            event->returned=-1;event->reply=*reply;return -1;
        }
        if(m->malformed && m->fetches==5)reply->epoch=1;
    }
    uint64_t offset=request->address-base,span=(uint64_t)request->width*request->count;
    if(offset>RAM_SIZE || span>RAM_SIZE-offset ||
       (m->data_fault && request->operation!=VF_MEMORY_FETCH)) {
        reply->result=VF_MEMORY_UNSUPPORTED;reply->address=request->address;
    } else if(request->operation==VF_MEMORY_STORE) {
        write_le(m->ram+offset,request->value0,request->width);
        if(request->count==2)write_le(m->ram+offset+request->width,request->value1,request->width);
    } else {
        reply->value0=read_le(m->ram+offset,request->width);
        if(request->count==2)reply->value1=read_le(m->ram+offset+request->width,request->width);
    }
    event->reply=*reply;return 0;
}
static void word(memory *m,unsigned offset,uint32_t instruction) {write_le(m->ram+offset,instruction,4);}
static void dump(const char *directory,const char *name,const vf_cpu *cpu,
                 const vf_memory_run_result_v1 *result,const memory *m,int status) {
    char path[4096];CHECK(snprintf(path,sizeof(path),"%s/%s.bin",directory,name)>0);
    FILE *f=fopen(path,"wb");CHECK(f);
    CHECK(fwrite(&status,sizeof(status),1,f)==1);
    CHECK(fwrite(cpu,sizeof(*cpu),1,f)==1);
    CHECK(fwrite(result,sizeof(*result),1,f)==1);
    CHECK(fwrite(m->ram,sizeof(m->ram),1,f)==1);
    CHECK(fwrite(&m->count,sizeof(m->count),1,f)==1);
    CHECK(fwrite(m->requests,sizeof(exchange),m->count,f)==m->count);
    CHECK(fclose(f)==0);
}
static void run_case(const char *directory,const char *name,unsigned kind,size_t capacity,unsigned fail_call) {
    memory *m=calloc(1,sizeof(*m));CHECK(m);vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);
    cpu.pc=base;cpu.guest_ram_base=base;cpu.pstate=UINT64_C(0xa00003c5);
    cpu.x[1]=base+8192;cpu.x[2]=17;cpu.tpidr_el1=UINT64_C(0x123456789abcdef0);m->cpu=&cpu;
    uint64_t budget=256;
    /* ADD X2,X2,#1; STR X2,[X1]; B back. */
    word(m,0,0x91000442);word(m,4,0xf9000022);word(m,8,0x17fffffe);
    switch(kind) {
    case 1:word(m,0,0x14000000);m->fetch_failure=1;break;
    case 2:word(m,0,0x14000000);m->malformed=1;break;
    case 3: /* Guest store changes a previously cached ADD at the same PC. */
        cpu.x[0]=0x91000842;cpu.x[1]=base;word(m,4,0xb9000020);break;
    case 4: /* Identical ADR words at distinct PCs must retain PC specialization. */
        for(unsigned i=0;i<65;i++)word(m,i*4,0x10000004);
        word(m,260,0x17ffffbf);budget=264;break;
    case 5: /* MRS X0,TPIDR_EL1 becomes undefined in the EL0 profile. */
        word(m,0,0xd538d080);word(m,4,0x17ffffff);m->change_el=1;break;
    case 6:m->data_fault=1;break;
    case 7:word(m,0,0xffffffff);break;
    case 8: /* Full-width BFM X2,X1,#0,#63 has a larger native entry. */
        word(m,0,0xb340fc22);word(m,4,0x17ffffff);break;
    default:break;
    }
    uint8_t *allocation=mmap(0,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(allocation!=MAP_FAILED);CHECK(capacity<=65536);
    protection p={0,0,0,fail_call,allocation,capacity};vf_code code={allocation,capacity,0};
    vf_memory_run_result_v1 result={0};result.abi_version=1;result.struct_size=sizeof(result);
    int status=vf_run_memory_provider(&cpu,&code,budget,protect,&p,callback,m,&result);
    CHECK(cpu.status==(unsigned)status);
    if(fail_call)CHECK(status==VF_PROTECTION && cpu.retired==0);
    else if(capacity==16)CHECK(status==VF_CODE_FULL && cpu.retired==0);
    else if(kind==1)CHECK(status==VF_DATA_FAULT && result.provider_status==VF_PROVIDER_CALLBACK_FAILURE && cpu.retired==4);
    else if(kind==2)CHECK(status==VF_DATA_FAULT && result.provider_status==VF_PROVIDER_INVALID_REPLY && cpu.retired==4);
    else if(kind==5)CHECK(status==VF_UNDEFINED_INSTRUCTION && cpu.retired==2 && cpu.exception_pending!=0);
    else if(kind==6)CHECK(status==VF_DATA_FAULT && result.provider_status==VF_PROVIDER_UNSUPPORTED && cpu.retired==1);
    else if(kind==7)CHECK(status==VF_UNDEFINED_INSTRUCTION && cpu.retired==0);
    else {CHECK(status==VF_BUDGET && cpu.retired==budget);
        if(kind==0)CHECK(cpu.x[2]==103 && read_le(m->ram+8192,8)==102);
        if(kind==3)CHECK(cpu.x[2]==188 && read_le(m->ram,4)==0x91000842);
        if(kind==4)CHECK(cpu.x[4]==base+256 && cpu.pc==base);
        if(kind==8)CHECK(cpu.x[2]==cpu.x[1]);
    }
    CHECK(result.fetch_requests==m->fetches && cpu.compiled_blocks<=m->fetches);
    dump(directory,name,&cpu,&result,m,status);
    printf("%s %u %u %u %u %llu\n",name,p.calls,p.rw,p.rx,(unsigned)status,(unsigned long long)cpu.retired);
    /* The private runner does not own final restore; verify our caller can restore it. */
    CHECK(mprotect(allocation,65536,PROT_READ|PROT_WRITE)==0);memset(allocation,0,65536);
    CHECK(munmap(allocation,65536)==0);free(m);
}
int main(int argc,char **argv) {
    CHECK(argc==2);CHECK(vf_host_supported());
    run_case(argv[1],"loop",0,65536,0);
    run_case(argv[1],"fetch-failure",1,65536,0);
    run_case(argv[1],"fetch-malformed",2,65536,0);
    run_case(argv[1],"self-modifying",3,65536,0);
    run_case(argv[1],"pc-eviction",4,65536,0);
    run_case(argv[1],"el-specialization",5,65536,0);
    run_case(argv[1],"data-fault",6,65536,0);
    run_case(argv[1],"undefined",7,65536,0);
    run_case(argv[1],"slot-fallback",8,65536,0);
    run_case(argv[1],"small-buffer",0,512,0);
    run_case(argv[1],"full-buffer-overflow",0,16,0);
    run_case(argv[1],"rw-failure",0,65536,1);
    run_case(argv[1],"rx-failure",0,65536,2);
    return 0;
}
