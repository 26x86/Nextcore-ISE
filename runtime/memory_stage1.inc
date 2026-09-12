/* SPDX-License-Identifier: BSD-4-Clause
 * Compiled in jit.c to share the existing memory decoder/native emitter.
 * This file contains no second instruction or page-table decoder. */
static int v2_error(vf_memory_run_result_v2 *result,unsigned reason) {
    return provider_error(&result->base,reason);
}
static int v2_controls_match(const vf_cpu *cpu,const vf_memory_controls_v2 *c) {
    return cpu->sctlr==c->sctlr && cpu->ttbr0==c->ttbr0 && cpu->ttbr1==c->ttbr1 &&
        cpu->tcr==c->tcr && cpu->mair==c->mair && cpu->hcr_el2==c->hcr && cpu->scr_el3==c->scr &&
        cpu->current_el<=VF_EL1 && !(cpu->pstate&~UINT64_C(0xf00003cf));
}
static int v2_provenance_valid(const vf_memory_reply_v2 *r) {
    if(r->metadata_flags&~3u || (!(r->metadata_flags&1) && r->descriptor_pa) ||
       (!(r->metadata_flags&2) && r->output_pa) ||
       ((r->metadata_flags&1) && ((r->descriptor_pa&7) || r->descriptor_pa>>48)) ||
       ((r->metadata_flags&2) && r->output_pa>>48) || r->context>4)return 0;
    if(r->context==0)return r->level==UINT32_MAX && !(r->metadata_flags&1);
    if(r->context==VF_MEMORY_V2_INPUT)return !(r->metadata_flags&1) && (r->level==0 || r->level==UINT32_MAX);
    return r->level<=3 && (r->metadata_flags&1);
}
static int v2_fault_valid(const vf_memory_request_v2 *q,const vf_memory_reply_v2 *r,uint64_t *esr) {
    int fetch=q->operation==VF_MEMORY_FETCH;
    int misaligned=(q->address&(q->width-1))!=0;
    /* The CPU knows alignment before consulting any table. A callback may
     * report a host failure, but may not replace this architectural priority. */
    if(misaligned && r->fault!=(fetch?VF_MEMORY_V2_PC_ALIGNMENT:VF_MEMORY_V2_DATA_ALIGNMENT))return 0;
    if(r->fault==VF_MEMORY_V2_PC_ALIGNMENT || r->fault==VF_MEMORY_V2_DATA_ALIGNMENT) {
        if(r->address!=q->address || r->level!=UINT32_MAX || r->context!=VF_MEMORY_V2_INPUT || r->metadata_flags)return 0;
        if(r->fault==VF_MEMORY_V2_PC_ALIGNMENT) {
            if(!fetch || !(q->pc&3) || r->fsc)return 0;
            *esr=UINT64_C(0x8a000000);return 1;
        }
        if(fetch || !(q->address&(q->width-1)) || r->fsc!=0x21)return 0;
        *esr=((UINT64_C(0x24)+q->current_el)<<26)|(UINT64_C(1)<<25)|0x21|
            (q->operation==VF_MEMORY_STORE?64:0);return 1;
    }
    if(r->level>3 || r->fault<VF_MEMORY_V2_ADDRESS_SIZE || r->fault>VF_MEMORY_V2_ACCESS_FLAG ||
       ((r->address-q->address)%q->width))return 0;
    unsigned fsc;
    switch(r->fault) {
    case VF_MEMORY_V2_ADDRESS_SIZE:fsc=r->level;break;
    case VF_MEMORY_V2_TRANSLATION:fsc=4+r->level;break;
    case VF_MEMORY_V2_PERMISSION:fsc=12+r->level;break;
    default:fsc=8+r->level;break;
    }
    if(fsc!=r->fsc || !r->context)return 0;
    int sixteen=((q->controls.tcr>>14)&3)==2;
    static const unsigned physical_bits[6]={32,36,40,42,44,48};
    uint64_t physical_mask=(UINT64_C(1)<<physical_bits[(q->controls.tcr>>32)&7])-1;
    if(r->context==VF_MEMORY_V2_INPUT) {
        if(r->level)return 0;
        if(r->fault==VF_MEMORY_V2_TRANSLATION) {if(r->metadata_flags)return 0;}
        else if(r->fault==VF_MEMORY_V2_ADDRESS_SIZE) {
            if(r->metadata_flags!=2 || !(r->output_pa&~physical_mask))return 0;
        } else return 0;
    } else {
        unsigned tsz=(r->address>>63)?(q->controls.tcr>>16)&63:q->controls.tcr&63;
        unsigned page=sixteen?14:12,bits=sixteen?11:9;
        unsigned levels=(64-tsz-page+bits-1)/bits,start=4-levels;
        if(r->level<start)return 0;
        if(r->context==VF_MEMORY_V2_WALK) {
            if(r->fault==VF_MEMORY_V2_TRANSLATION) {
                if(r->metadata_flags!=1 || r->descriptor_pa>physical_mask-7)return 0;
            } else if(r->fault==VF_MEMORY_V2_ADDRESS_SIZE) {
                if(!(r->metadata_flags&1) || (r->descriptor_pa<=physical_mask-7 &&
                    (!(r->metadata_flags&2) || !(r->output_pa&~physical_mask))))return 0;
            } else return 0;
        } else {
            if(r->level<(sixteen?2u:1u) || r->metadata_flags!=3 || r->descriptor_pa>physical_mask-7)return 0;
            if(r->context==VF_MEMORY_V2_CACHED_LEAF) {
                /* A successful immutable fill cannot later discover an AF,
                 * translation, or output-width error. Permissions vary by EL. */
                if(r->fault!=VF_MEMORY_V2_PERMISSION)return 0;
            } else if(r->context!=VF_MEMORY_V2_LEAF ||
                (r->fault!=VF_MEMORY_V2_PERMISSION && r->fault!=VF_MEMORY_V2_ACCESS_FLAG &&
                 r->fault!=VF_MEMORY_V2_ADDRESS_SIZE))return 0;
            if((r->output_pa&~physical_mask)!=0) {
                if(r->fault!=VF_MEMORY_V2_ADDRESS_SIZE)return 0;
            } else if(r->fault==VF_MEMORY_V2_ADDRESS_SIZE)return 0;
        }
    }
    *esr=((uint64_t)((fetch?0x20:0x24)+q->current_el)<<26)|(UINT64_C(1)<<25)|fsc|
        (q->operation==VF_MEMORY_STORE?64:0);
    return 1;
}
static int memory_exchange_version(vf_cpu *cpu,vf_memory_callback_v2 callback,void *owner,
    const vf_memory_request_v2 *q,vf_memory_reply_v2 *r,vf_memory_run_result_v2 *result,unsigned version) {
    *r=(vf_memory_reply_v2){0};
    result->last_reply=(vf_memory_reply_v2){.abi_version=version,.struct_size=128,.level=UINT32_MAX,.epoch=q->controls.epoch};
    result->base.last_address=q->address;
    if(q->operation==VF_MEMORY_FETCH)result->base.fetch_requests++;else result->base.data_requests++;
    if(callback(owner,q,r))return v2_error(result,VF_PROVIDER_CALLBACK_FAILURE);
    if(r->abi_version!=version || r->struct_size!=128 || r->epoch!=q->controls.epoch || r->result>4 ||
       r->reserved[0] || r->reserved[1] || r->reserved[2] || r->reserved[3] || r->reserved[4] ||
       !v2_provenance_valid(r))return v2_error(result,VF_PROVIDER_INVALID_REPLY);
    uint64_t mask=q->width==8?UINT64_MAX:(UINT64_C(1)<<(q->width*8))-1;
    unsigned bytes=q->width*q->count;
    int wrapping=q->address>UINT64_MAX-(bytes-1);
    int physical_dynamic=version==3 && !(q->controls.sctlr&1);
    if(r->result==VF_MEMORY_V2_OK) {
        if(wrapping || (physical_dynamic && (q->address>=(UINT64_C(1)<<48) ||
               bytes>(UINT64_C(1)<<48)-q->address)) || (q->address&(q->width-1)) ||
           r->fault || r->fsc || r->esr || r->address || r->context || r->level!=UINT32_MAX || r->metadata_flags ||
           (r->value0&~mask) || (r->value1&~mask) || (q->count==1 && r->value1) ||
           (q->operation==VF_MEMORY_STORE && (r->value0 || r->value1)))return v2_error(result,VF_PROVIDER_INVALID_REPLY);
        result->last_reply=*r;return VF_NEXT;
    }
    if(r->value0 || r->value1)return v2_error(result,VF_PROVIDER_INVALID_REPLY);
    if(r->result==VF_MEMORY_V2_INVALID_REQUEST) {
        if(r->fault || r->fsc || r->esr || r->address || r->context || r->level!=UINT32_MAX || r->metadata_flags)
            return v2_error(result,VF_PROVIDER_INVALID_REPLY);
        result->last_reply=*r;return v2_error(result,VF_PROVIDER_INVALID_REQUEST);
    }
    if(r->address<q->address || r->address-q->address>=bytes)return v2_error(result,VF_PROVIDER_INVALID_REPLY);
    if(r->result==VF_MEMORY_V2_UNSUPPORTED || r->result==VF_MEMORY_V2_UNAVAILABLE) {
        if(r->fault || r->fsc || r->esr)return v2_error(result,VF_PROVIDER_INVALID_REPLY);
        if(physical_dynamic && (r->context || r->level!=UINT32_MAX ||
           (r->result==VF_MEMORY_V2_UNSUPPORTED?r->metadata_flags!=0:
            (r->metadata_flags!=2 || r->output_pa!=r->address))))return v2_error(result,VF_PROVIDER_INVALID_REPLY);
        result->last_reply=*r;result->base.last_address=r->address;
        return v2_error(result,r->result==VF_MEMORY_V2_UNAVAILABLE?VF_PROVIDER_UNAVAILABLE_V2:VF_PROVIDER_UNSUPPORTED);
    }
    if(wrapping && !(q->address&(q->width-1)))return v2_error(result,VF_PROVIDER_INVALID_REPLY);
    if(physical_dynamic && r->fault!=VF_MEMORY_V2_PC_ALIGNMENT && r->fault!=VF_MEMORY_V2_DATA_ALIGNMENT)
        return v2_error(result,VF_PROVIDER_INVALID_REPLY);
    uint64_t esr;
    if(!v2_fault_valid(q,r,&esr) || r->esr!=esr)return v2_error(result,VF_PROVIDER_INVALID_REPLY);
    enum vf_exception_kind kind=q->operation==VF_MEMORY_FETCH?VF_EXCEPTION_INSTRUCTION_ABORT:
        r->fault==VF_MEMORY_V2_DATA_ALIGNMENT?VF_EXCEPTION_ALIGNMENT_FAULT:VF_EXCEPTION_DATA_ABORT;
    int status=q->operation==VF_MEMORY_FETCH?VF_INSTRUCTION_ABORT:
        r->fault==VF_MEMORY_V2_DATA_ALIGNMENT?VF_ALIGNMENT_FAULT:VF_DATA_ABORT;
    if(vf_cpu_raise_exception(cpu,kind,cpu->pc,r->address,(uint32_t)esr,q->operation==VF_MEMORY_FETCH?0:cpu->instruction))
        return v2_error(result,VF_PROVIDER_INVALID_REPLY);
    result->last_reply=*r;result->base.last_address=r->address;return status;
}
static int memory_exchange_v2(vf_cpu *cpu,vf_memory_callback_v2 callback,void *owner,
    const vf_memory_request_v2 *q,vf_memory_reply_v2 *r,vf_memory_run_result_v2 *result) {
    return memory_exchange_version(cpu,callback,owner,q,r,result,2);
}
static vf_memory_request_v2 memory_request_v2(const vf_cpu *cpu,const vf_memory_controls_v2 *controls,
    unsigned op,unsigned width,unsigned count,uint64_t address) {
    vf_memory_request_v2 q={0};q.abi_version=2;q.struct_size=160;q.operation=op;
    q.width=width;q.count=count;q.current_el=cpu->current_el;q.pc=cpu->pc;q.address=address;
    q.pstate=cpu->pstate;q.controls=*controls;
    q.controls.sctlr=cpu->sctlr;q.controls.ttbr0=cpu->ttbr0;q.controls.ttbr1=cpu->ttbr1;
    q.controls.tcr=cpu->tcr;q.controls.mair=cpu->mair;q.controls.hcr=cpu->hcr_el2;q.controls.scr=cpu->scr_el3;
    return q;
}
static int memory_data_step_version(vf_cpu *cpu,const vf_memory_controls_v2 *controls,
    vf_memory_callback_v2 callback,void *owner,vf_memory_run_result_v2 *result,unsigned version) {
    memory_shape shape;
    if(!decode_memory(cpu->instruction,&shape)) {
        (void)vf_cpu_commit_status(cpu,VF_UNDEFINED_INSTRUCTION);return VF_UNDEFINED_INSTRUCTION;
    }
    uint64_t base=memory_register(cpu,shape.rn,1);
    if(shape.rn==31 && (cpu->sctlr&(cpu->current_el==VF_EL0?16u:8u)) && (base&15)) {
        cpu->far=base;(void)vf_cpu_commit_status(cpu,VF_SP_ALIGNMENT_FAULT);return VF_SP_ALIGNMENT_FAULT;
    }
    uint64_t updated=base+memory_offset(cpu,&shape),address=shape.mode==1?base:updated;
    vf_memory_request_v2 q=memory_request_v2(cpu,controls,shape.read?VF_MEMORY_LOAD:VF_MEMORY_STORE,shape.width,shape.count,address);
    if(version==3){q.abi_version=3;q.controls=*controls;}
    uint64_t mask=shape.width==8?UINT64_MAX:(UINT64_C(1)<<(shape.width*8))-1;
    if(!shape.read){q.value0=memory_register(cpu,shape.rt,0)&mask;if(shape.count==2)q.value1=memory_register(cpu,shape.rt2,0)&mask;}
    vf_memory_reply_v2 reply;int status=memory_exchange_version(cpu,callback,owner,&q,&reply,result,version);
    if(status!=VF_NEXT)return status;
    if(shape.read) {
        memory_save(cpu,shape.rt,shape.signed_load?(uint64_t)sext(reply.value0,shape.width*8):reply.value0,shape.result64);
        if(shape.count==2)memory_save(cpu,shape.rt2,reply.value1,shape.result64);
    }
    if(shape.mode!=2){if(shape.rn==31)cpu->sp=updated;else cpu->x[shape.rn]=updated;}
    cpu->pc+=4;cpu->retired++;vf_cpu_advance_counter(cpu,1);result->base.completed_data_operations++;return VF_NEXT;
}
static int memory_data_step_v2(vf_cpu *cpu,const vf_memory_controls_v2 *controls,
    vf_memory_callback_v2 callback,void *owner,vf_memory_run_result_v2 *result) {
    return memory_data_step_version(cpu,controls,callback,owner,result,2);
}
static int memory_control_read_v2(vf_cpu *cpu) {
    uint32_t w=cpu->instruction;
    if(!is_mrs_msr(w) || !(w&0x00200000))return 0;
    uint32_t key=sysreg_key(w);
    if(key!=VF_SYSREG_KEY_SCTLR_EL1 && key!=VF_SYSREG_KEY_TTBR0_EL1 && key!=VF_SYSREG_KEY_TTBR1_EL1 &&
       key!=VF_SYSREG_KEY_TCR_EL1 && key!=VF_SYSREG_KEY_MAIR_EL1)return 0;
    uint64_t value;if(vf_cpu_read_sysreg(cpu,key,&value))return 0;
    if((w&31)!=31)cpu->x[w&31]=value;
    cpu->pc+=4;cpu->retired++;vf_cpu_advance_counter(cpu,1);return 1;
}
int vf_run_memory_provider_v2(vf_cpu *cpu,vf_code *code,uint64_t budget,
    vf_protect protect,void *opaque,const vf_memory_controls_v2 *controls,
    vf_memory_callback_v2 callback,void *owner,vf_memory_run_result_v2 *result) {
    if(!result)return VF_DATA_FAULT;
    if(!cpu || !code || !code->bytes || !protect || !callback || !owner || !budget ||
       !vf_memory_controls_valid_v2(controls) || !vf_cpu_state_valid(cpu) ||
       cpu->exception_pending!=VF_EXCEPTION_NONE)return v2_error(result,VF_PROVIDER_INVALID_REQUEST);
    uint64_t start=cpu->retired;
    while(cpu->retired-start<budget) {
        if(!v2_controls_match(cpu,controls))return cpu->status=v2_error(result,VF_PROVIDER_INVALID_REQUEST);
        int status=vf_cpu_poll_interrupt(cpu);if(status!=VF_NEXT)return cpu->status=status;
        vf_memory_request_v2 q=memory_request_v2(cpu,controls,VF_MEMORY_FETCH,4,1,cpu->pc);
        vf_memory_reply_v2 reply;status=memory_exchange_v2(cpu,callback,owner,&q,&reply,result);
        if(status!=VF_NEXT)return cpu->status=status;
        uint32_t instruction=(uint32_t)reply.value0;
        if(protect(code->bytes,code->capacity,0,opaque))return cpu->status=VF_PROTECTION;
        status=translate_impl(code,cpu,(const uint8_t *)&instruction,4,cpu->pc,1,1);
        if(status!=VF_NEXT)return cpu->status=status;
        if(protect(code->bytes,code->capacity,1,opaque))return cpu->status=VF_PROTECTION;
        status=execute_native_block(cpu,code,0,0);
        if((uint32_t)status==VF_MEMORY_DISPATCH) {
            status=memory_data_step_v2(cpu,controls,callback,owner,result);
            if(status!=VF_NEXT)return cpu->status=status;continue;
        }
        if((status==VF_UNDEFINED_INSTRUCTION || status==VF_SYSTEM_REGISTER_TRAP) &&
           (platform_slow_step(cpu) || memory_control_read_v2(cpu)))continue;
        if(status!=VF_NEXT){(void)vf_cpu_commit_status(cpu,status);return cpu->status=status;}
    }
    return cpu->status=VF_BUDGET;
}
