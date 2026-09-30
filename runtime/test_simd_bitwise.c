/* SPDX-License-Identifier: BSD-4-Clause; authored vector contract checks. */
#include "simd_bitwise.h"
#include "jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static unsigned checks;
static const uint32_t operations[] = {
    0x0e201c00,0x0e601c00,0x0ea01c00,0x0ee01c00,
    0x2e201c00,0x2e601c00,0x2ea01c00,0x2ee01c00
};
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)

static void seed(nc_fp_bank *bank) {
    CHECK(nc_fp_bank_reset(bank) == NC_FP_OK);
    bank->fpcr = UINT32_C(0x00800000);
    bank->fpsr = NC_FP_IOC | NC_FP_IXC;
    for (unsigned i = 0; i < 32; ++i) {
        bank->v[i][0] = UINT64_C(0x0123456789abcdef) * (i + 1u);
        bank->v[i][1] = UINT64_C(0xfedcba9876543210) - i;
    }
}
static nc_fp_result execute(nc_fp_bank *bank, uint32_t word) {
    return nc_simd_execute_bitwise(bank,word,1,1,UINT64_C(0x00300000),0);
}

/* Truth tables index each bit as (old destination, left, right). */
static uint64_t oracle(unsigned op, uint64_t dst, uint64_t left, uint64_t right) {
    static const uint8_t tables[] = {0x88,0x44,0xee,0xdd,0x66,0xca,0xd8,0xe4};
    uint64_t result = 0;
    for (unsigned bit = 0; bit < 64; ++bit) {
        unsigned index = (unsigned)(((dst >> bit) & 1u) << 2 |
                                     ((left >> bit) & 1u) << 1 |
                                     ((right >> bit) & 1u));
        if ((tables[op] >> index) & 1u)
            result |= UINT64_C(1) << bit;
    }
    return result;
}
static void registers(void) {
    nc_fp_bank bank, expected;
    for (unsigned op = 0; op < 8; ++op)
    for (unsigned q = 0; q < 2; ++q)
    for (unsigned dst = 0; dst < 32; ++dst)
    for (unsigned left = 0; left < 32; ++left)
    for (unsigned right = 0; right < 32; ++right) {
        seed(&bank); expected = bank;
        expected.v[dst][0] = oracle(op,bank.v[dst][0],bank.v[left][0],bank.v[right][0]);
        expected.v[dst][1] = q ? oracle(op,bank.v[dst][1],bank.v[left][1],bank.v[right][1]) : 0;
        uint32_t word = operations[op] | q << 30 |
                        right << 16 | left << 5 | dst;
        CHECK(execute(&bank,word) == NC_FP_OK);
        CHECK(!memcmp(&bank,&expected,sizeof(bank)));
    }
    static const uint64_t patterns[] = {
        0,UINT64_MAX,UINT64_C(0xaaaaaaaaaaaaaaaa),UINT64_C(0x5555555555555555),
        UINT64_C(0x8000000000000001),UINT64_C(0x7ff0000000000001)
    };
    for (unsigned op = 0; op < 8; ++op)
    for (unsigned q = 0; q < 2; ++q)
    for (unsigned d = 0; d < sizeof(patterns)/sizeof(patterns[0]); ++d)
    for (unsigned a = 0; a < sizeof(patterns)/sizeof(patterns[0]); ++a)
    for (unsigned b = 0; b < sizeof(patterns)/sizeof(patterns[0]); ++b) {
        seed(&bank); bank.v[31][0] = bank.v[31][1] = patterns[d];
        bank.v[0][0] = patterns[a]; bank.v[0][1] = patterns[b];
        bank.v[1][0] = patterns[b]; bank.v[1][1] = patterns[a]; expected = bank;
        expected.v[31][0] = oracle(op,patterns[d],patterns[a],patterns[b]);
        expected.v[31][1] = q ? oracle(op,patterns[d],patterns[b],patterns[a]) : 0;
        CHECK(execute(&bank,operations[op] | q << 30 | 1u << 16 | 31u) == NC_FP_OK);
        CHECK(!memcmp(&bank,&expected,sizeof(bank)));
    }
}

static void access_case(unsigned op, unsigned q) {
    const uint32_t word = operations[op] | q << 30 | 1u << 16 | 1u << 5 | 1u;
    for (unsigned present = 0; present < 2; ++present)
    for (unsigned el = 0; el < 4; ++el)
    for (unsigned fpen = 0; fpen < 4; ++fpen)
    for (unsigned higher = 0; higher < 2; ++higher) {
        nc_fp_bank bank, expected; seed(&bank); expected = bank;
        nc_fp_result want = !present ? NC_FP_UNDEFINED :
            el > 1 || higher ? NC_FP_UNSUPPORTED :
            fpen == 0 || fpen == 2 || (fpen == 1 && el == 0) ? NC_FP_TRAP_EL1 : NC_FP_OK;
        if (want == NC_FP_OK) {
            expected.v[1][0] = oracle(op,bank.v[1][0],bank.v[1][0],bank.v[1][0]);
            expected.v[1][1] = q ? oracle(op,bank.v[1][1],bank.v[1][1],bank.v[1][1]) : 0;
        }
        CHECK(nc_simd_execute_bitwise(&bank,word,present,el,
                                  (uint64_t)fpen << 20,higher) == want);
        CHECK(!memcmp(&bank,&expected,sizeof(bank)));
    }
    nc_fp_bank bank, saved; seed(&bank); saved = bank;
    CHECK(execute(NULL,word) == NC_FP_INVALID);
    for (unsigned bit = 0; bit < 32; ++bit)
        if (UINT32_C(0x9f20fc00) & (UINT32_C(1) << bit)) {
            uint32_t neighbor = word ^ (UINT32_C(1) << bit);
            CHECK(execute(&bank,neighbor) == NC_FP_UNSUPPORTED);
            CHECK(!memcmp(&bank,&saved,sizeof(bank)));
        }
    static const uint32_t unsupported[] = {
        0x6e205800, /* NOT vector */
        0x2f0797c0, /* BIC immediate */
        0x4f00e400, /* MOVI vector */
        0xd503201f,0,UINT32_MAX
    };
    for (unsigned i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); ++i) {
        CHECK(execute(&bank,unsupported[i]) == NC_FP_UNSUPPORTED);
        CHECK(nc_simd_execute_bitwise(&bank,unsupported[i],0,3,UINT64_MAX,1) == NC_FP_UNSUPPORTED);
        CHECK(!memcmp(&bank,&saved,sizeof(bank)));
    }
    CHECK(nc_simd_execute_bitwise(&bank,word,2,1,0x300000,0) == NC_FP_INVALID);
    CHECK(nc_simd_execute_bitwise(&bank,word,1,4,0x300000,0) == NC_FP_INVALID);
    CHECK(nc_simd_execute_bitwise(&bank,word,1,1,0x300000,2) == NC_FP_INVALID);
    CHECK(nc_simd_execute_bitwise(&bank,word,1,1,0x300001,0) == NC_FP_UNSUPPORTED);
    CHECK(!memcmp(&bank,&saved,sizeof(bank)));
    for (unsigned field = 0; field < 2; ++field)
    for (unsigned bit = 0; bit < 32; ++bit) {
        uint32_t represented = field ? UINT32_C(31) : UINT32_C(0x00c00000);
        if (represented & (UINT32_C(1) << bit)) continue;
        bank = saved;
        if (field) bank.fpsr |= UINT32_C(1) << bit;
        else bank.fpcr |= UINT32_C(1) << bit;
        nc_fp_bank malformed = bank;
        CHECK(execute(&bank,word) == NC_FP_INVALID);
        CHECK(nc_simd_execute_bitwise(&bank,word,0,3,UINT64_MAX,1) == NC_FP_UNDEFINED);
        CHECK(nc_simd_execute_bitwise(&bank,word,1,0,0,0) == NC_FP_TRAP_EL1);
        CHECK(!memcmp(&bank,&malformed,sizeof(bank)));
    }
    for (unsigned mode = 0; mode < 4; ++mode)
    for (unsigned flags = 0; flags < 32; ++flags) {
        seed(&bank); bank.fpcr = mode << 22; bank.fpsr = flags;
        CHECK(execute(&bank,word) == NC_FP_OK);
        CHECK(bank.fpcr == mode << 22 && bank.fpsr == flags);
    }
    seed(&bank); saved = bank;
    nc_fp_result strict = nc_simd_execute_eor(&bank,word,1,1,0x300000,0);
    if (op == 4) {
        CHECK(strict == NC_FP_OK);
        CHECK(execute(&saved,word) == NC_FP_OK);
    } else CHECK(strict == NC_FP_UNSUPPORTED);
    CHECK(!memcmp(&bank,&saved,sizeof(bank)));
    CHECK(nc_simd_execute_eor(NULL,word,0,3,UINT64_MAX,1) == NC_FP_INVALID);
}
static void access_and_rejection(void) {
    for (unsigned op = 0; op < 8; ++op)
    for (unsigned q = 0; q < 2; ++q) access_case(op,q);
}

static int perms(void *p, size_t n, int executable, void *unused) {
    (void)unused;
    return mprotect(p,n,PROT_READ | (executable ? PROT_EXEC : PROT_WRITE));
}
static void admission(void) {
    vf_code code = {mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};
    CHECK(code.bytes != MAP_FAILED);
    for (unsigned op = 0; op < 8; ++op)
    for (unsigned q = 0; q < 2; ++q) for (unsigned el = 0; el < 4; ++el) {
        vf_cpu cpu; vf_cpu_reset(&cpu,el); seed(&cpu.fp);
        cpu.sp = 0xabc; cpu.retired = 123;
        nc_fp_bank bank = cpu.fp; uint64_t gpr[32]; memcpy(gpr,cpu.x,sizeof(gpr));
        uint64_t pstate = cpu.pstate;
        uint8_t ram[8] = {1,2,3}, before[8]; memcpy(before,ram,sizeof(ram));
        uint32_t word = operations[op] | 1u << 16 | 1u << 5 | 1u | q << 30;
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
    registers(); access_and_rejection(); admission();
    printf("{\"passed\":true,\"assertions\":%u}\n",checks);
}
