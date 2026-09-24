/* 26x86 first-party graph-local timer stub. See TIMER.md.
 * Matches Rust M1Timer pending rule: enabled && !masked && counter >= compare.
 * Not a C-JIT architectural CNTP/CNTV substitute.
 */
#include "timer_v1.h"

#ifdef VF_EFI_BUILD
static void *timer_memset(void *dst, int c, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
#define memset(d, c, n) timer_memset((d), (c), (unsigned)(n))
#else
#include <string.h>
#endif

int vf_timer_init(vf_timer_v1 *t) {
    if (!t) return -1;
    memset(t, 0, sizeof(*t));
    t->frequency = VF_TIMER_FREQUENCY_HZ;
    return 0;
}

int vf_timer_advance(vf_timer_v1 *t, uint64_t ticks) {
    if (!t) return -1;
    if (t->counter > UINT64_MAX - ticks)
        t->counter = UINT64_MAX;
    else
        t->counter += ticks;
    return 0;
}

int vf_timer_irq_pending(const vf_timer_v1 *t) {
    if (!t) return 0;
    if ((t->ctl & VF_M1_TIMER_CTL_ENABLE) == 0) return 0;
    if ((t->ctl & VF_M1_TIMER_CTL_MASKED) != 0) return 0;
    return t->counter >= t->compare ? 1 : 0;
}

int vf_timer_read(vf_timer_v1 *t, uint32_t offset, unsigned width_bits,
                  uint64_t *value) {
    if (!t || !value) return -1;
    switch (offset) {
    case VF_TIMER_MMIO_COUNTER:
        if (width_bits != 64) return -1;
        *value = t->counter;
        return 0;
    case VF_TIMER_MMIO_COMPARE:
        if (width_bits != 64) return -1;
        *value = t->compare;
        return 0;
    case VF_TIMER_MMIO_CTL:
        if (width_bits != 32) return -1;
        *value = t->ctl & VF_M1_TIMER_CTL_WRITABLE_MASK;
        return 0;
    case VF_TIMER_MMIO_FREQUENCY:
        if (width_bits != 64) return -1;
        *value = t->frequency;
        return 0;
    default:
        return -1;
    }
}

int vf_timer_write(vf_timer_v1 *t, uint32_t offset, unsigned width_bits,
                   uint64_t value) {
    if (!t) return -1;
    switch (offset) {
    case VF_TIMER_MMIO_COMPARE:
        if (width_bits != 64) return -1;
        t->compare = value;
        return 0;
    case VF_TIMER_MMIO_CTL:
        if (width_bits != 32) return -1;
        if ((value & ~((uint64_t)VF_M1_TIMER_CTL_WRITABLE_MASK)) != 0) return -1;
        t->ctl = (uint32_t)value & VF_M1_TIMER_CTL_WRITABLE_MASK;
        return 0;
    case VF_TIMER_MMIO_COUNTER:
    case VF_TIMER_MMIO_FREQUENCY:
        return -1; /* read-only */
    default:
        return -1;
    }
}
