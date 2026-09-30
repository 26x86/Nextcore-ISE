/* SPDX-License-Identifier: BSD-4-Clause; independently authored FP state tests. */
#include "fp_guest.h"
#include "fp_scalar.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void seed(nc_fp_bank *bank) {
    for (unsigned reg = 0; reg < 32; ++reg) {
        bank->v[reg][0] = UINT64_C(0xa123456789abcdef) ^ reg;
        bank->v[reg][1] = UINT64_C(0xfedcba9876543210) ^ ((uint64_t)reg << 32);
    }
    bank->fpcr = 2u << 22;
    bank->fpsr = NC_FP_IOC | NC_FP_DZC;
}

static void reset(void) {
    nc_fp_bank bank, other, zero = {0};
    seed(&other);
    nc_fp_bank saved = other;
    memset(&bank, 0xa5, sizeof(bank));
    CHECK(nc_fp_bank_reset(&bank) == NC_FP_OK);
    CHECK(memcmp(&bank, &zero, sizeof(bank)) == 0);
    CHECK(memcmp(&other, &saved, sizeof(other)) == 0);
    CHECK(nc_fp_bank_reset(&bank) == NC_FP_OK);
    CHECK(memcmp(&bank, &zero, sizeof(bank)) == 0);
    CHECK(nc_fp_bank_reset(NULL) == NC_FP_INVALID);
}

static void views(void) {
    nc_fp_bank bank, other;
    seed(&bank);
    seed(&other);
    other.fpcr = 3u << 22;
    other.fpsr = NC_FP_STATUS_MASK;
    nc_fp_bank isolated = other;
    for (unsigned reg = 0; reg < 32; ++reg) {
        uint64_t low = UINT64_C(0x9988776655443300) | reg;
        uint64_t high = UINT64_C(0x1020304050607080) ^ reg;
        uint64_t d, q[2];
        uint32_t s;
        nc_fp_bank expected = bank;
        expected.v[reg][0] = low;
        expected.v[reg][1] = high;
        CHECK(nc_fp_bank_write_q(&bank, reg, low, high) == NC_FP_OK);
        CHECK(memcmp(&bank, &expected, sizeof(bank)) == 0);
        CHECK(nc_fp_bank_get_s(&bank, reg, &s) == NC_FP_OK && s == (uint32_t)low);
        CHECK(nc_fp_bank_get_d(&bank, reg, &d) == NC_FP_OK && d == low);
        CHECK(nc_fp_bank_get_q(&bank, reg, q) == NC_FP_OK && q[0] == low && q[1] == high);
        CHECK(memcmp(&bank, &expected, sizeof(bank)) == 0);
        low = UINT64_C(0xfedcba9801234500) | reg;
        expected.v[reg][0] = low;
        expected.v[reg][1] = 0;
        CHECK(nc_fp_bank_write_d(&bank, reg, low) == NC_FP_OK);
        CHECK(memcmp(&bank, &expected, sizeof(bank)) == 0);
        CHECK(nc_fp_bank_get_s(&bank, reg, &s) == NC_FP_OK && s == (uint32_t)low);
        CHECK(nc_fp_bank_get_q(&bank, reg, q) == NC_FP_OK && q[0] == low && q[1] == 0);
        CHECK(nc_fp_bank_write_q(&bank, reg, UINT64_MAX, UINT64_MAX) == NC_FP_OK);
        uint32_t word = UINT32_C(0x81234500) | reg;
        expected.v[reg][0] = word;
        CHECK(nc_fp_bank_write_s(&bank, reg, word) == NC_FP_OK);
        CHECK(memcmp(&bank, &expected, sizeof(bank)) == 0);
        CHECK(nc_fp_bank_get_d(&bank, reg, &d) == NC_FP_OK && d == word);
        CHECK(nc_fp_bank_get_q(&bank, reg, q) == NC_FP_OK && q[0] == word && q[1] == 0);
        CHECK(memcmp(&other, &isolated, sizeof(other)) == 0);
    }
    CHECK(nc_fp_bank_reset(&bank) == NC_FP_OK);
    CHECK(memcmp(&other, &isolated, sizeof(other)) == 0);
}

static void rejects(void) {
    nc_fp_bank bank;
    seed(&bank);
    nc_fp_bank saved = bank;
    const unsigned bad[] = {32, UINT_MAX};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        uint32_t s = UINT32_C(0xfeedabcd);
        uint64_t d = UINT64_C(0xabcddcba12344321);
        uint64_t q[2] = {UINT64_C(0x123456789abcdef0), UINT64_C(0xfedcba9876543210)};
        CHECK(nc_fp_bank_get_s(&bank, bad[i], &s) == NC_FP_INVALID && s == UINT32_C(0xfeedabcd));
        CHECK(nc_fp_bank_get_d(&bank, bad[i], &d) == NC_FP_INVALID && d == UINT64_C(0xabcddcba12344321));
        CHECK(nc_fp_bank_get_q(&bank, bad[i], q) == NC_FP_INVALID && q[0] == UINT64_C(0x123456789abcdef0) && q[1] == UINT64_C(0xfedcba9876543210));
        CHECK(nc_fp_bank_write_s(&bank, bad[i], 0) == NC_FP_INVALID);
        CHECK(nc_fp_bank_write_d(&bank, bad[i], 0) == NC_FP_INVALID);
        CHECK(nc_fp_bank_write_q(&bank, bad[i], 0, 0) == NC_FP_INVALID);
        for (unsigned op = 0; op < 2; ++op) {
            nc_fp_result (*binary)(nc_fp_bank *, unsigned, unsigned, unsigned) =
                op ? nc_fp_bank_div32 : nc_fp_bank_add32;
            CHECK(binary(&bank, bad[i], 0, 1) == NC_FP_INVALID);
            CHECK(binary(&bank, 0, bad[i], 1) == NC_FP_INVALID);
            CHECK(binary(&bank, 0, 1, bad[i]) == NC_FP_INVALID);
            CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
        }
        CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
    }
    uint32_t s = UINT32_C(0xfeedabcd);
    uint64_t d = UINT64_C(0xabcddcba12344321), q[2] = {13, 29};
    CHECK(nc_fp_bank_get_s(NULL, 0, &s) == NC_FP_INVALID && s == UINT32_C(0xfeedabcd));
    CHECK(nc_fp_bank_get_d(NULL, 0, &d) == NC_FP_INVALID && d == UINT64_C(0xabcddcba12344321));
    CHECK(nc_fp_bank_get_q(NULL, 0, q) == NC_FP_INVALID && q[0] == 13 && q[1] == 29);
    CHECK(nc_fp_bank_get_s(&bank, 0, NULL) == NC_FP_INVALID);
    CHECK(nc_fp_bank_get_d(&bank, 0, NULL) == NC_FP_INVALID);
    CHECK(nc_fp_bank_get_q(&bank, 0, NULL) == NC_FP_INVALID);
    CHECK(nc_fp_bank_write_s(NULL, 0, 0) == NC_FP_INVALID);
    CHECK(nc_fp_bank_write_d(NULL, 0, 0) == NC_FP_INVALID);
    CHECK(nc_fp_bank_write_q(NULL, 0, 0, 0) == NC_FP_INVALID);
    CHECK(nc_fp_bank_write_fpcr(NULL, 0) == NC_FP_INVALID);
    CHECK(nc_fp_bank_write_fpsr(NULL, 0) == NC_FP_INVALID);
    CHECK(nc_fp_bank_add32(NULL, 0, 1, 2) == NC_FP_INVALID);
    CHECK(nc_fp_bank_div32(NULL, 0, 1, 2) == NC_FP_INVALID);
    CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
    for (unsigned bit = 0; bit < 32; ++bit) {
        if (bit != 22 && bit != 23) {
            CHECK(nc_fp_bank_write_fpcr(&bank, bank.fpcr | (UINT32_C(1) << bit)) == NC_FP_INVALID);
            CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
        }
        if (bit >= 5) {
            CHECK(nc_fp_bank_write_fpsr(&bank, bank.fpsr | (UINT32_C(1) << bit)) == NC_FP_INVALID);
            CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
        }
    }
    CHECK(nc_fp_bank_write_fpcr(&bank, UINT32_C(0x9f00)) == NC_FP_INVALID);
    CHECK(nc_fp_bank_write_fpcr(&bank, UINT32_MAX) == NC_FP_INVALID);
    CHECK(nc_fp_bank_write_fpsr(&bank, UINT32_MAX) == NC_FP_INVALID);
    CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
    for (unsigned mode = 0; mode < 4; ++mode) {
        saved.fpcr = mode << 22;
        CHECK(nc_fp_bank_write_fpcr(&bank, saved.fpcr) == NC_FP_OK);
        CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
    }
    for (uint32_t status = 0; status <= NC_FP_STATUS_MASK; ++status) {
        saved.fpsr = status;
        CHECK(nc_fp_bank_write_fpsr(&bank, status) == NC_FP_OK);
        CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
    }
}

typedef struct {
    unsigned divide;
    uint32_t left, right, bits[4], status;
} arithmetic_vector;

static void arithmetic(void) {
    static const arithmetic_vector vectors[] = {
        {0, 0x3f800000, 0x33800000, {0x3f800000, 0x3f800001, 0x3f800000, 0x3f800000}, NC_FP_IXC},
        {0, 0x3f800001, 0x33800000, {0x3f800002, 0x3f800002, 0x3f800001, 0x3f800001}, NC_FP_IXC},
        {0, 0xbf800000, 0xb3800000, {0xbf800000, 0xbf800000, 0xbf800001, 0xbf800000}, NC_FP_IXC},
        {0, 0x3f800000, 0xbf800000, {0, 0, 0x80000000, 0}, 0},
        {0, 0x7f7fffff, 0x7f7fffff, {0x7f800000, 0x7f800000, 0x7f7fffff, 0x7f7fffff}, NC_FP_OFC | NC_FP_IXC},
        {0, 0xff7fffff, 0xff7fffff, {0xff800000, 0xff7fffff, 0xff800000, 0xff7fffff}, NC_FP_OFC | NC_FP_IXC},
        {0, 0x7f800000, 0xff800000, {0x7fc00000, 0x7fc00000, 0x7fc00000, 0x7fc00000}, NC_FP_IOC},
        {1, 0x3f800000, 0x40400000, {0x3eaaaaab, 0x3eaaaaab, 0x3eaaaaaa, 0x3eaaaaaa}, NC_FP_IXC},
        {1, 0xbf800000, 0x40400000, {0xbeaaaaab, 0xbeaaaaaa, 0xbeaaaaab, 0xbeaaaaaa}, NC_FP_IXC},
        {1, 1, 0x40000000, {0, 1, 0, 0}, NC_FP_UFC | NC_FP_IXC},
        {1, 0x80000001, 0x40000000, {0x80000000, 0x80000000, 0x80000001, 0x80000000}, NC_FP_UFC | NC_FP_IXC},
        {1, 0x00ffffff, 0x40000000, {0x00800000, 0x00800000, 0x007fffff, 0x007fffff}, NC_FP_UFC | NC_FP_IXC},
        {1, 0x7f7fffff, 0x3f000000, {0x7f800000, 0x7f800000, 0x7f7fffff, 0x7f7fffff}, NC_FP_OFC | NC_FP_IXC},
        {1, 0x3f800000, 0, {0x7f800000, 0x7f800000, 0x7f800000, 0x7f800000}, NC_FP_DZC},
        {1, 0, 0, {0x7fc00000, 0x7fc00000, 0x7fc00000, 0x7fc00000}, NC_FP_IOC},
        {1, 0x7f801234, 0x3f800000, {0x7fc01234, 0x7fc01234, 0x7fc01234, 0x7fc01234}, NC_FP_IOC}
    };
    for (unsigned i = 0; i < sizeof(vectors) / sizeof(*vectors); ++i) {
        for (unsigned mode = 0; mode < 4; ++mode) {
            nc_fp_bank bank;
            seed(&bank);
            CHECK(nc_fp_bank_write_fpcr(&bank, mode << 22) == NC_FP_OK);
            CHECK(nc_fp_bank_write_fpsr(&bank, 0) == NC_FP_OK);
            CHECK(nc_fp_bank_write_q(&bank, 2, UINT64_C(0xdeadbeef00000000) | vectors[i].left, UINT64_MAX) == NC_FP_OK);
            CHECK(nc_fp_bank_write_q(&bank, 13, UINT64_C(0x1234567800000000) | vectors[i].right, UINT64_MAX) == NC_FP_OK);
            nc_fp_bank expected = bank;
            expected.v[25][0] = vectors[i].bits[mode];
            expected.v[25][1] = 0;
            expected.fpsr = vectors[i].status;
            CHECK((vectors[i].divide ? nc_fp_bank_div32(&bank, 25, 2, 13) :
                   nc_fp_bank_add32(&bank, 25, 2, 13)) == NC_FP_OK);
            CHECK(memcmp(&bank, &expected, sizeof(bank)) == 0);
        }
    }
}

static void aliasing(void) {
    /* Left, right, equal sources, and all three operands sharing a register. */
    static const unsigned regs[][3] = {{7, 7, 19}, {19, 7, 19}, {30, 7, 7}, {7, 7, 7}};
    for (unsigned op = 0; op < 2; ++op) {
        for (unsigned i = 0; i < sizeof(regs) / sizeof(*regs); ++i) {
            nc_fp_bank bank, other;
            seed(&bank);
            seed(&other);
            nc_fp_bank isolated = other;
            bank.fpcr = 0;
            bank.fpsr = NC_FP_DZC;
            unsigned dst = regs[i][0], left = regs[i][1], right = regs[i][2];
            CHECK(nc_fp_bank_write_q(&bank, left, UINT64_C(0xdeadbeef40800000), UINT64_MAX) == NC_FP_OK);
            if (left != right)
                CHECK(nc_fp_bank_write_q(&bank, right, UINT64_C(0xcafebabe40000000), UINT64_MAX) == NC_FP_OK);
            nc_fp_bank expected = bank;
            expected.v[dst][0] = op ? (left == right ? 0x3f800000u : 0x40000000u) :
                (left == right ? 0x41000000u : 0x40c00000u);
            expected.v[dst][1] = 0;
            CHECK((op ? nc_fp_bank_div32(&bank, dst, left, right) :
                   nc_fp_bank_add32(&bank, dst, left, right)) == NC_FP_OK);
            CHECK(memcmp(&bank, &expected, sizeof(bank)) == 0);
            CHECK(memcmp(&other, &isolated, sizeof(other)) == 0);
        }
    }
}

static void cumulative(void) {
    nc_fp_bank bank;
    CHECK(nc_fp_bank_reset(&bank) == NC_FP_OK);
    CHECK(nc_fp_bank_write_fpcr(&bank, 2u << 22) == NC_FP_OK);
    static const uint32_t operands[][2] = {
        {0, 0}, {0x3f800000, 0}, {0x7f7fffff, 0x3f000000}, {1, 0x40000000}
    };
    static const uint32_t status[] = {NC_FP_IOC, NC_FP_IOC | NC_FP_DZC,
        NC_FP_IOC | NC_FP_DZC | NC_FP_OFC | NC_FP_IXC, NC_FP_STATUS_MASK};
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(nc_fp_bank_write_s(&bank, 0, operands[i][0]) == NC_FP_OK);
        CHECK(nc_fp_bank_write_s(&bank, 1, operands[i][1]) == NC_FP_OK);
        CHECK(nc_fp_bank_div32(&bank, 31, 0, 1) == NC_FP_OK);
        CHECK(bank.fpsr == status[i] && bank.fpcr == (2u << 22));
    }
    CHECK(nc_fp_bank_write_s(&bank, 0, 0x3f800000) == NC_FP_OK);
    CHECK(nc_fp_bank_write_s(&bank, 1, 0x3f800000) == NC_FP_OK);
    CHECK(nc_fp_bank_add32(&bank, 31, 0, 1) == NC_FP_OK);
    CHECK(bank.v[31][0] == 0x40000000 && bank.fpsr == NC_FP_STATUS_MASK);
    CHECK(nc_fp_bank_write_fpsr(&bank, NC_FP_DZC) == NC_FP_OK);
    CHECK(bank.fpcr == (2u << 22) && bank.fpsr == NC_FP_DZC);
    CHECK(nc_fp_bank_add32(&bank, 31, 0, 1) == NC_FP_OK);
    CHECK(bank.fpsr == NC_FP_DZC);
    CHECK(nc_fp_bank_write_fpsr(&bank, 0) == NC_FP_OK);
    CHECK(nc_fp_bank_add32(&bank, 31, 0, 1) == NC_FP_OK);
    CHECK(bank.fpsr == 0 && bank.fpcr == (2u << 22));
}

static void invalid_arithmetic_state(void) {
    for (unsigned op = 0; op < 2; ++op) {
        nc_fp_result (*binary)(nc_fp_bank *, unsigned, unsigned, unsigned) =
            op ? nc_fp_bank_div32 : nc_fp_bank_add32;
        for (unsigned bit = 0; bit < 32; ++bit) {
            nc_fp_bank bank;
            seed(&bank);
            if (bit != 22 && bit != 23) {
                bank.fpcr |= UINT32_C(1) << bit;
                nc_fp_bank saved = bank;
                CHECK(binary(&bank, 0, 1, 2) == NC_FP_INVALID);
                CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
            }
            seed(&bank);
            if (bit >= 5) {
                bank.fpsr |= UINT32_C(1) << bit;
                nc_fp_bank saved = bank;
                CHECK(binary(&bank, 0, 1, 2) == NC_FP_INVALID);
                CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
            }
        }
        nc_fp_bank bank;
        seed(&bank);
        bank.fpcr = 0x9f00;
        nc_fp_bank saved = bank;
        CHECK(binary(&bank, 0, 1, 2) == NC_FP_INVALID);
        CHECK(memcmp(&bank, &saved, sizeof(bank)) == 0);
    }
}

static void access(void) {
    static const nc_fp_result expected[2][4] = {
        {NC_FP_TRAP_EL1, NC_FP_TRAP_EL1, NC_FP_TRAP_EL1, NC_FP_OK},
        {NC_FP_TRAP_EL1, NC_FP_OK, NC_FP_TRAP_EL1, NC_FP_OK}
    };
    for (unsigned el = 0; el <= 1; ++el)
        for (unsigned fpen = 0; fpen < 4; ++fpen) {
            CHECK(nc_fp_access_check(1, el, (uint64_t)fpen << 20, 0) == expected[el][fpen]);
            CHECK(nc_fp_access_check(0, el, (uint64_t)fpen << 20, 0) == NC_FP_UNDEFINED);
            CHECK(nc_fp_access_check(1, el, (uint64_t)fpen << 20, 1) == NC_FP_UNSUPPORTED);
        }
    for (unsigned el = 0; el <= 3; ++el) {
        CHECK(nc_fp_access_check(0, el, UINT64_MAX, 1) == NC_FP_UNDEFINED);
        if (el >= 2)
            for (unsigned fpen = 0; fpen < 4; ++fpen)
                CHECK(nc_fp_access_check(1, el, (uint64_t)fpen << 20, 0) == NC_FP_UNSUPPORTED);
    }
    for (unsigned bit = 0; bit < 64; ++bit) {
        if (bit == 20 || bit == 21) continue;
        for (unsigned el = 0; el <= 1; ++el) {
            CHECK(nc_fp_access_check(1, el, UINT64_C(1) << bit, 0) == NC_FP_UNSUPPORTED);
            CHECK(nc_fp_access_check(0, el, UINT64_C(1) << bit, 0) == NC_FP_UNDEFINED);
        }
    }
    const unsigned malformed[] = {2, UINT_MAX};
    for (unsigned i = 0; i < sizeof(malformed) / sizeof(*malformed); ++i) {
        CHECK(nc_fp_access_check(malformed[i], 0, 0, 0) == NC_FP_INVALID);
        CHECK(nc_fp_access_check(0, 0, 0, malformed[i]) == NC_FP_INVALID);
        CHECK(nc_fp_access_check(1, 1, 0x300000, malformed[i]) == NC_FP_INVALID);
    }
    CHECK(nc_fp_access_check(0, 4, 0, 0) == NC_FP_INVALID);
    CHECK(nc_fp_access_check(1, 4, 0, 0) == NC_FP_INVALID);
    CHECK(nc_fp_access_check(0, UINT_MAX, UINT64_MAX, 1) == NC_FP_INVALID);
    CHECK(nc_fp_access_check(1, UINT_MAX, 0, 0) == NC_FP_INVALID);
}

int main(void) {
    reset();
    views();
    rejects();
    arithmetic();
    aliasing();
    cumulative();
    invalid_arithmetic_state();
    access();
    printf("FP bank/access: %lu assertions\n", checks);
    return 0;
}
