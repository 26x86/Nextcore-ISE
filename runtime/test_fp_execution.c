/* SPDX-License-Identifier: BSD-4-Clause; authored live FP execution checks. */
#include "fp_execution.h"
#include "memory_boot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"check failed at line %d: %s\n",__LINE__,#x); exit(1); \
} } while (0)
static const uint32_t controls[] = {0xd53b4400,0xd53b4420,0xd51b4400,0xd51b4420};
static const uint32_t bitwise[] = {
    0x0e201c00,0x0e601c00,0x0ea01c00,0x0ee01c00,
    0x2e201c00,0x2e601c00,0x2ea01c00,0x2ee01c00
};
static const uint64_t fpen_mask = UINT64_C(0x00300000);

static void seed(vf_cpu *cpu,unsigned el) {
    vf_cpu_reset(cpu,VF_EL1);
    CHECK(vf_cpu_enable_fp_research(cpu) == 0);
    CHECK(cpu->fp_execution_profile == VF_FP_EXECUTION_PARTIAL);
    CHECK(cpu->cpacr_el1 == 0);
    if (el != VF_EL1) CHECK(vf_cpu_set_current_el(cpu,el) == 0);
    for (unsigned r = 0; r < 32; ++r) {
        cpu->x[r] = UINT64_C(0xabcdef0123456789) + r;
        cpu->fp.v[r][0] = UINT64_C(0x0123456789abcdef) * (r + 1u);
        cpu->fp.v[r][1] = UINT64_C(0xfedcba9876543210) - r;
    }
    cpu->fp.fpcr = UINT32_C(0x00800000);
    cpu->fp.fpsr = NC_FP_IOC | NC_FP_IXC;
    cpu->retired = 73;
    cpu->cntpct = 100;
    cpu->cntvct = 151;
    cpu->sp = UINT64_C(0x1122334455667780);
}

static void configure_access(vf_cpu *cpu,uint64_t value) {
    CHECK(vf_cpu_write_sysreg(cpu,VF_SYSREG_KEY_CPACR_EL1,value) == VF_SYSREG_OK);
}

static void expect_unchanged(vf_cpu *cpu,int want) {
    vf_cpu before = *cpu;
    CHECK(vf_fp_step(cpu) == want);
    CHECK(!memcmp(cpu,&before,sizeof(*cpu)));
}

static void expected_retirement(vf_cpu *cpu) {
    cpu->pc += 4;
    cpu->retired++;
    cpu->cntpct++;
    cpu->cntvct++;
}

static uint64_t truth(unsigned operation,uint64_t old,uint64_t a,uint64_t b) {
    static const uint8_t tables[] = {0x88,0x44,0xee,0xdd,0x66,0xca,0xd8,0xe4};
    uint64_t result = 0;
    for (unsigned bit = 0; bit < 64; ++bit) {
        unsigned index = (unsigned)((old >> bit & 1u) << 2 |
                                    (a >> bit & 1u) << 1 | (b >> bit & 1u));
        result |= (uint64_t)(tables[operation] >> index & 1u) << bit;
    }
    return result;
}

static void classes_and_rejections(void) {
    static const uint32_t owned[] = {
        0x1e202800,0x1e602800, /* FADD S and unimplemented FADD D. */
        0x4f00e400,0x6e205800,0x0e31b800, /* MOVI, NOT, ADDV. */
        0x3d800000,0x3dc00000,0x3ce26820, /* STR/LDR Q and register offset. */
        0xad400400,0x9c000000,0x4c407000, /* LDP Q, LDR Q literal, LD1. */
        0xd53b4400,0xd51b441f,0xd53b443f,0xd51b4420,
        0x0fffffff /* Reserved word in a broad data space. */
    };
    static const uint32_t unowned[] = {
        0,0xd503201f,0x91000400,0xf9400000,0xa9400400,0x58000000,
        0xd5181040,0xd53bd040,0xd53b4440,0xd51b4460
    };
    for (unsigned i = 0; i < sizeof(owned)/sizeof(owned[0]); ++i)
        CHECK(vf_fp_instruction_class(owned[i]));
    for (unsigned i = 0; i < sizeof(unowned)/sizeof(unowned[0]); ++i)
        CHECK(!vf_fp_instruction_class(unowned[i]));
    CHECK(vf_fp_step(NULL) == VF_IMPLEMENTATION_GAP);
    vf_cpu cpu;
    seed(&cpu,VF_EL1); configure_access(&cpu,fpen_mask);
    static const uint32_t gaps[] = {
        0x1e602800,0x4f00e400,0x6e205800,0x0e31b800,
        0x3d800000,0x3dc00000,0x3ce26820,0xad400400,0x9c000000,0x4c407000,0x0fffffff
    };
    for (unsigned i = 0; i < sizeof(gaps)/sizeof(gaps[0]); ++i) {
        cpu.instruction = gaps[i]; expect_unchanged(&cpu,VF_IMPLEMENTATION_GAP);
        /* Unknown exact legality must not invent an access exception. */
        cpu.cpacr_el1 = 0; expect_unchanged(&cpu,VF_IMPLEMENTATION_GAP);
        configure_access(&cpu,fpen_mask);
    }
    cpu.instruction = 0x1e202800;
    cpu.fp_execution_profile = 0; expect_unchanged(&cpu,VF_UNDEFINED_INSTRUCTION);
    cpu.fp_execution_profile = 2; expect_unchanged(&cpu,VF_IMPLEMENTATION_GAP);
    cpu.fp_execution_profile = VF_FP_EXECUTION_PARTIAL;
    for (unsigned malformed = 0; malformed < 2; ++malformed) {
        vf_cpu bad = cpu;
        if (malformed) bad.fp.fpsr |= 1u << 27;
        else bad.fp.fpcr |= 1u << 8;
        expect_unchanged(&bad,VF_IMPLEMENTATION_GAP);
        bad.cpacr_el1 = 0; expect_unchanged(&bad,VF_FP_ACCESS_TRAP);
    }
    for (unsigned setting = 0; setting < 3; ++setting) {
        vf_cpu bad = cpu;
        if (setting == 0) bad.cpacr_el1 |= 1;
        if (setting == 1) bad.hcr_el2 = 1;
        if (setting == 2) bad.scr_el3 = 1;
        expect_unchanged(&bad,VF_IMPLEMENTATION_GAP);
    }
    for (unsigned el = VF_EL2; el <= VF_EL3; ++el) {
        seed(&cpu,el); cpu.cpacr_el1 = fpen_mask;
        cpu.instruction = 0x1e202800; expect_unchanged(&cpu,VF_IMPLEMENTATION_GAP);
    }
}

static void direct_controls(void) {
    for (unsigned kind = 0; kind < 4; ++kind)
    for (unsigned rt = 0; rt < 32; ++rt) {
        vf_cpu cpu; seed(&cpu,VF_EL1); configure_access(&cpu,fpen_mask);
        cpu.instruction = controls[kind] | rt;
        if (kind >= 2 && rt != 31)
            cpu.x[rt] = UINT64_C(0xffffffff00000000) |
                         (kind == 2 ? UINT32_C(0x00c09f00) : UINT32_C(0x1f));
        vf_cpu want = cpu;
        if (kind < 2 && rt != 31) want.x[rt] = kind ? cpu.fp.fpsr : cpu.fp.fpcr;
        if (kind == 2) want.fp.fpcr = rt == 31 ? 0 : UINT32_C(0x00c00000);
        if (kind == 3) want.fp.fpsr = rt == 31 ? 0 : UINT32_C(0x1f);
        expected_retirement(&want);
        CHECK(vf_fp_step(&cpu) == VF_NEXT);
        CHECK(!memcmp(&cpu,&want,sizeof(cpu)));
    }
    for (unsigned mode = 0; mode < 4; ++mode)
    for (unsigned enables = 0; enables < 64; ++enables) {
        vf_cpu cpu; seed(&cpu,VF_EL1); configure_access(&cpu,fpen_mask);
        cpu.x[0] = UINT64_C(0xffffffff00000000) | mode << 22 |
                     (enables & 31u) << 8 | (enables >> 5) << 15;
        cpu.instruction = 0xd51b4400;
        CHECK(vf_fp_step(&cpu) == VF_NEXT);
        CHECK(cpu.fp.fpcr == mode << 22);
    }
    vf_cpu cpu; seed(&cpu,VF_EL1); configure_access(&cpu,fpen_mask);
    cpu.instruction = 0xd51b4400; cpu.x[0] = 1;
    expect_unchanged(&cpu,VF_IMPLEMENTATION_GAP);
    cpu.instruction = 0xd51b4420; cpu.x[0] = 1u << 27;
    expect_unchanged(&cpu,VF_IMPLEMENTATION_GAP);
}

static int protect(void *p,size_t n,int executable,void *unused) {
    (void)unused;
    return mprotect(p,n,PROT_READ | (executable ? PROT_EXEC : PROT_WRITE));
}

static int native(vf_cpu *cpu,vf_code *code,uint32_t word) {
    uint8_t ram[16] = {1,2,3},saved[16]; memcpy(saved,ram,sizeof(ram));
    int result = vf_run(cpu,(const uint8_t *)&word,4,ram,sizeof(ram),code,1,protect,0);
    CHECK(!memcmp(ram,saved,sizeof(ram)));
    return result;
}

static void native_scalar(vf_code *code) {
    for (unsigned operation = 0; operation < 2; ++operation)
    for (unsigned alias = 0; alias < 3; ++alias) {
        vf_cpu cpu; seed(&cpu,VF_EL1); configure_access(&cpu,fpen_mask);
        cpu.fp.v[4][0] = UINT32_C(0x3f800000);
        cpu.fp.v[7][0] = UINT32_C(0x40000000);
        unsigned dst = alias == 0 ? 31 : alias == 1 ? 4 : 7;
        uint32_t word = (operation ? 0x1e201800 : 0x1e202800) | 7u << 16 | 4u << 5 | dst;
        vf_cpu want = cpu;
        want.fp.v[dst][0] = operation ? UINT32_C(0x3f000000) : UINT32_C(0x40400000);
        want.fp.v[dst][1] = 0; expected_retirement(&want);
        CHECK(native(&cpu,code,word) == VF_BUDGET);
        CHECK(!memcmp(&cpu.fp,&want.fp,sizeof(cpu.fp)));
        CHECK(!memcmp(cpu.x,want.x,sizeof(cpu.x)));
        CHECK(cpu.pc == 4 && cpu.retired == want.retired);
        CHECK(cpu.cntpct == want.cntpct && cpu.cntvct == want.cntvct);
        CHECK(cpu.exception_pending == VF_EXCEPTION_NONE);
    }
    for (unsigned direction = 0; direction < 2; ++direction)
    for (unsigned zero = 0; zero < 2; ++zero) {
        vf_cpu cpu; seed(&cpu,VF_EL1); configure_access(&cpu,fpen_mask);
        cpu.x[2] = UINT64_C(0xffffffff7f800001);
        cpu.fp.v[31][0] = UINT64_C(0xffffffff7f800001);
        cpu.fp.fpsr = 0;
        unsigned dst = direction ? (zero ? 31 : 2) : 31;
        unsigned src = direction ? 31 : (zero ? 31 : 2);
        uint32_t word = (direction ? 0x1e260000 : 0x1e270000) | src << 5 | dst;
        vf_cpu want = cpu;
        if (direction && dst != 31) want.x[dst] = UINT32_C(0x7f800001);
        if (!direction) {want.fp.v[dst][0] = zero ? 0 : UINT32_C(0x7f800001);want.fp.v[dst][1] = 0;}
        CHECK(native(&cpu,code,word) == VF_BUDGET);
        CHECK(!memcmp(&cpu.fp,&want.fp,sizeof(cpu.fp)) && !memcmp(cpu.x,want.x,sizeof(cpu.x)));
        CHECK(cpu.retired == 74 && cpu.cntpct == 101 && cpu.cntvct == 152 && cpu.pc == 4);
    }
}

static void native_bitwise(vf_code *code) {
    static const unsigned aliases[][3] = {{31,0,1},{0,0,1},{1,0,1},{31,31,31},{0,1,0}};
    for (unsigned operation = 0; operation < 8; ++operation)
    for (unsigned q = 0; q < 2; ++q)
    for (unsigned alias = 0; alias < sizeof(aliases)/sizeof(aliases[0]); ++alias) {
        vf_cpu cpu; seed(&cpu,VF_EL1); configure_access(&cpu,fpen_mask);
        unsigned dst = aliases[alias][0],left = aliases[alias][1],right = aliases[alias][2];
        uint32_t word = bitwise[operation] | q << 30 | right << 16 | left << 5 | dst;
        nc_fp_bank want = cpu.fp; uint64_t gpr[32]; memcpy(gpr,cpu.x,sizeof(gpr));
        want.v[dst][0] = truth(operation,cpu.fp.v[dst][0],cpu.fp.v[left][0],cpu.fp.v[right][0]);
        want.v[dst][1] = q ? truth(operation,cpu.fp.v[dst][1],cpu.fp.v[left][1],cpu.fp.v[right][1]) : 0;
        CHECK(native(&cpu,code,word) == VF_BUDGET);
        CHECK(!memcmp(&cpu.fp,&want,sizeof(want)) && !memcmp(cpu.x,gpr,sizeof(gpr)));
        CHECK(cpu.retired == 74 && cpu.cntpct == 101 && cpu.cntvct == 152 && cpu.pc == 4);
    }
}

static void native_access(vf_code *code) {
    static const uint32_t known[] = {0xd53b4400,0x1e202800,0x4e211c00};
    for (unsigned kind = 0; kind < sizeof(known)/sizeof(known[0]); ++kind)
    for (unsigned el = 0; el < 2; ++el)
    for (unsigned fpen = 0; fpen < 4; ++fpen) {
        vf_cpu cpu; seed(&cpu,VF_EL1); configure_access(&cpu,(uint64_t)fpen << 20);
        CHECK(vf_cpu_set_current_el(&cpu,el) == 0);
        vf_cpu before = cpu;
        int trap = fpen == 0 || fpen == 2 || (fpen == 1 && el == 0);
        CHECK(native(&cpu,code,known[kind]) == (trap ? VF_FP_ACCESS_TRAP : VF_BUDGET));
        if (trap) {
            CHECK(!memcmp(&cpu.fp,&before.fp,sizeof(cpu.fp)) && !memcmp(cpu.x,before.x,sizeof(cpu.x)));
            CHECK(cpu.retired == before.retired && cpu.pc == 0);
            CHECK(cpu.cntpct == before.cntpct && cpu.cntvct == before.cntvct);
            CHECK(cpu.exception_pending == VF_EXCEPTION_FP_ACCESS);
            CHECK(cpu.exception_syndrome == UINT32_C(0x1fe00000));
            CHECK(cpu.exception_pc == 0 && cpu.exception_far == 0);
            CHECK(cpu.exception_target_el == VF_EL1 && cpu.exception_from_lower_el == (el == VF_EL0));
            cpu.vbar_el[VF_EL1] = UINT64_C(0x8000);
            CHECK(vf_cpu_take_exception(&cpu) == 0);
            CHECK(cpu.pc == UINT64_C(0x8000) + (el == VF_EL0 ? 0x400u : 0x200u));
            CHECK(cpu.current_el == VF_EL1 && cpu.retired == before.retired);
        } else {
            CHECK(cpu.pc == 4 && cpu.retired == before.retired + 1);
            CHECK(cpu.cntpct == before.cntpct + 1 && cpu.cntvct == before.cntvct + 1);
            CHECK(cpu.exception_pending == VF_EXCEPTION_NONE);
        }
    }
}

static void native_gap_and_default(vf_code *code) {
    static const uint32_t gaps[] = {
        0x1e602800,0x4f00e400,0x3dc00000,0x3ce26820,0xad400400,0x9c000000,0x4c407000
    };
    for (unsigned i = 0; i < sizeof(gaps)/sizeof(gaps[0]); ++i) {
        vf_cpu cpu; seed(&cpu,VF_EL1); configure_access(&cpu,fpen_mask);
        vf_cpu before = cpu;
        CHECK(native(&cpu,code,gaps[i]) == VF_IMPLEMENTATION_GAP);
        CHECK(!memcmp(&cpu.fp,&before.fp,sizeof(cpu.fp)) && !memcmp(cpu.x,before.x,sizeof(cpu.x)));
        CHECK(cpu.pc == 0 && cpu.retired == before.retired);
        CHECK(cpu.cntpct == before.cntpct && cpu.cntvct == before.cntvct);
        CHECK(cpu.exception_pending == VF_EXCEPTION_NONE);
    }
    vf_cpu cpu; vf_cpu_reset(&cpu,VF_EL1);
    CHECK(cpu.fp_execution_profile == VF_FP_EXECUTION_ABSENT && cpu.cpacr_el1 == 0);
    cpu.fp.v[0][0] = UINT32_C(0x3f800000); vf_cpu before = cpu;
    CHECK(native(&cpu,code,0x1e202800) == VF_UNDEFINED_INSTRUCTION);
    CHECK(!memcmp(&cpu.fp,&before.fp,sizeof(cpu.fp)) && !memcmp(cpu.x,before.x,sizeof(cpu.x)));
    CHECK(cpu.retired == 0 && cpu.cntpct == 0 && cpu.cntvct == 0 && cpu.pc == 0);
    CHECK(cpu.exception_pending == VF_EXCEPTION_UNDEFINED_INSTRUCTION);
}

static void guest_enables_access(vf_code *code) {
    vf_cpu cpu; seed(&cpu,VF_EL1); cpu.x[1] = fpen_mask;
    cpu.x[2] = UINT64_C(0xffffffff00c09f00);
    const uint32_t guest[] = {
        0xd5181041, /* MSR CPACR_EL1,X1. */
        0xd5381043, /* MRS X3,CPACR_EL1. */
        0xd51b4402, /* MSR FPCR,X2. */
        0xd53b4404, /* MRS X4,FPCR. */
        0x1e27005f  /* FMOV S31,W2. */
    };
    uint8_t ram[8] = {0};
    CHECK(vf_run(&cpu,(const uint8_t *)guest,sizeof(guest),ram,sizeof(ram),code,5,protect,0) == VF_BUDGET);
    CHECK(cpu.cpacr_el1 == fpen_mask && cpu.x[3] == fpen_mask);
    CHECK(cpu.fp.fpcr == UINT32_C(0x00c00000) && cpu.x[4] == UINT32_C(0x00c00000));
    CHECK(cpu.fp.v[31][0] == UINT32_C(0x00c09f00) && cpu.fp.v[31][1] == 0);
    CHECK(cpu.pc == 20 && cpu.retired == 78 && cpu.cntpct == 105 && cpu.cntvct == 156);
    CHECK(cpu.exception_pending == VF_EXCEPTION_NONE);

    seed(&cpu,VF_EL1); cpu.x[0] = 1;
    vf_cpu before = cpu;
    CHECK(native(&cpu,code,UINT32_C(0xd5181040)) == VF_IMPLEMENTATION_GAP);
    CHECK(cpu.pc == before.pc && cpu.retired == before.retired);
    CHECK(cpu.cntpct == before.cntpct && cpu.cntvct == before.cntvct);
    CHECK(cpu.cpacr_el1 == before.cpacr_el1 && cpu.exception_pending == VF_EXCEPTION_NONE);
    CHECK(!memcmp(&cpu.fp,&before.fp,sizeof(cpu.fp)) && !memcmp(cpu.x,before.x,sizeof(cpu.x)));
    CHECK(cpu.status == VF_IMPLEMENTATION_GAP && cpu.instruction == UINT32_C(0xd5181040));
    CHECK(cpu.compiled_blocks == before.compiled_blocks + 1);
    /* Only execution diagnostics change on a failed guest CPACR write. */
    vf_cpu observed = cpu;
    observed.status = before.status;
    observed.instruction = before.instruction;
    observed.compiled_blocks = before.compiled_blocks;
    CHECK(!memcmp(&observed,&before,sizeof(before)));
}

typedef struct {vf_cpu *cpu;unsigned fetches,data;unsigned revoke;} provider;
static int32_t fetch(void *opaque,const vf_memory_request_v1 *request,vf_memory_reply_v1 *reply) {
    provider *p = opaque;
    CHECK(request->abi_version == 1 && request->struct_size == sizeof(*request));
    *reply = (vf_memory_reply_v1){0}; reply->abi_version = 1;reply->struct_size = sizeof(*reply);
    if (request->operation != VF_MEMORY_FETCH) {p->data++;return -1;}
    CHECK(request->width == 4 && request->count == 1);
    p->fetches++;
    if (p->revoke && p->fetches == 3) configure_access(p->cpu,0);
    CHECK(request->address == 0 || request->address == 4);
    reply->value0 = request->address == 0 ? UINT32_C(0x1e270000) : UINT32_C(0x17ffffff);
    return 0;
}

static void cached_live_access(vf_code *code) {
    for (unsigned revoke = 0; revoke < 2; ++revoke) {
        vf_cpu cpu; seed(&cpu,VF_EL1); configure_access(&cpu,fpen_mask);
        cpu.x[0] = UINT64_C(0xffffffff7f800001);provider p = {&cpu,0,0,revoke};
        vf_memory_run_result_v1 result = {0};result.abi_version = 1;result.struct_size = sizeof(result);
        int status = vf_run_memory_provider(&cpu,code,8,protect,0,fetch,&p,&result);
        CHECK(status == (revoke ? VF_FP_ACCESS_TRAP : VF_BUDGET));
        CHECK(cpu.fp.v[0][0] == UINT32_C(0x7f800001) && cpu.fp.v[0][1] == 0);
        CHECK(cpu.fp.fpsr == (NC_FP_IOC | NC_FP_IXC));
        CHECK(cpu.pc == 0 && cpu.retired == 73u + (revoke ? 2u : 8u));
        CHECK(cpu.cntpct == 100u + (revoke ? 2u : 8u) && cpu.cntvct == 151u + (revoke ? 2u : 8u));
        CHECK(p.data == 0 && result.data_requests == 0 && result.completed_data_operations == 0);
        CHECK(result.fetch_requests == (revoke ? 3u : 8u));
        if (revoke) CHECK(cpu.exception_pending == VF_EXCEPTION_FP_ACCESS && cpu.exception_syndrome == UINT32_C(0x1fe00000));
        else CHECK(cpu.exception_pending == VF_EXCEPTION_NONE);
    }
}

int main(void) {
    CHECK(vf_host_supported());
    classes_and_rejections(); direct_controls();
    vf_code code = {mmap(0,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),65536,0};
    CHECK(code.bytes != MAP_FAILED);
    native_scalar(&code); native_bitwise(&code); native_access(&code);
    native_gap_and_default(&code); guest_enables_access(&code); cached_live_access(&code);
    CHECK(munmap(code.bytes,code.capacity) == 0);
    printf("{\"passed\":true,\"assertions\":%u,\"live_cpu\":true,\"live_provider_cache\":true}\n",checks);
    return 0;
}
