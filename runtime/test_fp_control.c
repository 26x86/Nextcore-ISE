/* SPDX-License-Identifier: BSD-4-Clause; authored control/access checks. */
#include "fp_control.h"
#include "jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)

static void seed(nc_fp_bank *bank, uint64_t x[31]) {
    CHECK(nc_fp_bank_reset(bank) == NC_FP_OK);
    bank->fpcr = UINT32_C(0x00800000);
    bank->fpsr = NC_FP_IOC | NC_FP_IXC;
    for (unsigned i = 0; i < 32; ++i) {
        bank->v[i][0] = UINT64_C(0x0123456789abcdef) + i;
        bank->v[i][1] = UINT64_MAX - i;
        if (i < 31) x[i] = UINT64_C(0xfedcba9876543210) + i;
    }
}
static nc_fp_result execute(nc_fp_bank *b, uint64_t x[31], uint32_t word) {
    return nc_fp_execute_control(b,x,word,1,1,UINT64_C(0x00300000),0);
}

static void register_views(void) {
    nc_fp_bank bank, expected; uint64_t x[31], saved[31];
    for (unsigned kind = 0; kind < 4; ++kind)
    for (unsigned rt = 0; rt < 32; ++rt) {
        seed(&bank,x); expected = bank; memcpy(saved,x,sizeof(x));
        static const uint32_t words[] = {0xd53b4400,0xd53b4420,0xd51b4400,0xd51b4420};
        if (kind < 2) {
            if (rt != 31) saved[rt] = kind ? bank.fpsr : bank.fpcr;
        } else {
            if (rt != 31) {
                x[rt] = saved[rt] = UINT64_C(0xffffffff00000000) |
                                    (kind == 2 ? UINT32_C(0x00c09f00) : UINT32_C(0x1f));
            }
            if (kind == 2) expected.fpcr = rt == 31 ? 0 : UINT32_C(0x00c00000);
            else expected.fpsr = rt == 31 ? 0 : UINT32_C(0x1f);
        }
        CHECK(execute(&bank,x,words[kind] | rt) == NC_FP_OK);
        CHECK(!memcmp(&bank,&expected,sizeof(bank)));
        CHECK(!memcmp(x,saved,sizeof(x)));
    }
    /* Every combination of optional enables is discarded, while rounding
     * persists and arithmetic still reports an untrapped cumulative flag. */
    for (unsigned mode = 0; mode < 4; ++mode)
    for (unsigned flags = 0; flags < 64; ++flags) {
        seed(&bank,x);
        uint32_t enables = (flags & 31u) << 8 | (flags >> 5) << 15;
        x[0] = UINT64_C(0xffffffff00000000) | mode << 22 | enables;
        CHECK(execute(&bank,x,UINT32_C(0xd51b4400)) == NC_FP_OK);
        CHECK(bank.fpcr == mode << 22);
        CHECK(execute(&bank,x,UINT32_C(0xd53b4401)) == NC_FP_OK);
        CHECK(x[1] == mode << 22);
        bank.v[2][0] = UINT32_C(0x3f800000); bank.v[3][0] = 0;
        CHECK(nc_fp_bank_div32(&bank,4,2,3) == NC_FP_OK);
        CHECK(bank.v[4][0] == UINT32_C(0x7f800000));
        CHECK(bank.fpsr == (NC_FP_IOC | NC_FP_IXC | NC_FP_DZC));
    }
    for (unsigned flags = 0; flags < 32; ++flags) {
        seed(&bank,x); x[0] = UINT64_C(0xffffffff00000000) | flags;
        CHECK(execute(&bank,x,UINT32_C(0xd51b4420)) == NC_FP_OK);
        CHECK(bank.fpsr == flags);
        CHECK(execute(&bank,x,UINT32_C(0xd53b4421)) == NC_FP_OK);
        CHECK(x[1] == flags);
    }
}

static void access_and_rejection(void) {
    static const uint32_t words[] = {0xd53b4400,0xd53b4420,0xd51b4400,0xd51b4420};
    for (unsigned k = 0; k < 4; ++k)
    for (unsigned present = 0; present < 2; ++present)
    for (unsigned el = 0; el < 4; ++el)
    for (unsigned fpen = 0; fpen < 4; ++fpen)
    for (unsigned higher = 0; higher < 2; ++higher) {
        nc_fp_bank bank,saved; uint64_t x[31],before[31]; seed(&bank,x);
        x[0] = 0; saved = bank; memcpy(before,x,sizeof(x));
        nc_fp_result want = !present ? NC_FP_UNDEFINED :
            el > 1 || higher ? NC_FP_UNSUPPORTED :
            fpen == 0 || fpen == 2 || (fpen == 1 && el == 0) ? NC_FP_TRAP_EL1 : NC_FP_OK;
        CHECK(nc_fp_execute_control(&bank,x,words[k],present,el,
                                    (uint64_t)fpen << 20,higher) == want);
        if (want != NC_FP_OK) CHECK(!memcmp(&bank,&saved,sizeof(bank)) && !memcmp(x,before,sizeof(x)));
    }
    nc_fp_bank bank,saved; uint64_t x[31],before[31]; seed(&bank,x);
    x[0] = 0; saved = bank; memcpy(before,x,sizeof(x));
    for (unsigned k = 0; k < 4; ++k) {
        CHECK(nc_fp_execute_control(NULL,x,words[k],1,1,0x300000,0) == NC_FP_INVALID);
        CHECK(nc_fp_execute_control(&bank,NULL,words[k],1,1,0x300000,0) == NC_FP_INVALID);
        CHECK(nc_fp_execute_control(&bank,x,words[k],2,1,0x300000,0) == NC_FP_INVALID);
        CHECK(nc_fp_execute_control(&bank,x,words[k],1,4,0x300000,0) == NC_FP_INVALID);
        CHECK(nc_fp_execute_control(&bank,x,words[k],1,1,0x300000,2) == NC_FP_INVALID);
        CHECK(nc_fp_execute_control(&bank,x,words[k],1,1,0x300001,0) == NC_FP_UNSUPPORTED);
        CHECK(nc_fp_execute_control(&bank,x,words[k],0,3,UINT64_MAX,1) == NC_FP_UNDEFINED);
        for (unsigned bit = 5; bit < 32; ++bit) {
            uint32_t changed = words[k] ^ (UINT32_C(1) << bit);
            unsigned supported = 0;
            for (unsigned j = 0; j < 4; ++j) supported |= changed == words[j];
            if (supported) continue;
            CHECK(execute(&bank,x,changed) == NC_FP_UNSUPPORTED);
            CHECK(!memcmp(&bank,&saved,sizeof(bank)) && !memcmp(x,before,sizeof(x)));
        }
        for (unsigned bad = 0; bad < 2; ++bad) {
            bank = saved;
            if (bad) bank.fpsr |= 1u << 27; else bank.fpcr |= 1u << 8;
            nc_fp_bank malformed = bank;
            CHECK(execute(&bank,x,words[k]) == NC_FP_INVALID);
            CHECK(!memcmp(&bank,&malformed,sizeof(bank)) && !memcmp(x,before,sizeof(x)));
            /* Architectural access classification precedes bank validation. */
            CHECK(nc_fp_execute_control(&bank,x,words[k],0,1,0x300000,0) == NC_FP_UNDEFINED);
            CHECK(nc_fp_execute_control(&bank,x,words[k],1,1,0,0) == NC_FP_TRAP_EL1);
        }
        bank = saved;
    }
    for (unsigned status = 0; status < 2; ++status)
    for (unsigned bit = 0; bit < 32; ++bit) {
        uint32_t represented = status ? UINT32_C(0x1f) : UINT32_C(0x00c09f00);
        if (represented & (UINT32_C(1) << bit)) continue;
        seed(&bank,x); x[0] = UINT32_C(1) << bit; saved = bank; memcpy(before,x,sizeof(x));
        CHECK(execute(&bank,x,status ? UINT32_C(0xd51b4420) : UINT32_C(0xd51b4400)) == NC_FP_UNSUPPORTED);
        CHECK(!memcmp(&bank,&saved,sizeof(bank)) && !memcmp(x,before,sizeof(x)));
    }
}

static int perms(void *p,size_t n,int executable,void *unused) {
    (void)unused; return mprotect(p,n,PROT_READ | (executable ? PROT_EXEC : PROT_WRITE));
}
static void guest_gate(void) {
    vf_code code = {mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};
    CHECK(code.bytes != MAP_FAILED);
    const uint32_t words[] = {0xd53b4400,0xd53b4420,0xd51b4400,0xd51b4420};
    for (unsigned i = 0; i < 4; ++i) for (unsigned el = 0; el < 4; ++el) {
        vf_cpu cpu; vf_cpu_reset(&cpu,el); seed(&cpu.fp,cpu.x);
        cpu.sp = 0xabc; cpu.retired = 123; cpu.hcr_el2 = cpu.scr_el3 = UINT64_MAX;
        nc_fp_bank bank = cpu.fp; uint64_t gpr[32]; memcpy(gpr,cpu.x,sizeof(gpr));
        uint64_t pstate = cpu.pstate;
        uint8_t ram[8] = {1,2,3},before[8]; memcpy(before,ram,sizeof(ram));
        uint32_t word = words[i];
        CHECK(vf_run(&cpu,(const uint8_t *)&word,4,ram,sizeof(ram),&code,1,perms,0) == VF_UNDEFINED_INSTRUCTION);
        CHECK(!memcmp(&cpu.fp,&bank,sizeof(bank)) && !memcmp(cpu.x,gpr,sizeof(gpr)));
        CHECK(!memcmp(ram,before,sizeof(ram)) && cpu.sp == 0xabc && cpu.pstate == pstate);
        CHECK(cpu.retired == 123 && cpu.pc == 0 && cpu.instruction == word);
        CHECK(cpu.exception_syndrome >> 26 == VF_ESR_EC_UNKNOWN);
    }
    CHECK(VF_PFR0_SCALAR_PROFILE == UINT64_C(0x00ff0011));
    CHECK(munmap(code.bytes,4096) == 0);
}
int main(void) {
    register_views(); access_and_rejection(); guest_gate();
    printf("{\"passed\":true,\"assertions\":%u}\n",checks);
}
