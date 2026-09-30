/* SPDX-License-Identifier: BSD-4-Clause; authored instruction contract checks. */
#include "fp_instruction.h"
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
    for (unsigned r = 0; r < 32; ++r) {
        bank->v[r][0] = UINT64_C(0x1234567800000000) | r;
        bank->v[r][1] = UINT64_MAX - r;
        if (r < 31) x[r] = UINT64_C(0xfedcba9800000000) | (UINT32_C(0x7f800001) + r);
    }
    bank->fpsr = NC_FP_IOC;
}

static void registers(void) {
    nc_fp_bank bank, expected;
    uint64_t x[31], saved[31];
    for (unsigned operation = 0; operation < 2; ++operation)
    for (unsigned dst = 0; dst < 32; ++dst)
    for (unsigned left = 0; left < 32; ++left)
    for (unsigned right = 0; right < 32; ++right) {
        seed(&bank, x);
        /* Source equality supplies independently known exact outcomes. */
        bank.v[left][0] = bank.v[right][0] = UINT32_C(0x40000000); /* 2.0 */
        expected = bank;
        expected.v[dst][0] = operation ? UINT32_C(0x3f800000) : UINT32_C(0x40800000);
        expected.v[dst][1] = 0;
        memcpy(saved, x, sizeof(x));
        uint32_t word = (operation ? UINT32_C(0x1e201800) : UINT32_C(0x1e202800)) |
                        right << 16 | left << 5 | dst;
        CHECK(nc_fp_execute32(&bank, x, word) == NC_FP_OK);
        CHECK(!memcmp(&bank, &expected, sizeof(bank)));
        CHECK(!memcmp(x, saved, sizeof(x)));
    }
    for (unsigned direction = 0; direction < 2; ++direction)
    for (unsigned dst = 0; dst < 32; ++dst)
    for (unsigned src = 0; src < 32; ++src) {
        seed(&bank, x);
        expected = bank;
        memcpy(saved, x, sizeof(x));
        if (direction) {
            if (dst != 31) saved[dst] = (uint32_t)bank.v[src][0];
        } else {
            expected.v[dst][0] = src == 31 ? 0 : (uint32_t)x[src];
            expected.v[dst][1] = 0;
        }
        uint32_t word = (direction ? UINT32_C(0x1e260000) : UINT32_C(0x1e270000)) |
                        src << 5 | dst;
        CHECK(nc_fp_execute32(&bank, x, word) == NC_FP_OK);
        CHECK(!memcmp(&bank, &expected, sizeof(bank)));
        CHECK(!memcmp(x, saved, sizeof(x)));
    }
}

static void numerical(void) {
    /* Public binary32 encodings and integer-derived expected outputs. The
     * underlying arithmetic oracle was checked separately, not repeated here. */
    static const struct { uint32_t a,b,add,div,flags; } vectors[] = {
        {0x3f800000,0x40000000,0x40400000,0x3f000000,0},
        {0x00000000,0x00000000,0x00000000,0x7fc00000,NC_FP_IOC},
        {0x3f800000,0x00000000,0x3f800000,0x7f800000,NC_FP_DZC},
        {0x7f800001,0x3f800000,0x7fc00001,0x7fc00001,NC_FP_IOC},
    };
    for (unsigned i = 0; i < sizeof(vectors)/sizeof(vectors[0]); ++i)
    for (unsigned op = 0; op < 2; ++op)
    for (unsigned alias = 0; alias < 3; ++alias) {
        nc_fp_bank bank; uint64_t x[31]; seed(&bank, x);
        bank.v[4][0] = vectors[i].a; bank.v[7][0] = vectors[i].b;
        bank.fpsr = NC_FP_IXC;
        unsigned dst = alias == 0 ? 9 : alias == 1 ? 4 : 7;
        uint32_t word = (op ? UINT32_C(0x1e201800) : UINT32_C(0x1e202800)) |
                        7u << 16 | 4u << 5 | dst;
        CHECK(nc_fp_execute32(&bank, x, word) == NC_FP_OK);
        CHECK(bank.v[dst][0] == (op ? vectors[i].div : vectors[i].add));
        CHECK(bank.v[dst][1] == 0);
        CHECK(bank.fpsr == (NC_FP_IXC | (op || i == 3 ? vectors[i].flags : 0)));
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        nc_fp_bank bank; uint64_t x[31]; seed(&bank, x);
        bank.fpcr = mode << 22;
        bank.v[0][0] = 0x3f800000; bank.v[1][0] = 0x33800000; /* half ULP */
        CHECK(nc_fp_execute32(&bank, x, UINT32_C(0x1e212800)) == NC_FP_OK);
        CHECK(bank.v[0][0] == (mode == 1 ? UINT32_C(0x3f800001) : UINT32_C(0x3f800000)));
        CHECK(bank.fpcr == mode << 22 && bank.fpsr == (NC_FP_IOC | NC_FP_IXC));
    }
}

static void rejections(void) {
    nc_fp_bank bank, saved_bank; uint64_t x[31], saved[31]; seed(&bank, x);
    saved_bank = bank; memcpy(saved, x, sizeof(x));
    static const uint32_t good[] = {0x1e202800,0x1e201800,0x1e270000,0x1e260000};
    for (unsigned g = 0; g < 4; ++g) {
        CHECK(nc_fp_execute32(NULL, x, good[g]) == NC_FP_INVALID);
        CHECK(nc_fp_execute32(&bank, NULL, good[g]) == NC_FP_INVALID);
        uint32_t operands = g < 2 ? UINT32_C(0x001f03ff) : UINT32_C(0x000003ff);
        for (unsigned bit = 0; bit < 32; ++bit) if (!(operands & (UINT32_C(1) << bit))) {
            uint32_t changed = good[g] ^ (UINT32_C(1) << bit);
            /* Some fixed-bit neighbors select another supported operation.
             * Its semantics are covered above; reject every other neighbor. */
            unsigned supported = 0;
            for (unsigned j = 0; j < 4; ++j) supported |= changed == good[j];
            if (supported) continue;
            CHECK(nc_fp_execute32(&bank, x, changed) == NC_FP_UNSUPPORTED);
            CHECK(!memcmp(&bank, &saved_bank, sizeof(bank)) && !memcmp(x, saved, sizeof(x)));
        }
        for (unsigned malformed = 0; malformed < 2; ++malformed) {
            bank = saved_bank;
            if (malformed) bank.fpsr |= 1u << 27;
            else bank.fpcr |= 1u << 8;
            nc_fp_bank before = bank;
            CHECK(nc_fp_execute32(&bank, x, good[g]) == NC_FP_INVALID);
            CHECK(!memcmp(&bank, &before, sizeof(bank)) && !memcmp(x, saved, sizeof(x)));
        }
        bank = saved_bank;
    }
    /* Raw moves never quiet a signaling NaN or accrue IOC. */
    bank.fpsr = 0; x[0] = UINT32_C(0x7f800001);
    CHECK(nc_fp_execute32(&bank, x, UINT32_C(0x1e27001f)) == NC_FP_OK);
    CHECK(bank.v[31][0] == UINT32_C(0x7f800001) && bank.fpsr == 0);
    CHECK(nc_fp_execute32(&bank, x, UINT32_C(0x1e2603e0)) == NC_FP_OK);
    CHECK(x[0] == UINT32_C(0x7f800001) && bank.fpsr == 0);
}

static int perms(void *p, size_t n, int executable, void *unused) {
    (void)unused;
    return mprotect(p, n, PROT_READ | (executable ? PROT_EXEC : PROT_WRITE));
}
static void admission(void) {
    vf_code code = {mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),4096,0};
    CHECK(code.bytes != MAP_FAILED);
    const uint32_t words[] = {0x1e202800,0x1e201800,0x1e270000,0x1e260000};
    for (unsigned i = 0; i < 4; ++i) for (unsigned el = 0; el < 4; ++el) {
        vf_cpu cpu; vf_cpu_reset(&cpu,el); seed(&cpu.fp,cpu.x);
        cpu.sp = 0xabc; cpu.retired = 123; cpu.hcr_el2 = cpu.scr_el3 = UINT64_MAX;
        nc_fp_bank bank = cpu.fp; uint64_t gpr[32]; memcpy(gpr,cpu.x,sizeof(gpr));
        uint64_t pstate = cpu.pstate;
        uint8_t ram[8] = {1,2,3}, before[8]; memcpy(before,ram,sizeof(ram));
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
    registers(); numerical(); rejections(); admission();
    printf("{\"passed\":true,\"assertions\":%u}\n",checks);
}
