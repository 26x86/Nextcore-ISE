/* SPDX-License-Identifier: BSD-4-Clause
 * One-way control dispatcher. Native ALU generation and all memory decoding,
 * reply validation and fault commitment remain the canonical shared path. */
#include "memory_dynamic.h"
static vf_dynamic_snapshot dynamic_snapshot(const vf_memory_controls_v2 *c) {
    return (vf_dynamic_snapshot){c->sctlr,c->ttbr0,c->ttbr1,c->tcr,c->mair,c->hcr,c->scr,0};
}
static vf_memory_controls_v2 dynamic_controls(vf_dynamic_snapshot s,uint64_t epoch) {
    return (vf_memory_controls_v2){3,80,2,0,s.sctlr,s.ttbr0,s.ttbr1,s.tcr,s.mair,s.hcr,s.scr,epoch};
}
static int dynamic_equal(vf_dynamic_snapshot a,vf_dynamic_snapshot b) {
    return a.sctlr==b.sctlr && a.ttbr0==b.ttbr0 && a.ttbr1==b.ttbr1 && a.tcr==b.tcr &&
        a.mair==b.mair && a.hcr==b.hcr && a.scr==b.scr && a.reserved==b.reserved;
}
static int dynamic_cpu_match(const vf_cpu *cpu,vf_dynamic_snapshot a) {
    return cpu->current_el==VF_EL1 && cpu->sctlr==a.sctlr && cpu->ttbr0==a.ttbr0 &&
        cpu->ttbr1==a.ttbr1 && cpu->tcr==a.tcr && cpu->mair==a.mair &&
        cpu->hcr_el2==a.hcr && cpu->scr_el3==a.scr && !(cpu->pstate&~UINT64_C(0xf00003cf));
}
static int dynamic_failure(vf_memory_run_result_dynamic *out,unsigned reason,int uncertain) {
    if(uncertain)out->final_control.state_tag=VF_DYNAMIC_UNCERTAIN;
    return v2_error(&out->memory,reason);
}
static int dynamic_error_valid(const vf_dynamic_request *q,const vf_dynamic_reply *r) {
    vf_dynamic_snapshot zero={0};
    return r->abi_version==1 && r->struct_size==192 && r->phase==q->phase &&
        r->operation==q->operation && r->selector==q->selector && r->result>=1 && r->result<=3 &&
        r->detail>=1 && r->detail<=10 && !r->state_tag && !r->token && !r->revision && !r->epoch && !r->invalidations &&
        dynamic_equal(r->architectural,zero) && dynamic_equal(r->effective,zero);
}
static int dynamic_reply_equal(const vf_dynamic_reply *r,const vf_dynamic_reply *e) {
    return r->abi_version==e->abi_version && r->struct_size==e->struct_size && r->result==e->result &&
        r->detail==e->detail && r->phase==e->phase && r->operation==e->operation && r->selector==e->selector &&
        r->state_tag==e->state_tag && r->token==e->token && r->revision==e->revision && r->epoch==e->epoch &&
        r->invalidations==e->invalidations && dynamic_equal(r->architectural,e->architectural) && dynamic_equal(r->effective,e->effective);
}
static int dynamic_operation(uint32_t w,unsigned *op,unsigned *selector) {
    *selector=0;
    if(w==VF_DYNAMIC_ISB_WORD){*op=VF_DYNAMIC_ISB;return 1;}
    if(w==VF_DYNAMIC_DSB_WORD){*op=VF_DYNAMIC_DSB;return 1;}
    if(w==VF_DYNAMIC_TLBI_WORD){*op=VF_DYNAMIC_TLBI;return 1;}
    if(!is_mrs_msr(w) || (w&0x00200000))return 0;
    switch(sysreg_key(w)) {
    case VF_SYSREG_KEY_SCTLR_EL1:*selector=VF_DYNAMIC_SCTLR;break;
    case VF_SYSREG_KEY_TTBR0_EL1:*selector=VF_DYNAMIC_TTBR0;break;
    case VF_SYSREG_KEY_TTBR1_EL1:*selector=VF_DYNAMIC_TTBR1;break;
    case VF_SYSREG_KEY_TCR_EL1:*selector=VF_DYNAMIC_TCR;break;
    case VF_SYSREG_KEY_MAIR_EL1:*selector=VF_DYNAMIC_MAIR;break;
    default:return 0;
    }
    *op=VF_DYNAMIC_WRITE;return 1;
}
static int dynamic_control_step(vf_cpu *cpu,unsigned op,unsigned selector,
    vf_dynamic_callback callback,void *owner,vf_memory_run_result_dynamic *out) {
    const vf_dynamic_reply known=out->final_control;
    vf_dynamic_request q={.abi_version=1,.struct_size=192,.phase=VF_DYNAMIC_PREPARE,
        .operation=op,.selector=selector,.current_el=VF_EL1,.pc=cpu->pc,
        .revision=known.revision,.epoch=known.epoch,.before=known.architectural,.candidate=known.architectural};
    if(op==VF_DYNAMIC_WRITE) {
        q.operand=memory_register(cpu,cpu->instruction&31,0);
        switch(selector) {
        case VF_DYNAMIC_SCTLR:q.candidate.sctlr=q.operand;break;
        case VF_DYNAMIC_TTBR0:q.candidate.ttbr0=q.operand;break;
        case VF_DYNAMIC_TTBR1:q.candidate.ttbr1=q.operand;break;
        case VF_DYNAMIC_TCR:q.candidate.tcr=q.operand;break;
        default:q.candidate.mair=q.operand;break;
        }
    }
    vf_dynamic_reply expected=known;expected.phase=VF_DYNAMIC_PREPARE;expected.operation=op;expected.selector=selector;
    expected.state_tag=VF_DYNAMIC_PROPOSED;expected.token=0;
    if(op==VF_DYNAMIC_WRITE) {
        vf_memory_controls_v2 candidate=dynamic_controls(q.candidate,known.epoch);
        uint64_t fixed=(UINT64_C(3)<<14)|(UINT64_C(3)<<30)|(UINT64_C(7)<<32);
        if(known.architectural.sctlr&1 || !vf_dynamic_controls_valid(&candidate) ||
           ((known.effective.tcr^candidate.tcr)&fixed) || known.revision==UINT64_MAX ||
           (selector==VF_DYNAMIC_SCTLR && (q.operand!=(known.architectural.sctlr|1) ||
             !dynamic_equal(known.architectural,known.effective) || q.pc>UINT64_MAX-7)))
            return dynamic_failure(out,VF_PROVIDER_UNSUPPORTED,0);
        expected.architectural=q.candidate;expected.revision++;
    } else if(op==VF_DYNAMIC_ISB && !dynamic_equal(known.architectural,known.effective)) {
        if(known.epoch==UINT64_MAX)return dynamic_failure(out,VF_PROVIDER_UNSUPPORTED,0);
        expected.effective=known.architectural;expected.epoch++;
    } else if(op==VF_DYNAMIC_TLBI) {
        if(known.invalidations==UINT64_MAX)return dynamic_failure(out,VF_PROVIDER_UNSUPPORTED,0);
        expected.invalidations++;
    }
    vf_dynamic_reply reply={0};
    if(callback(owner,&q,&reply))return dynamic_failure(out,VF_PROVIDER_CALLBACK_FAILURE,0);
    if(reply.result) {
        if(!dynamic_error_valid(&q,&reply))return dynamic_failure(out,VF_PROVIDER_INVALID_REPLY,0);
        return dynamic_failure(out,reply.result==VF_DYNAMIC_UNSUPPORTED?VF_PROVIDER_UNSUPPORTED:
            reply.result==VF_DYNAMIC_UNAVAILABLE?VF_PROVIDER_UNAVAILABLE_V2:VF_PROVIDER_INVALID_REQUEST,0);
    }
    expected.token=reply.token;
    if(!reply.token || !dynamic_reply_equal(&reply,&expected))return dynamic_failure(out,VF_PROVIDER_INVALID_REPLY,0);
    q.phase=VF_DYNAMIC_COMMIT;q.operand=reply.token;
    expected.phase=VF_DYNAMIC_COMMIT;expected.state_tag=VF_DYNAMIC_KNOWN;expected.token=0;
    reply=(vf_dynamic_reply){0};
    /* Once COMMIT is sent, transport or malformed acknowledgement cannot prove
     * whether the service committed. Preserve only the last acknowledged state. */
    if(callback(owner,&q,&reply))return dynamic_failure(out,VF_PROVIDER_CALLBACK_FAILURE,1);
    if(!dynamic_reply_equal(&reply,&expected))return dynamic_failure(out,VF_PROVIDER_INVALID_REPLY,1);
    cpu->sctlr=expected.architectural.sctlr;cpu->ttbr0=expected.architectural.ttbr0;cpu->ttbr1=expected.architectural.ttbr1;
    cpu->tcr=expected.architectural.tcr;cpu->mair=expected.architectural.mair;
    expected.phase=0;expected.operation=0;expected.selector=0;out->final_control=expected;
    cpu->pc+=4;cpu->retired++;vf_cpu_advance_counter(cpu,1);return VF_NEXT;
}
int vf_run_memory_provider_dynamic(vf_cpu *cpu,vf_code *code,uint64_t budget,
    vf_protect protect,void *opaque,const vf_memory_controls_v2 *initial,
    vf_memory_callback_v2 memory,vf_dynamic_callback control,void *owner,vf_memory_run_result_dynamic *out) {
    if(!out)return VF_DATA_FAULT;
    if(!cpu || !code || !code->bytes || !protect || !memory || !control || !owner || !budget ||
       !vf_dynamic_controls_valid(initial) || (initial->sctlr&1) || initial->epoch!=1 ||
       !vf_cpu_state_valid(cpu) || cpu->exception_pending!=VF_EXCEPTION_NONE ||
       cpu->current_el!=VF_EL1 || (cpu->pstate&0xc0)!=0xc0)return dynamic_failure(out,VF_PROVIDER_INVALID_REQUEST,0);
    out->final_control=(vf_dynamic_reply){.abi_version=1,.struct_size=192,.state_tag=VF_DYNAMIC_KNOWN,
        .revision=1,.epoch=1,.architectural=dynamic_snapshot(initial),.effective=dynamic_snapshot(initial)};
    uint64_t start=cpu->retired,enable_pc=0;int enabling=0;
    while(cpu->retired-start<budget) {
        if(!dynamic_cpu_match(cpu,out->final_control.architectural))return cpu->status=dynamic_failure(out,VF_PROVIDER_INVALID_REQUEST,0);
        int pending=!dynamic_equal(out->final_control.architectural,out->final_control.effective);
        if(pending && (cpu->pstate&0xc0)!=0xc0)return cpu->status=dynamic_failure(out,VF_PROVIDER_UNSUPPORTED,0);
        int status=vf_cpu_poll_interrupt(cpu);if(status!=VF_NEXT)return cpu->status=status;
        vf_memory_controls_v2 effective=dynamic_controls(out->final_control.effective,out->final_control.epoch);
        vf_memory_request_v2 q=memory_request_v2(cpu,&effective,VF_MEMORY_FETCH,4,1,cpu->pc);
        q.abi_version=3;q.controls=effective;
        vf_memory_reply_v2 reply;status=memory_exchange_version(cpu,memory,owner,&q,&reply,&out->memory,3);
        if(status!=VF_NEXT)return cpu->status=status;
        uint32_t instruction=(uint32_t)reply.value0;
        if(enabling && (cpu->pc!=enable_pc || instruction!=VF_DYNAMIC_ISB_WORD))
            return cpu->status=dynamic_failure(out,VF_PROVIDER_UNSUPPORTED,0);
        /* DAIF writes remain explicit unsupported system boundaries, including
         * before any pending control can be interrupted. No hidden PSTATE write. */
        if(protect(code->bytes,code->capacity,0,opaque))return cpu->status=VF_PROTECTION;
        status=translate_impl(code,cpu,(const uint8_t *)&instruction,4,cpu->pc,1,1);
        if(status!=VF_NEXT)return cpu->status=status;
        if(protect(code->bytes,code->capacity,1,opaque))return cpu->status=VF_PROTECTION;
        status=execute_native_block(cpu,code,0,0);
        if((uint32_t)status==VF_MEMORY_DISPATCH) {
            status=memory_data_step_version(cpu,&effective,memory,owner,&out->memory,3);
            if(status!=VF_NEXT)return cpu->status=status;continue;
        }
        if(status==VF_UNDEFINED_INSTRUCTION || status==VF_SYSTEM_REGISTER_TRAP) {
            unsigned op,selector;
            if(dynamic_operation(instruction,&op,&selector)) {
                uint64_t old_pc=cpu->pc;
                status=dynamic_control_step(cpu,op,selector,control,owner,out);
                if(status!=VF_NEXT)return cpu->status=status;
                if(op==VF_DYNAMIC_WRITE && selector==VF_DYNAMIC_SCTLR){enabling=1;enable_pc=old_pc+4;}
                if(op==VF_DYNAMIC_ISB)enabling=0;
                continue;
            }
            if(platform_slow_step(cpu) || memory_control_read_v2(cpu))continue;
        }
        if(status!=VF_NEXT){(void)vf_cpu_commit_status(cpu,status);return cpu->status=status;}
    }
    return cpu->status=VF_BUDGET;
}
