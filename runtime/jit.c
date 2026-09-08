/* SPDX-License-Identifier: BSD-4-Clause
 * Original, specification-based A64 subset translator; no QEMU code incorporated.
 * Generated ABI: Microsoft x64; RCX=cpu, RDX=RAM, R8=RAM size. Only volatile
 * RAX/R9/R10/R11 and flags are modified; no stack or helper calls in JIT code.
 */
#include "jit.h"
static void b(vf_code *c, unsigned x) { if (c->used < c->capacity) c->bytes[c->used] = (uint8_t)x; ++c->used; }
static void u32(vf_code *c, uint32_t x) { for (int i=0;i<4;i++) b(c,x>>(8*i)); }
static void u64(vf_code *c, uint64_t x) { for (int i=0;i<8;i++) b(c,(unsigned)(x>>(8*i))); }
static void imm(vf_code *c,uint64_t x) { b(c,0x48);b(c,0xb8);u64(c,x); }
static void load(vf_code *c,unsigned reg,int sp,int wide) {
    if (reg==31) {
        if (!sp) { b(c,0x31);b(c,0xc0); return; }
        if (wide) b(c,0x48); b(c,0x8b);b(c,0x81);u32(c,offsetof(vf_cpu,sp)); return;
    }
    if(wide)b(c,0x48); b(c,0x8b);b(c,0x81);u32(c,reg*8);
}
static void save(vf_code *c,unsigned reg,int sp) {
    if(reg==31) { if(sp) { b(c,0x48);b(c,0x89);b(c,0x81);u32(c,offsetof(vf_cpu,sp)); } return; }
    b(c,0x48);b(c,0x89);b(c,0x81);u32(c,reg*8);
}
static void field(vf_code *c,unsigned off,uint64_t x) { imm(c,x);b(c,0x48);b(c,0x89);b(c,0x81);u32(c,off); }
static void field32(vf_code *c,unsigned off,uint32_t x) {
    b(c,0xc7);b(c,0x81);u32(c,off);u32(c,x);
}
static void save_host_r11(vf_code *c,unsigned off) {
    b(c,0x4c);b(c,0x89);b(c,0x99);u32(c,off);
}
static void save_arithmetic_flags(vf_code *c,int subtract) {
    /* Capture every flag before shifts/ORs clobber host flags. AArch64 C
     * means no borrow for subtraction, the inverse of x86 CF. The result
     * has already been saved; AL can now hold the overflow predicate. */
    b(c,0x41);b(c,0x0f);b(c,0x98);b(c,0xc1); /* sets r9b */
    b(c,0x41);b(c,0x0f);b(c,0x94);b(c,0xc2); /* setz r10b */
    b(c,0x41);b(c,0x0f);b(c,subtract?0x93:0x92);b(c,0xc3);
    b(c,0x0f);b(c,0x90);b(c,0xc0); /* seto al */
    b(c,0x45);b(c,0x0f);b(c,0xb6);b(c,0xc9);
    b(c,0x45);b(c,0x0f);b(c,0xb6);b(c,0xd2);
    b(c,0x45);b(c,0x0f);b(c,0xb6);b(c,0xdb);
    b(c,0x0f);b(c,0xb6);b(c,0xc0);
    b(c,0x41);b(c,0xc1);b(c,0xe1);b(c,31);
    b(c,0x41);b(c,0xc1);b(c,0xe2);b(c,30);
    b(c,0x41);b(c,0xc1);b(c,0xe3);b(c,29);
    b(c,0xc1);b(c,0xe0);b(c,28);
    b(c,0x44);b(c,0x09);b(c,0xc8);
    b(c,0x44);b(c,0x09);b(c,0xd0);
    b(c,0x44);b(c,0x09);b(c,0xd8);
    b(c,0x49);b(c,0xb9);u64(c,~UINT64_C(0xf0000000));
    b(c,0x4c);b(c,0x21);b(c,0x89);u32(c,offsetof(vf_cpu,pstate));
    b(c,0x48);b(c,0x09);b(c,0x81);u32(c,offsetof(vf_cpu,pstate));
}
static void finish(vf_code *c,uint64_t pc,unsigned count,unsigned status) {
    field(c,offsetof(vf_cpu,pc),pc);
    b(c,0x48);b(c,0x81);b(c,0x81);u32(c,offsetof(vf_cpu,retired));u32(c,count);
    b(c,0xb8);u32(c,status);b(c,0xc3);
}
static size_t jcc(vf_code *c,unsigned cc) { b(c,0x0f);b(c,cc);size_t p=c->used;u32(c,0);return p; }
static void fix_to(vf_code *c,size_t p,size_t target) {
    uint32_t rel=(uint32_t)(target-p-4);
    if(p+4<=c->capacity)for(int i=0;i<4;i++)c->bytes[p+i]=(uint8_t)(rel>>(8*i));
}
static void fix(vf_code *c,size_t p) { fix_to(c,p,c->used); }
static uint32_t word(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int64_t sext(uint32_t x,unsigned bits) { return (int64_t)(int32_t)(x<<(32-bits))>>(32-bits); }
static uint32_t sysreg_key(uint32_t w) { return (w >> 5) & 0x7fff; }
static int is_system_encoding(uint32_t w) { return (w & 0xffc00000) == 0xd5000000; }
static int is_exception_return(uint32_t w) {
    return w == 0xd69f03e0 || w == 0xd69f0be0 || w == 0xd69f0fe0;
}

static int sysreg_el0_visible(uint32_t key) {
    switch (key) {
    case 0x5801: /* CTR_EL0 */
    case 0x5802: /* DCZID_EL0 */
    case 0x5a20: /* FPCR */
    case 0x5a21: /* FPSR */
    case 0x5e82: /* TPIDR_EL0 */
    case 0x5e83: /* TPIDRRO_EL0 */
    case VF_SYSREG_KEY_CNTFRQ_EL0:
    case VF_SYSREG_KEY_CNTPCT_EL0:
    case VF_SYSREG_KEY_CNTVCT_EL0:
    case VF_SYSREG_KEY_CNTP_TVAL_EL0:
    case VF_SYSREG_KEY_CNTP_CTL_EL0:
    case VF_SYSREG_KEY_CNTP_CVAL_EL0:
    case VF_SYSREG_KEY_CNTV_CTL_EL0:
    case VF_SYSREG_KEY_CNTV_CVAL_EL0:
    case VF_SYSREG_KEY_CURRENT_EL:
        return 1;
    default:
        return 0;
    }
}

static int sysreg_el1_only(uint32_t key) {
    switch (key) {
    case VF_SYSREG_KEY_ID_AA64MMFR0_EL1:
    case VF_SYSREG_KEY_ID_AA64ISAR1_EL1:
    case VF_SYSREG_KEY_SCTLR_EL1:
    case VF_SYSREG_KEY_TTBR0_EL1:
    case VF_SYSREG_KEY_TTBR1_EL1:
    case VF_SYSREG_KEY_TCR_EL1:
    case VF_SYSREG_KEY_MAIR_EL1:
    case VF_SYSREG_KEY_VBAR_EL1:
    case VF_SYSREG_KEY_ESR_EL1:
    case VF_SYSREG_KEY_FAR_EL1:
    case VF_SYSREG_KEY_ELR_EL1:
    case VF_SYSREG_KEY_SPSR_EL1:
        return 1;
    default:
        return 0;
    }
}

static int is_mrs_msr(uint32_t w) {
    return (w & 0xffe00000) == 0xd5200000 ||
           (w & 0xffe00000) == 0xd5000000;
}

static int known_privileged(uint32_t w, uint32_t current_el) {
    /* ERET is legal only at an exception level that owns an exception bank;
     * EL1+ is still a system-register phase boundary until SPSR/ELR reads are
     * implemented.  DAIF writes are privileged at EL0. */
    if (is_exception_return(w))
        return current_el == VF_EL0;
    if (w == 0xd503207f || w == 0xd503205f ||
        (w & 0xffe0001f) == 0xd4000002 ||
        (w & 0xffe0001f) == 0xd4000003)
        return current_el == VF_EL0;
    if ((w & 0xfffff0ff) == 0xd50340df || (w & 0xfffff0ff) == 0xd503409f)
        return current_el == VF_EL0;
    if (is_mrs_msr(w) && current_el == VF_EL0 &&
        !sysreg_el0_visible(sysreg_key(w)) &&
        sysreg_el1_only(sysreg_key(w)))
        return 1;
    return 0;
}

static int system_boundary(uint32_t w, uint32_t current_el) {
    if (is_exception_return(w)) return VF_SYSTEM_REGISTER_TRAP;
    if (!is_system_encoding(w)) return 0;
    if (w == 0xd503201f) return 0; /* NOP */
    if (known_privileged(w, current_el)) return VF_PRIVILEGE_FAULT;
    return VF_SYSTEM_REGISTER_TRAP;
}

static int translate_impl(vf_code *c,const vf_cpu *cpu,const uint8_t *guest,
                          size_t size,uint64_t pc,unsigned limit) {
    uint32_t current_el = cpu ? cpu->current_el : VF_EL0;
    uint64_t base = cpu ? cpu->guest_ram_base : 0;
    c->used=0;
    for(unsigned n=0;n<limit;n++,pc+=4) {
        if((pc&3) || size<4 || pc<base || pc-base>size-4 || pc>UINT64_MAX-4) { finish(c,pc,n,VF_INSTRUCTION_ABORT);break; }
        uint32_t w=word(guest+(pc-base)); unsigned rd=w&31,rn=(w>>5)&31,wide=w>>31;
        if((w&0x7f800000)==0x52800000 || (w&0x7f800000)==0x72800000 || (w&0x7f800000)==0x12800000) {
            unsigned shift=((w>>21)&3)*16; uint64_t v=(uint64_t)((w>>5)&65535)<<shift;
            if(!wide && shift>=32) {
                field32(c,offsetof(vf_cpu,instruction),w);
                finish(c,pc,n,VF_UNDEFINED_INSTRUCTION);
                break;
            }
            if((w&0x7f800000)==0x72800000) {
                load(c,rd,0,wide); b(c,0x49);b(c,0xb9);u64(c,~(UINT64_C(65535)<<shift));
                b(c,0x4c);b(c,0x21);b(c,0xc8);b(c,0x49);b(c,0xb9);u64(c,v);
                b(c,0x4c);b(c,0x09);b(c,0xc8);
            } else {
                if((w&0x7f800000)==0x12800000)v=wide?~v:(uint32_t)~v;
                imm(c,v);
            }
            save(c,rd,0);
        } else if((w&0x1f000000)==0x10000000) {
            /* ADR/ADRP use the guest PC; host allocation addresses never
             * participate in PC-relative address materialization. */
            int64_t displacement=sext((((w>>5)&0x7ffff)<<2)|((w>>29)&3),21);
            uint64_t target=(w>>31)?(pc&~UINT64_C(0xfff))+(uint64_t)(displacement*4096)
                                   :pc+(uint64_t)displacement;
            imm(c,target);save(c,rd,0);
        } else if((w&0x7fe0ffe0)==0x2a0003e0) {
            /* MOV register alias: ORR Xd/XZR/Xm, LSL #0. */
            load(c,(w>>16)&31,0,wide);save(c,rd,0);
        } else if((w&0x1f800000)==0x11000000) {
            /* ADD/SUB(S) immediate. Rn31 is SP; Rd31 is ZR for the flag
             * forms (CMP/CMN aliases), otherwise SP, including WSP. */
            uint32_t v=((w>>10)&4095)<<(((w>>22)&1)?12:0);
            int subtract=(w>>30)&1,flags=(w>>29)&1;
            load(c,rn,1,wide);if(wide)b(c,0x48);b(c,0x05+subtract*0x28);u32(c,v);
            save(c,rd,!flags);
            if(flags)save_arithmetic_flags(c,subtract);
        } else if((w&0x3fe0fc00)==0x0b000000) {
            /* ADD/SUB register, LSL #0 only, R31 is ZR. */
            unsigned rm=(w>>16)&31;load(c,rm,0,wide);b(c,0x49);b(c,0x89);b(c,0xc1);
            load(c,rn,0,wide);b(c,wide?0x4c:0x44);b(c,((w>>30)&1)?0x29:0x01);b(c,0xc8);save(c,rd,0);
        } else if((w&0xffc00000)==0xf9000000 || (w&0xffc00000)==0xf9400000) {
            /* Preserve the guest PA for faults and separately derive the
             * checked host-RAM offset. Host pointers never become guest PAs. */
            field32(c,offsetof(vf_cpu,instruction),w);
            load(c,rn,1,1);b(c,0x49);b(c,0x89);b(c,0xc1);
            b(c,0x49);b(c,0x81);b(c,0xc1);u32(c,((w>>10)&4095)*8);
            b(c,0x4d);b(c,0x89);b(c,0xcb); /* r11 = original guest address; flags retained */
            size_t carry=jcc(c,0x82); /* Preserve ADD's carry before TEST. */
            /* Keep the effective address in r9.  The fault blocks below
             * store it in FAR before the common architectural commit. */
            b(c,0x4d);b(c,0x89);b(c,0xca); /* r10 = r9 */
            b(c,0x49);b(c,0x83);b(c,0xe2);b(c,0x07); /* r10 &= 7 */
            b(c,0x4d);b(c,0x85);b(c,0xd2);
            size_t align=jcc(c,0x85); /* Unaligned 64-bit access. */
            imm(c,base);b(c,0x49);b(c,0x29);b(c,0xc1); /* r9 -= guest RAM base */
            size_t below=jcc(c,0x82);
            b(c,0x4d);b(c,0x89);b(c,0xc2);b(c,0x49);b(c,0x83);b(c,0xea);b(c,8);
            b(c,0x4d);b(c,0x39);b(c,0xd1);size_t bound=jcc(c,0x87);
            if(w&0x400000) { b(c,0x4a);b(c,0x8b);b(c,0x04);b(c,0x0a);save(c,rd,0); }
            else { load(c,rd,0,1);b(c,0x4a);b(c,0x89);b(c,0x04);b(c,0x0a); }
            b(c,0xe9);size_t next=c->used;u32(c,0);
            size_t alignment_target=c->used;
            save_host_r11(c,offsetof(vf_cpu,far));
            finish(c,pc,n,VF_ALIGNMENT_FAULT);
            size_t data_target=c->used;
            save_host_r11(c,offsetof(vf_cpu,far));
            finish(c,pc,n,VF_DATA_ABORT);
            size_t next_target=c->used;
            fix_to(c,align,alignment_target);
            fix_to(c,carry,data_target);
            fix_to(c,below,data_target);
            fix_to(c,bound,data_target);
            fix_to(c,next,next_target);
        } else if((w&0xff000010)==0x54000000) {
            unsigned condition=w&15;
            uint64_t target=pc+(uint64_t)(sext((w>>5)&0x7ffff,19)*4);
            if(condition>=14) { finish(c,target,n+1,VF_NEXT);break; }
            /* Compute each Arm predicate from committed NZCV, so intervening
             * instructions and host block boundaries cannot change it. */
            b(c,0x8b);b(c,0x81);u32(c,offsetof(vf_cpu,pstate));
            b(c,0xc1);b(c,0xe8);b(c,28);
            unsigned group=condition>>1,fall_cc=0x84;
            if(group<4) {
                const unsigned masks[4]={4,2,8,1};
                b(c,0x83);b(c,0xe0);b(c,masks[group]);
            } else if(group==4) {
                b(c,0x83);b(c,0xe0);b(c,6);
                b(c,0x83);b(c,0xf8);b(c,2);fall_cc=0x85;
            } else {
                b(c,0x41);b(c,0x89);b(c,0xc1);
                b(c,0x41);b(c,0xc1);b(c,0xe9);b(c,3);
                b(c,0x44);b(c,0x31);b(c,0xc8);
                b(c,0x83);b(c,0xe0);b(c,group==5?1:5);fall_cc=0x85;
            }
            if(condition&1)fall_cc^=1;
            size_t fall=jcc(c,fall_cc);
            finish(c,target,n+1,VF_NEXT);
            fix(c,fall);finish(c,pc+4,n+1,VF_NEXT);break;
        } else if((w&0x7e000000)==0x34000000) {
            load(c,rd,0,wide);b(c,0x48);b(c,0x85);b(c,0xc0);
            size_t fall=jcc(c,(w&0x1000000)?0x84:0x85);
            finish(c,pc+(uint64_t)(sext((w>>5)&0x7ffff,19)*4),n+1,VF_NEXT);
            fix(c,fall);finish(c,pc+4,n+1,VF_NEXT);break;
        } else if((w&0x7c000000)==0x14000000) {
            if(w>>31)field(c,offsetof(vf_cpu,x)+30*8,pc+4);
            finish(c,pc+(uint64_t)(sext(w&0x3ffffff,26)*4),n+1,VF_NEXT);break;
        } else if((w&0xfffffc1f)==0xd61f0000 || (w&0xfffffc1f)==0xd63f0000 ||
                  (w&0xfffffc1f)==0xd65f0000) {
            if(rn==31){field32(c,offsetof(vf_cpu,instruction),w);finish(c,pc,n,VF_UNDEFINED_INSTRUCTION);break;}
            load(c,rn,0,1);b(c,0x49);b(c,0x89);b(c,0xc1); /* retain target before writing LR */
            if((w&0xfffffc1f)==0xd63f0000)field(c,offsetof(vf_cpu,x)+30*8,pc+4);
            b(c,0x4c);b(c,0x89);b(c,0x89);u32(c,offsetof(vf_cpu,pc));
            b(c,0x48);b(c,0x81);b(c,0x81);u32(c,offsetof(vf_cpu,retired));u32(c,n+1);
            b(c,0xb8);u32(c,VF_NEXT);b(c,0xc3);break;
        } else if(w==0xd503201f) { /* NOP */
        } else if(w==0xd4400000) { /* HLT #0 is the synthetic monitor exit, not an EL exception. */
            finish(c,pc+4,n+1,VF_HALT);break;
        } else {
            int boundary=known_privileged(w,current_el) ? VF_PRIVILEGE_FAULT
                                                         : system_boundary(w,current_el);
            if(boundary) {
                field32(c,offsetof(vf_cpu,instruction),w);
                finish(c,pc,n,(unsigned)boundary);break;
            }
            b(c,0xc7);b(c,0x81);u32(c,offsetof(vf_cpu,instruction));u32(c,w);
            finish(c,pc,n,VF_UNDEFINED_INSTRUCTION);break;
        }
        if(n+1==limit)finish(c,pc+4,n+1,VF_NEXT);
    }
    return c->used>c->capacity?VF_CODE_FULL:VF_NEXT;
}
int vf_translate(vf_code *c,const uint8_t *guest,size_t size,uint64_t pc,unsigned limit) {
    return translate_impl(c,0,guest,size,pc,limit);
}
int vf_translate_cpu(vf_code *c,vf_cpu *cpu,const uint8_t *guest,size_t size,
                     uint64_t pc,unsigned limit) {
    if(!cpu || !vf_cpu_state_valid(cpu)) return VF_PRIVILEGE_FAULT;
    return translate_impl(c,cpu,guest,size,pc,limit);
}
int vf_host_supported(void) {
    uint32_t a=1,bv,c,d;
    __asm__ volatile("cpuid":"+a"(a),"=b"(bv),"=c"(c),"=d"(d));
    return (c & ((1u<<19)|(1u<<20)))==((1u<<19)|(1u<<20));
}

static int pauth_slow_step(vf_cpu *cpu) {
    if(!cpu->pauth_step || cpu->current_el!=VF_EL1 || (cpu->sctlr&1))return 0;
    vf_pauth_context context={0};
    for(unsigned i=0;i<31;i++)context.x[i]=cpu->x[i];
    for(unsigned i=0;i<5;i++)for(unsigned j=0;j<2;j++)context.keys[i][j]=cpu->pauth_keys[i][j];
    context.sp=cpu->sp;context.pc=cpu->pc;context.sctlr=cpu->sctlr;
    context.tcr=cpu->tcr;context.current_el=cpu->current_el;
    if(cpu->pauth_step(&context,cpu->instruction))return 0;
    /* Commit the architectural step only after the software PAC provider
     * returns success. It receives no guest or host-memory pointer. */
    for(unsigned i=0;i<31;i++)cpu->x[i]=context.x[i];
    for(unsigned i=0;i<5;i++)for(unsigned j=0;j<2;j++)cpu->pauth_keys[i][j]=context.keys[i][j];
    cpu->sp=context.sp;cpu->pc=context.pc;cpu->sctlr=context.sctlr;cpu->tcr=context.tcr;
    cpu->retired++;vf_cpu_advance_counter(cpu,1);
    return 1;
}
int vf_run(vf_cpu *cpu,const uint8_t *guest,size_t size,uint8_t *ram,size_t ram_size,
           vf_code *code,uint64_t budget,vf_protect protect,void *opaque) {
    if(!cpu||!guest||!ram||ram_size<8||!code||!code->bytes||!protect||
       !vf_cpu_state_valid(cpu)) return cpu ? (cpu->status=VF_DATA_FAULT) : VF_DATA_FAULT;
    uint64_t start=cpu->retired;
    while(cpu->retired-start<budget) {
        uint64_t left=budget-(cpu->retired-start);unsigned count=left>32?32:(unsigned)left;
        if(protect(code->bytes,code->capacity,0,opaque))return cpu->status=VF_PROTECTION;
        int status=vf_translate_cpu(code,cpu,guest,size,cpu->pc,count);
        if(status) {
            if(status!=VF_CODE_FULL) (void)vf_cpu_commit_status(cpu,status);
            return cpu->status=status;
        }
        if(protect(code->bytes,code->capacity,1,opaque))return cpu->status=VF_PROTECTION;
        /* CPUID serializes stores before execution on x86; no I-cache invalidate needed. */
        uint32_t a=0,bv,cv,d;__asm__ volatile("cpuid":"+a"(a),"=b"(bv),"=c"(cv),"=d"(d)::"memory");
        status=((vf_entry)(void *)code->bytes)(cpu,ram,ram_size);
        cpu->compiled_blocks++;
        if((status==VF_UNDEFINED_INSTRUCTION || status==VF_SYSTEM_REGISTER_TRAP)
            && pauth_slow_step(cpu))continue;
        if(status!=VF_NEXT) {
            if(vf_cpu_commit_status(cpu,status)) return cpu->status=status;
            return cpu->status=status;
        }
    }
    return cpu->status=VF_BUDGET;
}

int vf_run_boot(vf_cpu *cpu,uint8_t *ram,size_t ram_size,uint64_t base,
                uint64_t entry,uint64_t args,uint64_t stack,vf_code *code,
                uint64_t budget,vf_protect protect,void *opaque) {
    return vf_run_boot_with_pauth(cpu,ram,ram_size,base,entry,args,stack,code,
                                  budget,protect,opaque,0);
}

int vf_run_boot_with_pauth(vf_cpu *cpu,uint8_t *ram,size_t ram_size,uint64_t base,
                uint64_t entry,uint64_t args,uint64_t stack,vf_code *code,
                uint64_t budget,vf_protect protect,void *opaque,vf_pauth_step pauth) {
    const uint64_t registers[4]={args,0,0,0};
    return vf_run_boot_with_registers(cpu,ram,ram_size,base,entry,args,stack,
                                      code,budget,protect,opaque,registers,pauth);
}

int vf_run_boot_with_registers(vf_cpu *cpu,uint8_t *ram,size_t ram_size,uint64_t base,
                uint64_t entry,uint64_t args,uint64_t stack,vf_code *code,
                uint64_t budget,vf_protect protect,void *opaque,
                const uint64_t registers[4],vf_pauth_step pauth) {
    if(!cpu || !ram || ram_size<8 || base>UINT64_MAX-ram_size ||
       (base&0x3fff) || (entry&3) || entry<base || entry-base>ram_size-4 ||
       (args&7) || args<base || args-base>=ram_size ||
       (stack&15) || stack<=base || stack-base>ram_size || !budget || !registers)
        return VF_DATA_FAULT;
    uint64_t initial[4];for(unsigned i=0;i<4;i++)initial[i]=registers[i];
    vf_cpu_reset(cpu,VF_EL1);
    cpu->pc=entry;
    for(unsigned i=0;i<4;i++)cpu->x[i]=initial[i];
    cpu->sp=stack;cpu->sp_el[VF_EL1]=stack;
    cpu->pstate|=VF_PSTATE_DAIF_MASK;
    cpu->guest_ram_base=base;
    cpu->pauth_step=pauth;
    return vf_run(cpu,ram,ram_size,ram,ram_size,code,budget,protect,opaque);
}
