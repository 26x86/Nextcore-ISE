/* SPDX-License-Identifier: BSD-4-Clause; independently authored ISA tests.
 * Build with -frounding-math -ffp-contract=off -fno-fast-math. Native float
 * is an independent oracle only in this test, never in the implementation. */
#include "fp_scalar.h"
#include <fenv.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma STDC FENV_ACCESS ON
static unsigned long checks, oracle_cases, host_underflow_differences;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static const int native_modes[] = {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO};
static float as_float(uint32_t bits) { float value; memcpy(&value, &bits, 4); return value; }
static uint32_t as_bits(float value) { uint32_t bits; memcpy(&bits, &value, 4); return bits; }
static uint32_t native_flags(int flags) {
    return ((flags & FE_INVALID) ? NC_FP_IOC : 0) |
        ((flags & FE_DIVBYZERO) ? NC_FP_DZC : 0) |
        ((flags & FE_OVERFLOW) ? NC_FP_OFC : 0) |
        ((flags & FE_UNDERFLOW) ? NC_FP_UFC : 0) |
        ((flags & FE_INEXACT) ? NC_FP_IXC : 0);
}
static void vector(int divide, uint32_t a, uint32_t b,
                   const uint32_t expected[4], uint32_t flags) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        nc_fp32_state state; nc_fp32_init(&state);
        CHECK(nc_fp32_configure(&state, mode << 22));
        nc_fp32_result out = divide ? nc_fp32_div(&state, a, b) : nc_fp32_add(&state, a, b);
        if (out.bits != expected[mode] || out.status != flags) {
            fprintf(stderr, "vector %s mode=%u a=%08x b=%08x got=%08x/%x expected=%08x/%x\n",
                    divide ? "div" : "add", mode, a, b, out.bits, out.status, expected[mode], flags);
            exit(1);
        }
        CHECK(out.bits == expected[mode]); CHECK(out.status == flags); CHECK(state.status == flags);
    }
}
#define V(op,a,b,rn,rp,rm,rz,f) vector(op,a,b,(const uint32_t[]){rn,rp,rm,rz},f)
static void edges(void) {
    const uint32_t ix = NC_FP_IXC, uf = NC_FP_UFC | NC_FP_IXC, of = NC_FP_OFC | NC_FP_IXC;
    V(0,0,0,0,0,0,0,0);
    V(0,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0);
    V(0,0,0x80000000,0,0,0x80000000,0,0);
    V(0,0x3f800000,0xbf800000,0,0,0x80000000,0,0);
    V(0,0x3f800000,0x33800000,0x3f800000,0x3f800001,0x3f800000,0x3f800000,ix);
    V(0,0x3f800001,0x33800000,0x3f800002,0x3f800002,0x3f800001,0x3f800001,ix);
    V(0,0xbf800000,0xb3800000,0xbf800000,0xbf800000,0xbf800001,0xbf800000,ix);
    V(0,0xbf800001,0xb3800000,0xbf800002,0xbf800001,0xbf800002,0xbf800001,ix);
    V(0,0x007fffff,1,0x00800000,0x00800000,0x00800000,0x00800000,0);
    V(0,0x00800000,0x807fffff,1,1,1,1,0);
    V(0,1,1,2,2,2,2,0);
    V(0,0x7f7fffff,0x7f7fffff,0x7f800000,0x7f800000,0x7f7fffff,0x7f7fffff,of);
    V(0,0xff7fffff,0xff7fffff,0xff800000,0xff7fffff,0xff800000,0xff7fffff,of);
    V(0,0x7f800000,0xff800000,0x7fc00000,0x7fc00000,0x7fc00000,0x7fc00000,NC_FP_IOC);
    V(0,0x7f800000,0x3f800000,0x7f800000,0x7f800000,0x7f800000,0x7f800000,0);
    V(1,0x3f800000,0x40400000,0x3eaaaaab,0x3eaaaaab,0x3eaaaaaa,0x3eaaaaaa,ix);
    V(1,0xbf800000,0x40400000,0xbeaaaaab,0xbeaaaaaa,0xbeaaaaab,0xbeaaaaaa,ix);
    V(1,1,0x40000000,0,1,0,0,uf);
    V(1,0x80000001,0x40000000,0x80000000,0x80000000,0x80000001,0x80000000,uf);
    /* Tiny before rounding, including rounding to the minimum normal. */
    V(1,0x00ffffff,0x40000000,0x00800000,0x00800000,0x007fffff,0x007fffff,uf);
    V(1,0x80ffffff,0x40000000,0x80800000,0x807fffff,0x80800000,0x807fffff,uf);
    V(1,0x00800000,0x40000000,0x00400000,0x00400000,0x00400000,0x00400000,0);
    V(1,0x7f7fffff,0x3f000000,0x7f800000,0x7f800000,0x7f7fffff,0x7f7fffff,of);
    V(1,0xff7fffff,0x3f000000,0xff800000,0xff7fffff,0xff800000,0xff7fffff,of);
    V(1,0,0,0x7fc00000,0x7fc00000,0x7fc00000,0x7fc00000,NC_FP_IOC);
    V(1,0x7f800000,0xff800000,0x7fc00000,0x7fc00000,0x7fc00000,0x7fc00000,NC_FP_IOC);
    V(1,0xbf800000,0,0xff800000,0xff800000,0xff800000,0xff800000,NC_FP_DZC);
    V(1,0x3f800000,0x80000000,0xff800000,0xff800000,0xff800000,0xff800000,NC_FP_DZC);
    V(1,0x7f800000,0x80000000,0xff800000,0xff800000,0xff800000,0xff800000,0);
    V(1,0xbf800000,0x7f800000,0x80000000,0x80000000,0x80000000,0x80000000,0);
    V(1,0x80000000,0xbf800000,0,0,0,0,0);
    for (unsigned op = 0; op < 2; ++op) {
        V(op,0x7fc01234,0xffc05678,0x7fc01234,0x7fc01234,0x7fc01234,0x7fc01234,0);
        V(op,0x7fc01234,0xff801234,0xffc01234,0xffc01234,0xffc01234,0xffc01234,NC_FP_IOC);
        V(op,0xff801234,0x7f805678,0xffc01234,0xffc01234,0xffc01234,0xffc01234,NC_FP_IOC);
        V(op,0x3f800000,0xffc05678,0xffc05678,0xffc05678,0xffc05678,0xffc05678,0);
    }
    nc_fp32_state state; nc_fp32_init(&state);
    CHECK(nc_fp32_configure(&state, 2u << 22));
    (void)nc_fp32_div(&state, 0, 0);
    CHECK(state.status == NC_FP_IOC);
    for (unsigned bit = 0; bit < 32; ++bit) if (bit != 22 && bit != 23) {
        CHECK(!nc_fp32_configure(&state, (1u << bit) | (1u << 22)));
        CHECK(state.rounding == NC_FP_RM && state.status == NC_FP_IOC);
    }
    CHECK(!nc_fp32_configure(&state, 0x9f00));
    CHECK(state.rounding == NC_FP_RM && state.status == NC_FP_IOC);
    (void)nc_fp32_div(&state, 0x3f800000, 0);
    CHECK(state.status == (NC_FP_IOC | NC_FP_DZC));
    (void)nc_fp32_div(&state, 0x7f7fffff, 0x3f000000);
    (void)nc_fp32_div(&state, 1, 0x40000000);
    CHECK(state.status == NC_FP_STATUS_MASK);
    CHECK(nc_fp32_configure(&state, 3u << 22));
    CHECK(state.status == NC_FP_STATUS_MASK);
    nc_fp32_clear_status(&state, NC_FP_IOC | 0xffff0000u);
    CHECK(state.status == 30 && state.rounding == NC_FP_RZ);
    nc_fp32_result out = nc_fp32_add(&state, 0x3f800000, 0x3f800000);
    CHECK(out.bits == 0x40000000 && out.status == 30);
    nc_fp32_clear_status(&state, NC_FP_STATUS_MASK);
    CHECK(state.status == 0 && state.rounding == NC_FP_RZ);
}
static void oracle(unsigned mode, int divide, uint32_t a, uint32_t b) {
    if ((a & 0x7fffffff) > 0x7f800000 || (b & 0x7fffffff) > 0x7f800000) return;
    volatile float left = as_float(a), right = as_float(b), answer;
    CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    if (divide) answer = left / right; else answer = left + right;
    uint32_t native = as_bits(answer), flags = native_flags(fetestexcept(FE_ALL_EXCEPT));
    /* Binary64 has enough precision to classify binary32 input sums/ratios
     * relative to the minimum normal. Host underflow flags are not the oracle
     * for AH=0 tininess before rounding. */
    volatile double wide_left = (double)left, wide_right = (double)right;
    double exact = divide ? wide_left / wide_right : wide_left + wide_right;
    double magnitude = exact < 0 ? -exact : exact;
    int tiny = magnitude != 0 && magnitude < (double)FLT_MIN;
    uint32_t expected = (flags & ~NC_FP_UFC) | ((tiny && (flags & NC_FP_IXC)) ? NC_FP_UFC : 0);
    if ((flags ^ expected) & NC_FP_UFC) ++host_underflow_differences;
    nc_fp32_state state; nc_fp32_init(&state);
    CHECK(nc_fp32_configure(&state, mode << 22));
    nc_fp32_result out = divide ? nc_fp32_div(&state, a, b) : nc_fp32_add(&state, a, b);
    /* Native generated-invalid NaN sign is implementation-specific. */
    if ((native & 0x7fffffff) > 0x7f800000) native = 0x7fc00000;
    if (out.bits != native || out.status != expected) {
        fprintf(stderr, "oracle %s mode=%u a=%08x b=%08x got=%08x/%x wanted=%08x/%x host=%x\n",
                divide ? "div" : "add", mode, a, b, out.bits, out.status, native, expected, flags);
        exit(1);
    }
    CHECK(out.bits == native); CHECK(out.status == expected); ++oracle_cases;
}
static uint32_t random_word(uint32_t *seed) {
    uint32_t x = *seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *seed = x; return x;
}
int main(void) {
    edges();
    const uint32_t boundary[] = {0,0x80000000,1,2,0x007fffff,0x00800000,
        0x00800001,0x00ffffff,0x33800000,0x3f7fffff,0x3f800000,0x3f800001,
        0x40000000,0x40400000,0x7f7fffff,0x7f800000,0x80000001,0x807fffff,
        0x80800000,0x80ffffff,0xbf800000,0xff7fffff,0xff800000};
    for (unsigned mode = 0; mode < 4; ++mode) {
        CHECK(fesetround(native_modes[mode]) == 0);
        for (unsigned i = 0; i < sizeof(boundary)/sizeof(*boundary); ++i)
            for (unsigned j = 0; j < sizeof(boundary)/sizeof(*boundary); ++j)
                for (int op = 0; op < 2; ++op) oracle(mode, op, boundary[i], boundary[j]);
        uint32_t seed = 0x193ac527;
        for (unsigned i = 0; i < 100000; ++i) {
            uint32_t a = random_word(&seed), b = random_word(&seed);
            oracle(mode, 0, a, b); oracle(mode, 1, a, b);
        }
    }
    CHECK(fesetround(FE_TONEAREST) == 0);
    printf("binary32 add/div: %lu assertions, %lu native oracle cases, four rounding modes; %lu host underflow-policy differences classified separately\n",
           checks, oracle_cases, host_underflow_differences);
    return 0;
}
