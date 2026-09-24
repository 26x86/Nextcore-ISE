/* 26x86 first-party ASCWrapV2 core-map stub for ANS composite region index 1.
 * See ASCWRAP.md. Behaviour: 8-byte core RO/WO → 0 (qemu-t8030 apple_ans
 * AppleASCWrapV2 coreRegisterMap). Present/bootstrap announce plus optional
 * mbox STARTED → FLAG_READY composite bind. No IOP ACTIVE, MSI, DT IRQ, or
 * command DMA.
 */
#include "ascwrap_v1.h"

#ifdef VF_EFI_BUILD
static void *ascwrap_memset(void *dst, int c, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
#define memset(d, c, n) ascwrap_memset((d), (c), (unsigned)(n))
#else
#include <string.h>
#endif

/* READY tracks bound mbox STARTED; unbound never invents ready. */
static void ascwrap_reconcile_ready(vf_ascwrap_v1 *s) {
    if (!s) return;
    if (!s->mbox) {
        s->flags &= ~VF_ASCWRAP_FLAG_READY;
        return;
    }
    if (vf_ans_mbox_started(s->mbox))
        s->flags |= VF_ASCWRAP_FLAG_READY;
    else
        s->flags &= ~VF_ASCWRAP_FLAG_READY;
}

int vf_ascwrap_init(vf_ascwrap_v1 *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    /* Honest announce: ASCWrap core map present in bootstrap; no ASC FW. */
    s->status = VF_ASCWRAP_STATUS_BOOTSTRAP;
    s->flags = VF_ASCWRAP_FLAG_PRESENT;
    return 0;
}

uint32_t vf_ascwrap_status(const vf_ascwrap_v1 *s) {
    return s ? s->status : VF_ASCWRAP_STATUS_ABSENT;
}

uint32_t vf_ascwrap_flags(vf_ascwrap_v1 *s) {
    if (!s) return 0u;
    ascwrap_reconcile_ready(s);
    return s->flags;
}

int vf_ascwrap_core_write_seen(const vf_ascwrap_v1 *s) {
    return s && s->core_write_seen ? 1 : 0;
}

uint64_t vf_ascwrap_last_core_write(const vf_ascwrap_v1 *s) {
    return s ? s->last_core_write : 0u;
}

int vf_ascwrap_bind_mbox(vf_ascwrap_v1 *s, vf_ans_mbox_v1 *mbox) {
    if (!s) return -1;
    s->mbox = mbox;
    ascwrap_reconcile_ready(s);
    return 0;
}

int vf_ascwrap_ready(vf_ascwrap_v1 *s) {
    if (!s) return 0;
    ascwrap_reconcile_ready(s);
    return (s->flags & VF_ASCWRAP_FLAG_READY) ? 1 : 0;
}

int vf_ascwrap_read(vf_ascwrap_v1 *s, uint32_t offset, unsigned width,
                    uint64_t *value) {
    if (!s || !value) return -1;
    if (offset >= VF_ASCWRAP_MMIO_SIZE) return -1;
    switch (offset) {
    case VF_ASCWRAP_MMIO_STATUS:
        if (width != 32) return -1;
        *value = s->status;
        return 0;
    case VF_ASCWRAP_MMIO_FLAGS:
        if (width != 32) return -1;
        ascwrap_reconcile_ready(s);
        *value = s->flags;
        return 0;
    case VF_ASCWRAP_MMIO_TIER:
        if (width != 32) return -1;
        *value = VF_ASCWRAP_TIER_CORE_MAP;
        return 0;
    case VF_ASCWRAP_MMIO_CORE:
        /* Reference: AppleASCWrapV2 coreRegisterMap reads as 0. */
        if (width != 64) return -1;
        *value = 0;
        return 0;
    default:
        return -1;
    }
}

int vf_ascwrap_write(vf_ascwrap_v1 *s, uint32_t offset, unsigned width,
                     uint64_t value) {
    if (!s) return -1;
    /* Only the 8-byte core is writable; status/flags/tier stay RO. */
    if (offset != VF_ASCWRAP_MMIO_CORE) return -1;
    if (width != 64) return -1;
    /* Accept write; guest-visible read remains 0 (RO/WO → 0 contract). */
    s->last_core_write = value;
    s->core_write_seen = 1;
    (void)VF_ASCWRAP_CORE_SIZE;
    return 0;
}
