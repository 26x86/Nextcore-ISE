/* 26x86 first-party ANS IOP autoBootRegMap stub for composite region index 2.
 * See ANS_AUTOBOOT.md. Behaviour: present/bootstrap announce; graph-local
 * map RO→0 / WO accept (qemu-t8030 apple_ans iop_autoboot_reg_ops) plus
 * optional ASCWrap READY → FLAG_ARMED composite bind (READY←mbox STARTED).
 * Bound map WO fail-closes without READY. No IOP firmware load, MSI, DT IRQ,
 * or command DMA.
 */
#include "ans_autoboot_v1.h"

#ifdef VF_EFI_BUILD
static void *ans_autoboot_memset(void *dst, int c, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
#define memset(d, c, n) ans_autoboot_memset((d), (c), (unsigned)(n))
#else
#include <string.h>
#endif

/* ARMED tracks bound ASCWrap READY; unbound never invents armed. */
static void ans_autoboot_reconcile_armed(vf_ans_autoboot_v1 *s) {
    if (!s) return;
    if (!s->ascwrap) {
        s->flags &= ~VF_ANS_AUTOBOOT_FLAG_ARMED;
        return;
    }
    if (vf_ascwrap_ready(s->ascwrap))
        s->flags |= VF_ANS_AUTOBOOT_FLAG_ARMED;
    else
        s->flags &= ~VF_ANS_AUTOBOOT_FLAG_ARMED;
}

int vf_ans_autoboot_init(vf_ans_autoboot_v1 *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    /* Honest announce: autoBootRegMap present in bootstrap; no IOP FW load. */
    s->status = VF_ANS_AUTOBOOT_STATUS_BOOTSTRAP;
    s->flags = VF_ANS_AUTOBOOT_FLAG_PRESENT;
    return 0;
}

uint32_t vf_ans_autoboot_status(const vf_ans_autoboot_v1 *s) {
    return s ? s->status : VF_ANS_AUTOBOOT_STATUS_ABSENT;
}

uint32_t vf_ans_autoboot_flags(vf_ans_autoboot_v1 *s) {
    if (!s) return 0u;
    ans_autoboot_reconcile_armed(s);
    return s->flags;
}

int vf_ans_autoboot_write_seen(const vf_ans_autoboot_v1 *s) {
    return s && s->autoboot_write_seen ? 1 : 0;
}

uint64_t vf_ans_autoboot_last_write(const vf_ans_autoboot_v1 *s) {
    return s ? s->last_autoboot_write : 0u;
}

int vf_ans_autoboot_bind_ascwrap(vf_ans_autoboot_v1 *s, vf_ascwrap_v1 *ascwrap) {
    if (!s) return -1;
    s->ascwrap = ascwrap;
    ans_autoboot_reconcile_armed(s);
    return 0;
}

int vf_ans_autoboot_armed(vf_ans_autoboot_v1 *s) {
    if (!s) return 0;
    ans_autoboot_reconcile_armed(s);
    return (s->flags & VF_ANS_AUTOBOOT_FLAG_ARMED) ? 1 : 0;
}

int vf_ans_autoboot_read(vf_ans_autoboot_v1 *s, uint32_t offset, unsigned width,
                         uint64_t *value) {
    if (!s || !value) return -1;
    if (offset >= VF_ANS_AUTOBOOT_MMIO_SIZE) return -1;
    switch (offset) {
    case VF_ANS_AUTOBOOT_MMIO_STATUS:
        if (width != 32) return -1;
        *value = s->status;
        return 0;
    case VF_ANS_AUTOBOOT_MMIO_FLAGS:
        if (width != 32) return -1;
        ans_autoboot_reconcile_armed(s);
        *value = s->flags;
        return 0;
    case VF_ANS_AUTOBOOT_MMIO_TIER:
        if (width != 32) return -1;
        *value = VF_ANS_AUTOBOOT_TIER_REGMAP;
        return 0;
    case VF_ANS_AUTOBOOT_MMIO_MAP:
        /* Reference: AppleA7IOP autoBootRegMap reads as 0. */
        if (width != 64) return -1;
        *value = 0;
        return 0;
    default:
        return -1;
    }
}

int vf_ans_autoboot_write(vf_ans_autoboot_v1 *s, uint32_t offset,
                          unsigned width, uint64_t value) {
    if (!s) return -1;
    /* Only the autoboot map slot is writable; status/flags/tier stay RO. */
    if (offset != VF_ANS_AUTOBOOT_MMIO_MAP) return -1;
    if (width != 64) return -1;
    /* Bound composite: WO without ASCWrap READY is dishonest. Unbound skips. */
    if (s->ascwrap) {
        ans_autoboot_reconcile_armed(s);
        if (!(s->flags & VF_ANS_AUTOBOOT_FLAG_ARMED)) return -1;
    }
    /* Accept write; guest-visible read remains 0 (no firmware load claim). */
    s->last_autoboot_write = value;
    s->autoboot_write_seen = 1;
    return 0;
}
