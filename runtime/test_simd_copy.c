/* SPDX-License-Identifier: BSD-4-Clause; independent register-copy checks. */
#include "simd_copy.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum operation { DUP_ELEMENT, DUP_GPR, INS_GPR, INS_ELEMENT, SMOV, UMOV };
static uint64_t checks, cases;
static uint32_t current_word;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "check failed at line %d, word 0x%08" PRIx32 ": %s\n", \
            __LINE__, current_word, #condition); exit(1); \
} } while (0)

static const uint32_t bases[] = {
    UINT32_C(0x0e000400), UINT32_C(0x0e000c00),
    UINT32_C(0x4e001c00), UINT32_C(0x6e000400),
    UINT32_C(0x0e002c00), UINT32_C(0x0e003c00)
};

static uint32_t encoding(enum operation op, unsigned q, unsigned size,
                         unsigned dst, unsigned src, unsigned dst_lane,
                         unsigned src_lane) {
    unsigned imm5 = (1u << size) | (dst_lane << (size + 1u));
    unsigned imm4 = op == INS_ELEMENT ? src_lane << size : 0;
    return bases[op] | q << 30 | imm5 << 16 | imm4 << 11 | src << 5 | dst;
}

static void seed(nc_fp_bank *bank, uint64_t x[31]) {
    memset(bank, 0, sizeof(*bank));
    bank->fpcr = UINT32_C(0x00800000);
    bank->fpsr = NC_FP_IOC | NC_FP_UFC | NC_FP_IXC;
    for (unsigned r = 0; r < 32; ++r)
    for (unsigned b = 0; b < 16; ++b) {
        uint8_t byte = (uint8_t)(r * 37u + b * 13u + b * b + 19u);
        bank->v[r][b / 8] |= (uint64_t)byte << (8u * (b % 8));
    }
    for (unsigned r = 0; r < 31; ++r)
        x[r] = UINT64_C(0xa5e1b2c384957607) ^
               (UINT64_C(0x0123456789abcdef) * (r + 1u));
}

/* The oracle uses semantic arguments, not the instruction decoder. Elements
 * are byte arrays; extension fills bytes instead of copying service shifts. */
static void oracle(nc_fp_bank *bank, uint64_t x[31], enum operation op,
                   unsigned q, unsigned size, unsigned dst, unsigned src,
                   unsigned dst_lane, unsigned src_lane) {
    uint8_t vector[32][16], element[8], result[8] = {0};
    unsigned width = 1u << size;
    for (unsigned r = 0; r < 32; ++r)
    for (unsigned b = 0; b < 16; ++b)
        vector[r][b] = (uint8_t)(bank->v[r][b / 8] >> (8u * (b % 8)));
    if (op == DUP_GPR || op == INS_GPR) {
        uint64_t value = src == 31 ? 0 : x[src];
        for (unsigned b = 0; b < width; ++b)
            element[b] = (uint8_t)(value >> (8u * b));
    } else {
        unsigned lane = op == INS_ELEMENT ? src_lane : dst_lane;
        memcpy(element, &vector[src][lane * width], width);
    }
    if (op == DUP_ELEMENT || op == DUP_GPR) {
        memset(vector[dst], 0, 16);
        for (unsigned b = 0; b < (q ? 16u : 8u); b += width)
            memcpy(&vector[dst][b], element, width);
    } else if (op == INS_GPR || op == INS_ELEMENT) {
        memcpy(&vector[dst][dst_lane * width], element, width);
    } else if (dst != 31) {
        unsigned output_width = q ? 8u : 4u;
        uint8_t extension = op == SMOV && (element[width - 1u] & 0x80u)
            ? UINT8_MAX : 0;
        memset(result, extension, output_width);
        memcpy(result, element, width);
        uint64_t value = 0;
        for (unsigned b = 0; b < 8; ++b) value |= (uint64_t)result[b] << (8u * b);
        x[dst] = value;
    }
    for (unsigned r = 0; r < 32; ++r) {
        bank->v[r][0] = bank->v[r][1] = 0;
        for (unsigned b = 0; b < 16; ++b)
            bank->v[r][b / 8] |= (uint64_t)vector[r][b] << (8u * (b % 8));
    }
}

static nc_fp_result execute(nc_fp_bank *bank, uint64_t x[31], uint32_t word) {
    current_word = word;
    return nc_simd_execute_copy(bank, x, word, 1, 1, UINT64_C(0x00300000), 0);
}

static void success(nc_fp_bank *bank, uint64_t x[31], uint32_t word,
                    enum operation op, unsigned q, unsigned size,
                    unsigned dst, unsigned src, unsigned dst_lane,
                    unsigned src_lane) {
    nc_fp_bank expected = *bank;
    uint64_t expected_x[31];
    memcpy(expected_x, x, sizeof(expected_x));
    oracle(&expected, expected_x, op, q, size, dst, src, dst_lane, src_lane);
    ++cases;
    CHECK(execute(bank, x, word) == NC_FP_OK);
    CHECK(!memcmp(bank, &expected, sizeof(expected)));
    CHECK(!memcmp(x, expected_x, sizeof(expected_x)));
}

static void register_matrix(void) {
    nc_fp_bank bank;
    uint64_t x[31];
    for (unsigned q = 0; q < 2; ++q)
    for (unsigned size = 0; size < 4; ++size) {
        if (!q && size == 3) continue;
        for (unsigned lane = 0; lane < (16u >> size); ++lane)
        for (unsigned dst = 0; dst < 32; ++dst)
        for (unsigned src = 0; src < 32; ++src) {
            seed(&bank, x);
            success(&bank, x, encoding(DUP_ELEMENT,q,size,dst,src,lane,0),
                    DUP_ELEMENT,q,size,dst,src,lane,0);
            /* All upper immediate bits are ignored for the GPR source. */
            seed(&bank, x);
            success(&bank, x, encoding(DUP_GPR,q,size,dst,src,lane,0),
                    DUP_GPR,q,size,dst,src,0,0);
        }
    }
    for (unsigned size = 0; size < 4; ++size)
    for (unsigned lane = 0; lane < (16u >> size); ++lane)
    for (unsigned dst = 0; dst < 32; ++dst)
    for (unsigned src = 0; src < 32; ++src) {
        seed(&bank, x);
        success(&bank,x,encoding(INS_GPR,1,size,dst,src,lane,0),
                INS_GPR,1,size,dst,src,lane,0);
        for (unsigned source_lane = 0; source_lane < (16u >> size); ++source_lane) {
            seed(&bank, x);
            success(&bank,x,encoding(INS_ELEMENT,1,size,dst,src,lane,source_lane),
                    INS_ELEMENT,1,size,dst,src,lane,source_lane);
        }
    }
    for (unsigned q = 0; q < 2; ++q)
    for (unsigned size = 0; size < 4; ++size)
    for (unsigned op = SMOV; op <= UMOV; ++op) {
        int valid = op == SMOV ? (q ? size < 3 : size < 2)
                              : (q ? size == 3 : size < 3);
        if (!valid) continue;
        for (unsigned lane = 0; lane < (16u >> size); ++lane)
        for (unsigned dst = 0; dst < 32; ++dst)
        for (unsigned src = 0; src < 32; ++src) {
            seed(&bank, x);
            success(&bank,x,encoding((enum operation)op,q,size,dst,src,lane,0),
                    (enum operation)op,q,size,dst,src,lane,0);
        }
    }
}

static void ignored_source_bits(void) {
    nc_fp_bank bank;
    uint64_t x[31];
    for (unsigned size = 0; size < 4; ++size)
    for (unsigned lane = 0; lane < (16u >> size); ++lane)
    for (unsigned source_lane = 0; source_lane < (16u >> size); ++source_lane)
    for (unsigned ignored = 0; ignored < (1u << size); ++ignored)
    for (unsigned alias = 0; alias < 2; ++alias) {
        seed(&bank, x);
        unsigned src = alias ? 31 : 0;
        uint32_t word = encoding(INS_ELEMENT,1,size,31,src,lane,source_lane) |
                        ignored << 11;
        success(&bank,x,word,INS_ELEMENT,1,size,31,src,lane,source_lane);
    }
}

static void element_value(nc_fp_bank *bank, unsigned reg, unsigned size,
                          unsigned lane, uint64_t value) {
    unsigned width = 1u << size;
    for (unsigned b = 0; b < width; ++b) {
        unsigned at = lane * width + b;
        uint64_t mask = UINT64_C(0xff) << (8u * (at % 8));
        bank->v[reg][at / 8] = (bank->v[reg][at / 8] & ~mask) |
            (((value >> (8u * b)) & 0xffu) << (8u * (at % 8)));
    }
}

static void integer_extremes(void) {
    nc_fp_bank bank;
    uint64_t x[31];
    for (unsigned size = 0; size < 4; ++size) {
        unsigned bits = 8u << size;
        uint64_t sign = UINT64_C(1) << (bits - 1u);
        uint64_t maximum = size == 3 ? UINT64_MAX : (UINT64_C(1) << bits) - 1u;
        const uint64_t values[] = {0,1,sign - 1u,sign,sign + 1u,maximum - 1u,maximum};
        for (unsigned lane = 0; lane < (16u >> size); ++lane)
        for (unsigned pattern = 0; pattern < sizeof(values)/sizeof(values[0]); ++pattern)
        for (unsigned q = 0; q < 2; ++q)
        for (unsigned op = SMOV; op <= UMOV; ++op) {
            int valid = op == SMOV ? (q ? size < 3 : size < 2)
                                  : (q ? size == 3 : size < 3);
            if (!valid) continue;
            seed(&bank,x);
            element_value(&bank,31,size,lane,values[pattern]);
            x[0] = UINT64_MAX;
            success(&bank,x,encoding((enum operation)op,q,size,0,31,lane,0),
                    (enum operation)op,q,size,0,31,lane,0);
            if (!q) CHECK(x[0] >> 32 == 0);
        }
        for (unsigned pattern = 0; pattern < sizeof(values)/sizeof(values[0]); ++pattern)
        for (unsigned q = 0; q < 2; ++q) {
            if (!q && size == 3) continue;
            seed(&bank,x); x[0] = values[pattern];
            success(&bank,x,encoding(DUP_GPR,q,size,31,0,0,0),
                    DUP_GPR,q,size,31,0,0,0);
            if (!q) CHECK(bank.v[31][1] == 0);
        }
    }
    /* Explicit W sign extension is followed by X zero extension. */
    seed(&bank,x); element_value(&bank,31,0,15,0x80);
    CHECK(execute(&bank,x,encoding(SMOV,0,0,0,31,15,0)) == NC_FP_OK);
    CHECK(x[0] == UINT64_C(0x00000000ffffff80));
    seed(&bank,x); element_value(&bank,31,1,7,0x8000);
    CHECK(execute(&bank,x,encoding(SMOV,1,1,0,31,7,0)) == NC_FP_OK);
    CHECK(x[0] == UINT64_C(0xffffffffffff8000));
}

static void unchanged(nc_fp_bank *bank, uint64_t x[31], uint32_t word,
                      nc_fp_result want, unsigned present, unsigned el,
                      uint64_t cpacr, unsigned higher) {
    nc_fp_bank before = *bank;
    uint64_t before_x[31]; memcpy(before_x,x,sizeof(before_x));
    current_word = word;
    CHECK(nc_simd_execute_copy(bank,x,word,present,el,cpacr,higher) == want);
    CHECK(!memcmp(bank,&before,sizeof(before)));
    CHECK(!memcmp(x,before_x,sizeof(before_x)));
}

static void reserved_encodings(void) {
    nc_fp_bank bank;
    uint64_t x[31];
    for (unsigned op = DUP_ELEMENT; op <= UMOV; ++op)
    for (unsigned q = 0; q < 2; ++q)
    for (unsigned bad = 0; bad < 2; ++bad) {
        seed(&bank,x);
        uint32_t word = bases[op] | q << 30 | (bad ? 16u : 0u) << 16 |
                        31u << 5 | 31u;
        unchanged(&bank,x,word,NC_FP_UNSUPPORTED,1,1,0x300000,0);
        unchanged(&bank,x,word,NC_FP_UNSUPPORTED,0,3,UINT64_MAX,1);
    }
    for (unsigned size = 0; size < 4; ++size)
    for (unsigned q = 0; q < 2; ++q)
    for (unsigned op = DUP_ELEMENT; op <= UMOV; ++op) {
        int valid = op == SMOV ? (q ? size < 3 : size < 2) :
                    op == UMOV ? (q ? size == 3 : size < 3) :
                    op == INS_ELEMENT || op == INS_GPR ? q == 1 :
                    q || size < 3;
        if (valid) continue;
        seed(&bank,x);
        uint32_t word = encoding((enum operation)op,q,size,31,31,0,0);
        /* Clear Q explicitly: the INS instruction templates require Q=1. */
        if (!q) word &= ~UINT32_C(0x40000000);
        unchanged(&bank,x,word,NC_FP_UNSUPPORTED,1,1,0x300000,0);
    }
    static const uint32_t unrelated[] = {
        0,UINT32_MAX,UINT32_C(0xd503201f),UINT32_C(0x4e201c00),
        UINT32_C(0x5e010400),UINT32_C(0x4f00e400),UINT32_C(0xd53b4400),
        UINT32_C(0x4e003c00),UINT32_C(0x0e183c00)
    };
    for (unsigned i = 0; i < sizeof(unrelated)/sizeof(unrelated[0]); ++i) {
        seed(&bank,x);
        unchanged(&bank,x,unrelated[i],NC_FP_UNSUPPORTED,1,1,0x300000,0);
        unchanged(&bank,x,unrelated[i],NC_FP_UNSUPPORTED,0,3,UINT64_MAX,1);
    }
    /* Bits fixed in every supported family's encoding must not be ignored. */
    static const unsigned fixed_bits[] = {10,15,21,22,23,24,25,26,27,28,31};
    for (unsigned op = DUP_ELEMENT; op <= UMOV; ++op) {
        unsigned q = op == INS_GPR || op == INS_ELEMENT;
        uint32_t word = encoding((enum operation)op,q,0,31,31,0,0);
        for (unsigned i = 0; i < sizeof(fixed_bits)/sizeof(fixed_bits[0]); ++i) {
            seed(&bank,x);
            unchanged(&bank,x,word ^ (UINT32_C(1) << fixed_bits[i]),
                      NC_FP_UNSUPPORTED,1,1,0x300000,0);
        }
    }
}

static void access_and_bank_state(void) {
    nc_fp_bank bank;
    uint64_t x[31];
    for (unsigned op = DUP_ELEMENT; op <= UMOV; ++op) {
        unsigned q = op == INS_GPR || op == INS_ELEMENT;
        uint32_t word = encoding((enum operation)op,q,0,31,31,0,0);
        for (unsigned present = 0; present < 2; ++present)
        for (unsigned el = 0; el < 4; ++el)
        for (unsigned fpen = 0; fpen < 4; ++fpen)
        for (unsigned higher = 0; higher < 2; ++higher) {
            nc_fp_result want = !present ? NC_FP_UNDEFINED :
                el > 1 || higher ? NC_FP_UNSUPPORTED :
                fpen == 0 || fpen == 2 || (fpen == 1 && el == 0)
                    ? NC_FP_TRAP_EL1 : NC_FP_OK;
            seed(&bank,x);
            if (want != NC_FP_OK)
                unchanged(&bank,x,word,want,present,el,(uint64_t)fpen << 20,higher);
            else {
                nc_fp_bank expected = bank;
                uint64_t expected_x[31]; memcpy(expected_x,x,sizeof(expected_x));
                oracle(&expected,expected_x,(enum operation)op,q,0,31,31,0,0);
                current_word = word;
                CHECK(nc_simd_execute_copy(&bank,x,word,present,el,
                                           (uint64_t)fpen << 20,higher) == NC_FP_OK);
                CHECK(!memcmp(&bank,&expected,sizeof(bank)));
                CHECK(!memcmp(x,expected_x,sizeof(x)));
            }
        }
        seed(&bank,x);
        unchanged(&bank,x,word,NC_FP_INVALID,2,1,0x300000,0);
        unchanged(&bank,x,word,NC_FP_INVALID,1,4,0x300000,0);
        unchanged(&bank,x,word,NC_FP_INVALID,1,1,0x300000,2);
        for (unsigned bit = 0; bit < 64; ++bit) {
            if (bit == 20 || bit == 21) continue;
            unchanged(&bank,x,word,NC_FP_UNSUPPORTED,1,1,
                      UINT64_C(0x00300000) | (UINT64_C(1) << bit),0);
        }
        for (unsigned field = 0; field < 2; ++field)
        for (unsigned bit = 0; bit < 32; ++bit) {
            uint32_t allowed = field ? NC_FP_STATUS_MASK : UINT32_C(0x00c00000);
            if (allowed & (UINT32_C(1) << bit)) continue;
            seed(&bank,x);
            if (field) bank.fpsr |= UINT32_C(1) << bit;
            else bank.fpcr |= UINT32_C(1) << bit;
            unchanged(&bank,x,word,NC_FP_INVALID,1,1,0x300000,0);
            unchanged(&bank,x,word,NC_FP_UNDEFINED,0,3,UINT64_MAX,1);
            unchanged(&bank,x,word,NC_FP_TRAP_EL1,1,0,0,0);
        }
        for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned flags = 0; flags < 32; ++flags) {
            seed(&bank,x); bank.fpcr = mode << 22; bank.fpsr = flags;
            success(&bank,x,word,(enum operation)op,q,0,31,31,0,0);
            CHECK(bank.fpcr == mode << 22 && bank.fpsr == flags);
        }
        seed(&bank,x);
        nc_fp_bank saved = bank;
        uint64_t saved_x[31]; memcpy(saved_x,x,sizeof(saved_x));
        CHECK(execute(NULL,x,word) == NC_FP_INVALID);
        CHECK(!memcmp(x,saved_x,sizeof(x)));
        CHECK(execute(&bank,NULL,word) == NC_FP_INVALID);
        CHECK(!memcmp(&bank,&saved,sizeof(bank)));
        CHECK(nc_simd_execute_copy(NULL,x,0,0,3,UINT64_MAX,1) == NC_FP_INVALID);
        CHECK(nc_simd_execute_copy(&bank,NULL,0,0,3,UINT64_MAX,1) == NC_FP_INVALID);
    }
}

static void overlapping_storage(void) {
    const size_t storage_size = 128u * sizeof(uint64_t);
    unsigned char *storage = malloc(storage_size);
    uint8_t saved[128u * sizeof(uint64_t)];
    uint64_t separate[31];
    uint32_t word = encoding(DUP_GPR,1,3,0,0,0,0);
    CHECK(storage != NULL);
    /* Both overlap directions, exact endpoints and interior overlap. */
    static const unsigned bank_offsets[] = {0,0,0,30,31};
    static const unsigned x_offsets[] = {0,1,64,0,1};
    for (unsigned i = 0; i < sizeof(bank_offsets)/sizeof(bank_offsets[0]); ++i) {
        memset(storage,0,storage_size);
        nc_fp_bank *bank = (nc_fp_bank *)(storage + bank_offsets[i] * sizeof(uint64_t));
        uint64_t *x = (uint64_t *)(storage + x_offsets[i] * sizeof(uint64_t));
        seed(bank,separate);
        memcpy(saved,storage,sizeof(saved));
        current_word = word;
        CHECK(nc_simd_execute_copy(bank,x,word,1,1,0x300000,0) == NC_FP_INVALID);
        CHECK(!memcmp(storage,saved,storage_size));
    }
    /* Adjacent disjoint storage must not be rejected as overlapping. */
    memset(storage,0,storage_size);
    nc_fp_bank *bank = (nc_fp_bank *)storage;
    uint64_t *x = (uint64_t *)(storage + sizeof(nc_fp_bank));
    seed(bank,x);
    success(bank,x,word,DUP_GPR,1,3,0,0,0,0);
    free(storage);
}

static void assembled_examples(void) {
    static const struct {
        uint32_t word;
        enum operation op;
        unsigned q,size,dst,src,dst_lane,src_lane;
    } fixtures[] = {
        {UINT32_C(0x4e080c05),DUP_GPR,1,3,5,0,0,0},
        {UINT32_C(0x0e1e04eb),DUP_ELEMENT,0,1,11,7,7,0},
        {UINT32_C(0x4e181ce1),INS_GPR,1,3,1,7,1,0},
        {UINT32_C(0x6e084480),INS_ELEMENT,1,3,0,4,0,1},
        {UINT32_C(0x0e1f2c01),SMOV,0,0,1,0,15,0},
        {UINT32_C(0x4e183e47),UMOV,1,3,7,18,1,0},
        {UINT32_C(0x4e010fff),DUP_GPR,1,0,31,31,0,0},
        {UINT32_C(0x4e1c1fff),INS_GPR,1,2,31,31,3,0},
        {UINT32_C(0x0e1c3fff),UMOV,0,2,31,31,3,0},
        {UINT32_C(0x4e1c2fff),SMOV,1,2,31,31,3,0}
    };
    /* Independently cross-assembled forms bind exact words to semantics. */
    for (unsigned i = 0; i < sizeof(fixtures)/sizeof(fixtures[0]); ++i) {
        nc_fp_bank bank; uint64_t x[31]; seed(&bank,x);
        success(&bank,x,fixtures[i].word,fixtures[i].op,fixtures[i].q,
                fixtures[i].size,fixtures[i].dst,fixtures[i].src,
                fixtures[i].dst_lane,fixtures[i].src_lane);
    }
}

int main(void) {
    register_matrix(); ignored_source_bits(); integer_extremes();
    reserved_encodings(); access_and_bank_state(); overlapping_storage();
    assembled_examples();
    printf("{\"passed\":true,\"cases\":%" PRIu64 ",\"assertions\":%" PRIu64 "}\n",
           cases,checks);
    return 0;
}
