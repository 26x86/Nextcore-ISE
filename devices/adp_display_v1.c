/* 26x86 first-party ADP L1 framebuffer stub. See ADP.md for tier boundary.
 * Behaviour adapted from Inferno apple_displaypipe_v4.c frame/IRQ latch
 * (GPL reference only). No scaler, DSI bridge, or Metal/GPU claim.
 */
#include "adp_display_v1.h"

#ifdef VF_EFI_BUILD
static void *adp_memset(void *dst, int c, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
#define memset(d, c, n) adp_memset((d), (c), (unsigned)(n))
#else
#include <string.h>
#endif

static int configured(const vf_adp_display_v1 *s) {
    return s && (s->flags & VF_ADP_FLAG_CONFIGURED) != 0;
}

static void refresh_flags(vf_adp_display_v1 *s) {
    uint32_t f = VF_ADP_FLAG_PRESENT;
    if (s->width && s->height && s->backing_bytes)
        f |= VF_ADP_FLAG_CONFIGURED;
    if (s->flags & VF_ADP_FLAG_ENABLED)
        f |= VF_ADP_FLAG_ENABLED;
    if (s->flags & VF_ADP_FLAG_PRESENTED)
        f |= VF_ADP_FLAG_PRESENTED;
    if (s->vblank_pending)
        f |= VF_ADP_FLAG_VBLANK_PENDING;
    s->flags = f;
}

int vf_adp_display_init(vf_adp_display_v1 *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    s->flags = VF_ADP_FLAG_PRESENT;
    return 0;
}

int vf_adp_display_configure(vf_adp_display_v1 *s, uint64_t guest_address,
                             uint64_t backing_bytes, uint32_t width,
                             uint32_t height, uint32_t stride) {
    uint64_t required;

    if (!s) return -1;
    if ((guest_address & 7u) != 0 || backing_bytes == 0 || width == 0 ||
        height == 0 || width > VF_ADP_MAX_WIDTH || height > VF_ADP_MAX_HEIGHT)
        return -1;
    if (stride < width * 4u) return -1;
    required = (uint64_t)stride * (uint64_t)height;
    if (required > backing_bytes) return -1;

    s->guest_address = guest_address;
    s->backing_bytes = backing_bytes;
    s->width = width;
    s->height = height;
    s->stride = stride;
    s->flags &= ~(VF_ADP_FLAG_ENABLED | VF_ADP_FLAG_PRESENTED);
    s->vblank_pending = 0;
    s->generation = s->generation + 1u;
    refresh_flags(s);
    return 0;
}

int vf_adp_display_present(vf_adp_display_v1 *s) {
    if (!configured(s)) return -1;
    s->flags |= VF_ADP_FLAG_ENABLED | VF_ADP_FLAG_PRESENTED;
    s->frame_count = s->frame_count + 1u;
    s->vblank_pending = 1;
    refresh_flags(s);
    return 0;
}

int vf_adp_display_ack_vblank(vf_adp_display_v1 *s) {
    if (!s) return -1;
    s->vblank_pending = 0;
    refresh_flags(s);
    return 0;
}

int vf_adp_display_irq_pending(const vf_adp_display_v1 *s) {
    return s && s->vblank_pending ? 1 : 0;
}

int vf_adp_display_read(vf_adp_display_v1 *s, uint32_t offset, unsigned width,
                        uint64_t *value) {
    if (!s || !value) return -1;
    if (width != 32) return -1;
    switch (offset) {
    case VF_ADP_MMIO_CTRL:
        refresh_flags(s);
        *value = s->flags;
        return 0;
    case VF_ADP_MMIO_WIDTH:
        *value = s->width;
        return 0;
    case VF_ADP_MMIO_HEIGHT:
        *value = s->height;
        return 0;
    case VF_ADP_MMIO_STRIDE:
        *value = s->stride;
        return 0;
    case VF_ADP_MMIO_FRAME_COUNT:
        *value = (uint32_t)s->frame_count;
        return 0;
    case VF_ADP_MMIO_TIER:
        *value = VF_ADP_TIER_L1_FRAMEBUFFER;
        return 0;
    case VF_ADP_MMIO_GENERATION:
        *value = s->generation;
        return 0;
    default:
        return -1;
    }
}

int vf_adp_display_write(vf_adp_display_v1 *s, uint32_t offset, unsigned width,
                         uint64_t value) {
    if (!s) return -1;
    if (width != 32) return -1;
    switch (offset) {
    case VF_ADP_MMIO_CTRL:
        if (value == VF_ADP_CTRL_DISABLE) {
            s->flags &= ~VF_ADP_FLAG_ENABLED;
            refresh_flags(s);
            return 0;
        }
        if (value == VF_ADP_CTRL_PRESENT)
            return vf_adp_display_present(s);
        return -1;
    case VF_ADP_MMIO_VBLANK_ACK:
        if (value != 1u) return -1;
        return vf_adp_display_ack_vblank(s);
    default:
        return -1;
    }
}
