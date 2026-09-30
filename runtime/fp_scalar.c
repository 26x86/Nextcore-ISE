/* SPDX-License-Identifier: BSD-4-Clause; independently authored arithmetic. */
#include "fp_scalar.h"

#define SIGN UINT32_C(0x80000000)
#define ABS UINT32_C(0x7fffffff)
#define INF UINT32_C(0x7f800000)
#define QUIET UINT32_C(0x00400000)
#define FRACTION UINT32_C(0x007fffff)
#define HIDDEN UINT32_C(0x00800000)

void nc_fp32_init(nc_fp32_state *state) {
    state->rounding = NC_FP_RN;
    state->status = 0;
}
int nc_fp32_configure(nc_fp32_state *state, uint32_t control) {
    if (control & ~UINT32_C(0x00c00000)) return 0;
    state->rounding = (nc_fp_rounding)(control >> 22);
    return 1;
}
void nc_fp32_clear_status(nc_fp32_state *state, uint32_t mask) {
    state->status &= ~(mask & NC_FP_STATUS_MASK);
}
static nc_fp32_result result(nc_fp32_state *state, uint32_t bits, uint32_t flags) {
    nc_fp32_result out;
    state->status |= flags;
    out.bits = bits;
    out.status = state->status;
    return out;
}
static int is_nan(uint32_t bits) { return (bits & ABS) > INF; }
static int is_snan(uint32_t bits) { return is_nan(bits) && !(bits & QUIET); }
static nc_fp32_result nan_result(nc_fp32_state *state, uint32_t a, uint32_t b) {
    /* Signaling operands take priority, in operand order, over quiet NaNs. */
    if (is_snan(a)) return result(state, a | QUIET, NC_FP_IOC);
    if (is_snan(b)) return result(state, b | QUIET, NC_FP_IOC);
    return result(state, is_nan(a) ? a : b, 0);
}
static uint64_t shift_jam(uint64_t value, unsigned distance) {
    if (!distance) return value;
    if (distance >= 64) return value != 0;
    return (value >> distance) | ((value << (64 - distance)) != 0);
}
static uint32_t unpack(uint32_t bits, int *exponent) {
    uint32_t fraction = bits & FRACTION;
    unsigned encoded = (bits >> 23) & 255u;
    if (encoded) {
        *exponent = (int)encoded - 127;
        return fraction | HIDDEN;
    }
    *exponent = -126;
    while (!(fraction & HIDDEN)) { fraction <<= 1; --*exponent; }
    return fraction;
}
static nc_fp32_result overflow(nc_fp32_state *state, uint32_t sign) {
    int infinity = state->rounding == NC_FP_RN ||
        (state->rounding == NC_FP_RP && !sign) ||
        (state->rounding == NC_FP_RM && sign);
    return result(state, sign | (infinity ? INF : INF - 1), NC_FP_OFC | NC_FP_IXC);
}
/* Significand has its leading bit at 26, with three rounding bits. Any bits
 * below bit zero have been combined into the sticky bit, never discarded. */
static nc_fp32_result pack(nc_fp32_state *state, uint32_t sign,
                          int exponent, uint64_t significand) {
    int tiny = exponent < -126;
    uint32_t flags = 0;
    if (tiny) {
        significand = shift_jam(significand, (unsigned)(-126 - exponent));
        exponent = -126;
    }
    uint32_t tail = (uint32_t)(significand & 7u);
    uint32_t rounded = (uint32_t)(significand >> 3);
    int increment = state->rounding == NC_FP_RN ?
        (tail > 4u || (tail == 4u && (rounded & 1u))) :
        (tail && ((state->rounding == NC_FP_RP && !sign) ||
                  (state->rounding == NC_FP_RM && sign)));
    if (tail) flags = NC_FP_IXC | (tiny ? NC_FP_UFC : 0);
    rounded += (uint32_t)increment;
    if (rounded >= (HIDDEN << 1)) { rounded >>= 1; ++exponent; }
    if (exponent > 127) return overflow(state, sign);
    uint32_t encoded = rounded >= HIDDEN ? (uint32_t)(exponent + 127) << 23 : 0;
    return result(state, sign | encoded | (rounded & FRACTION), flags);
}
nc_fp32_result nc_fp32_add(nc_fp32_state *state, uint32_t a, uint32_t b) {
    uint32_t aa = a & ABS, ab = b & ABS, sa = a & SIGN, sb = b & SIGN;
    if (is_nan(a) || is_nan(b)) return nan_result(state, a, b);
    if (aa == INF || ab == INF) {
        if (aa == INF && ab == INF && sa != sb)
            return result(state, INF | QUIET, NC_FP_IOC);
        return result(state, aa == INF ? a : b, 0);
    }
    if (!aa && !ab) return result(state, sa == sb ? sa :
        (state->rounding == NC_FP_RM ? SIGN : 0), 0);
    if (!aa || !ab) return result(state, !aa ? b : a, 0);
    int ea, eb;
    uint64_t ma = (uint64_t)unpack(a, &ea) << 3;
    uint64_t mb = (uint64_t)unpack(b, &eb) << 3;
    /* Keep the greater magnitude on the left so subtraction stays unsigned. */
    if (ea < eb || (ea == eb && ma < mb)) {
        int te = ea; ea = eb; eb = te;
        uint64_t tm = ma; ma = mb; mb = tm;
        uint32_t ts = sa; sa = sb; sb = ts;
    }
    mb = shift_jam(mb, (unsigned)(ea - eb));
    uint64_t sum = sa == sb ? ma + mb : ma - mb;
    if (!sum) return result(state, state->rounding == NC_FP_RM ? SIGN : 0, 0);
    if (sum & (UINT64_C(1) << 27)) { sum = shift_jam(sum, 1); ++ea; }
    while (!(sum & (UINT64_C(1) << 26))) { sum <<= 1; --ea; }
    return pack(state, sa, ea, sum);
}
nc_fp32_result nc_fp32_div(nc_fp32_state *state, uint32_t a, uint32_t b) {
    uint32_t aa = a & ABS, ab = b & ABS, sign = (a ^ b) & SIGN;
    if (is_nan(a) || is_nan(b)) return nan_result(state, a, b);
    if ((!aa && !ab) || (aa == INF && ab == INF))
        return result(state, INF | QUIET, NC_FP_IOC);
    if (aa == INF) return result(state, sign | INF, 0);
    if (ab == INF) return result(state, sign, 0);
    if (!ab) return result(state, sign | INF, NC_FP_DZC);
    if (!aa) return result(state, sign, 0);
    int ea, eb;
    uint32_t ma = unpack(a, &ea), mb = unpack(b, &eb);
    int exponent = ea - eb;
    if (ma < mb) { ma <<= 1; --exponent; }
    uint64_t numerator = (uint64_t)ma << 26;
    uint64_t quotient = numerator / mb;
    if (numerator % mb) quotient |= 1u;
    return pack(state, sign, exponent, quotient);
}
