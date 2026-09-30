/* SPDX-License-Identifier: BSD-4-Clause; authored raw-bit transfer checks. */
#define _GNU_SOURCE
#include "simd_transfer.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    exit(1); \
} } while (0)
#define X_BYTES (31u * sizeof(uint64_t))

/* Distinct forms are specified independently of the service's decoder. */
static const uint32_t bases[4] = {
    UINT32_C(0x9e670000), UINT32_C(0x9e660000),
    UINT32_C(0x9eaf0000), UINT32_C(0x9eae0000)
};

static void seed(nc_fp_bank *bank, uint64_t x[31]) {
    /* Initialize padding as well as every architectural field. */
    memset(bank, 0, sizeof(*bank));
    memset(x, 0, X_BYTES);
    bank->fpcr = UINT32_C(0x00800000);
    bank->fpsr = NC_FP_IOC | NC_FP_IXC;
    for (unsigned reg = 0; reg < 32; ++reg) {
        bank->v[reg][0] = UINT64_C(0x0123456789abcdef) * (reg + 1u);
        bank->v[reg][1] = UINT64_C(0xfedcba9876543210) - reg;
        if (reg < 31)
            x[reg] = UINT64_C(0x8877665544332211) ^
                     (UINT64_C(0x0102040810204080) * (reg + 1u));
    }
}

/* Byte-copy oracle has no instruction decoder, FP helpers or access calls.
 * It copies only the specified destination bytes from an immutable snapshot. */
static void expected_transfer(nc_fp_bank *bank, uint64_t x[31],
                              const nc_fp_bank *before, const uint64_t old_x[31],
                              unsigned form, unsigned dst, unsigned src) {
    uint64_t zero = 0;
    if (form == 0) {
        memcpy(&bank->v[dst][0], src == 31 ? &zero : &old_x[src], sizeof(zero));
        memset(&bank->v[dst][1], 0, sizeof(zero));
    } else if (form == 1) {
        if (dst < 31) memcpy(&x[dst], &before->v[src][0], sizeof(zero));
    } else if (form == 2) {
        memcpy(&bank->v[dst][1], src == 31 ? &zero : &old_x[src], sizeof(zero));
    } else {
        if (dst < 31) memcpy(&x[dst], &before->v[src][1], sizeof(zero));
    }
}

static void verify(nc_fp_bank *bank, uint64_t x[31], uint32_t word,
                    unsigned form, unsigned dst, unsigned src,
                    unsigned present, unsigned el, uint64_t cpacr,
                    unsigned higher, nc_fp_result want) {
    nc_fp_bank before, expected;
    uint64_t old_x[31], expected_x[31];
    memcpy(&before, bank, sizeof(before));
    memcpy(&expected, bank, sizeof(expected));
    memcpy(old_x, x, X_BYTES);
    memcpy(expected_x, x, X_BYTES);
    if (want == NC_FP_OK)
        expected_transfer(&expected, expected_x, &before, old_x, form, dst, src);
    CHECK(nc_simd_transfer_execute(bank, x, word, present, el, cpacr, higher) == want);
    CHECK(!memcmp(bank, &expected, sizeof(expected)));
    CHECK(!memcmp(x, expected_x, X_BYTES));
}

static void valid(nc_fp_bank *bank, uint64_t x[31], unsigned form,
                   unsigned dst, unsigned src) {
    verify(bank, x, bases[form] | src << 5 | dst, form, dst, src,
           1, 1, UINT64_C(0x00300000), 0, NC_FP_OK);
}

static void selectors(uint64_t x[31]) {
    nc_fp_bank bank;
    for (unsigned form = 0; form < 4; ++form)
    for (unsigned dst = 0; dst < 32; ++dst)
    for (unsigned src = 0; src < 32; ++src) {
        seed(&bank, x);
        valid(&bank, x, form, dst, src);
    }
}

static void patterns(uint64_t x[31]) {
    static const uint64_t values[] = {
        0, UINT64_MAX, UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0x5555555555555555),
        UINT64_C(0x8000000000000000), UINT64_C(0x8000000000000001),
        UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000),
        UINT64_C(0x7ff0000000000001), UINT64_C(0xfff0000000000001),
        UINT64_C(0x7ff8123456789abc), UINT64_C(0xfff8123456789abc)
    };
    static const unsigned regs[] = {0, 7, 30, 31};
    nc_fp_bank bank;
    for (unsigned form = 0; form < 4; ++form)
    for (unsigned pattern = 0; pattern < 140; ++pattern)
    for (unsigned d = 0; d < 4; ++d)
    for (unsigned n = 0; n < 4; ++n) {
        uint64_t value = pattern < 12 ? values[pattern] :
            pattern < 76 ? UINT64_C(1) << (pattern - 12) :
                           ~(UINT64_C(1) << (pattern - 76));
        unsigned dst = regs[d], src = regs[n];
        seed(&bank, x);
        if (form == 0 || form == 2) {
            if (src < 31) x[src] = value;
        } else {
            bank.v[src][form == 1 ? 0 : 1] = value;
        }
        valid(&bank, x, form, dst, src);
    }
}

static void controls_and_access(uint64_t x[31]) {
    /* Rows are EL0 and EL1; columns are architectural FPEN values 0..3. */
    static const nc_fp_result access[2][4] = {
        {NC_FP_TRAP_EL1, NC_FP_TRAP_EL1, NC_FP_TRAP_EL1, NC_FP_OK},
        {NC_FP_TRAP_EL1, NC_FP_OK, NC_FP_TRAP_EL1, NC_FP_OK}
    };
    nc_fp_bank bank;
    for (unsigned form = 0; form < 4; ++form)
    for (unsigned present = 0; present < 2; ++present)
    for (unsigned el = 0; el < 4; ++el)
    for (unsigned fpen = 0; fpen < 4; ++fpen)
    for (unsigned higher = 0; higher < 2; ++higher) {
        seed(&bank, x);
        nc_fp_result want = !present ? NC_FP_UNDEFINED :
            el > 1 || higher ? NC_FP_UNSUPPORTED : access[el][fpen];
        verify(&bank, x, bases[form] | 29u << 5 | 30u, form, 30, 29,
               present, el, (uint64_t)fpen << 20, higher, want);
    }
    for (unsigned form = 0; form < 4; ++form)
    for (unsigned mode = 0; mode < 4; ++mode)
    for (unsigned flags = 0; flags < 32; ++flags) {
        seed(&bank, x);
        bank.fpcr = mode << 22;
        bank.fpsr = flags;
        valid(&bank, x, form, 31, 31);
        valid(&bank, x, form, 3, 7);
    }
}

static void rejection_priority(uint64_t x[31]) {
    static const unsigned invalid[][3] = {
        {2,1,0}, {UINT_MAX,1,0}, {1,4,0}, {0,4,0},
        {1,UINT_MAX,0}, {1,1,2}, {0,1,2}, {1,1,UINT_MAX}
    };
    nc_fp_bank bank;
    for (unsigned form = 0; form < 4; ++form) {
        uint32_t word = bases[form] | 31u << 5 | 31u;
        seed(&bank, x);
        nc_fp_bank saved; uint64_t old_x[31];
        memcpy(&saved, &bank, sizeof(saved)); memcpy(old_x, x, X_BYTES);
        CHECK(nc_simd_transfer_execute(NULL, x, 0, 2, 4, UINT64_MAX, 2) == NC_FP_INVALID);
        CHECK(nc_simd_transfer_execute(&bank, NULL, 0, 2, 4, UINT64_MAX, 2) == NC_FP_INVALID);
        CHECK(nc_simd_transfer_execute(NULL, x, word, 1, 1, 0x300000, 0) == NC_FP_INVALID);
        CHECK(nc_simd_transfer_execute(&bank, NULL, word, 1, 1, 0x300000, 0) == NC_FP_INVALID);
        CHECK(!memcmp(&bank, &saved, sizeof(saved)) && !memcmp(x, old_x, X_BYTES));
        for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
            verify(&bank, x, word, form, 31, 31, invalid[i][0], invalid[i][1],
                   0x300000, invalid[i][2], NC_FP_INVALID);
        for (unsigned bit = 0; bit < 64; ++bit) {
            if (bit == 20 || bit == 21) continue;
            uint64_t cpacr = UINT64_C(0x00300000) | UINT64_C(1) << bit;
            verify(&bank, x, word, form, 31, 31, 1, 1, cpacr, 0, NC_FP_UNSUPPORTED);
            verify(&bank, x, word, form, 31, 31, 0, 3, cpacr, 1, NC_FP_UNDEFINED);
        }
        for (unsigned field = 0; field < 2; ++field)
        for (unsigned bit = 0; bit < 32; ++bit) {
            uint32_t represented = field ? UINT32_C(0x1f) : UINT32_C(0x00c00000);
            if (represented & (UINT32_C(1) << bit)) continue;
            seed(&bank, x);
            if (field) bank.fpsr |= UINT32_C(1) << bit;
            else bank.fpcr |= UINT32_C(1) << bit;
            verify(&bank, x, word, form, 31, 31, 1, 1, 0x300000, 0, NC_FP_INVALID);
            verify(&bank, x, word, form, 31, 31, 0, 3, UINT64_MAX, 1, NC_FP_UNDEFINED);
            verify(&bank, x, word, form, 31, 31, 1, 0, 0, 0, NC_FP_TRAP_EL1);
            verify(&bank, x, word, form, 31, 31, 1, 1, 0x300000, 1, NC_FP_UNSUPPORTED);
            verify(&bank, x, word, form, 31, 31, 0, 4, 0, 0, NC_FP_INVALID);
        }
    }
}

static void exact_decoding(uint64_t x[31]) {
    static const uint32_t unsupported[] = {
        0, UINT32_MAX, UINT32_C(0xd503201f), UINT32_C(0x1e270000),
        UINT32_C(0x1e260000), UINT32_C(0x9ee70000), UINT32_C(0x9ee60000),
        UINT32_C(0x1e604000), UINT32_C(0x1e601000), UINT32_C(0x9e620000)
    };
    nc_fp_bank bank;
    for (unsigned form = 0; form < 4; ++form)
    for (unsigned bit = 10; bit < 32; ++bit) {
        uint32_t neighbor = (bases[form] | 29u << 5 | 31u) ^ (UINT32_C(1) << bit);
        unsigned other;
        for (other = 0; other < 4; ++other)
            if (neighbor == (bases[other] | 29u << 5 | 31u)) break;
        seed(&bank, x);
        verify(&bank, x, neighbor, other, 31, 29, 1, 1, 0x300000, 0,
               other < 4 ? NC_FP_OK : NC_FP_UNSUPPORTED);
        if (other == 4) {
            bank.fpcr |= UINT32_C(1) << 8;
            verify(&bank, x, neighbor, 0, 31, 29, 2, 4, UINT64_MAX, 2, NC_FP_UNSUPPORTED);
        }
    }
    for (unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        seed(&bank, x);
        verify(&bank, x, unsupported[i], 0, 0, 0, 1, 1, 0x300000, 0, NC_FP_UNSUPPORTED);
        bank.fpsr |= UINT32_C(1) << 27;
        verify(&bank, x, unsupported[i], 0, 0, 0, 0, 3, UINT64_MAX, 1, NC_FP_UNSUPPORTED);
        verify(&bank, x, unsupported[i], 0, 0, 0, 2, 4, UINT64_MAX, 2, NC_FP_UNSUPPORTED);
    }
}

int main(void) {
    long page_size = sysconf(_SC_PAGESIZE);
    CHECK(page_size > 0 && (size_t)page_size > X_BYTES + sizeof(uint64_t));
    size_t page = (size_t)page_size;
    unsigned char *mapping = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(mapping != MAP_FAILED);
    CHECK(mprotect(mapping + page, page, PROT_NONE) == 0);
    uint64_t *x = (uint64_t *)(void *)(mapping + page - X_BYTES);
    CHECK((uintptr_t)x % _Alignof(uint64_t) == 0);
    const uint64_t canary = UINT64_C(0xabcdef0176543210);
    memcpy((unsigned char *)x - sizeof(canary), &canary, sizeof(canary));

    selectors(x); patterns(x); controls_and_access(x);
    rejection_priority(x); exact_decoding(x);

    CHECK(!memcmp((unsigned char *)x - sizeof(canary), &canary, sizeof(canary)));
    CHECK(munmap(mapping, 2 * page) == 0);
    printf("{\"passed\":true,\"assertions\":%u}\n", checks);
    return 0;
}
