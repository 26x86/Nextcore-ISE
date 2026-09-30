/* 26x86 first-party DART v1 MMIO stub. See sandbox/devices/DART.md for register evidence.
 * Behaviour adapted from qemu-t8030 hw/arm/apple_dart.c (GPL reference only).
 * No page-table walk or guest DMA — MMIO storage and TLB invalidate ack only.
 */
#include "dart_v1.h"

static int dart_bounds(uint32_t off, unsigned width) {
    if (width != 4 || (off & 3u)) return -1;
    if (off >= VF_DART_INSTANCE_SIZE) return -1;
    return 0;
}

static int dart_sid(uint32_t off, uint32_t base, unsigned *sid) {
    if (off < base || ((off - base) & 3u)) return -1;
    unsigned index = (off - base) / 4u;
    if (index >= VF_DART_MAX_STREAMS) return -1;
    *sid = index;
    return 0;
}

static int dart_ttbr(uint32_t off, unsigned *sid, unsigned *idx) {
    if (off < VF_DART_REG_TTBR_BASE || ((off - VF_DART_REG_TTBR_BASE) & 3u)) return -1;
    unsigned delta = off - VF_DART_REG_TTBR_BASE;
    unsigned slot = delta / 4u;
    if (slot >= VF_DART_MAX_STREAMS * VF_DART_TTBR_PER_SID) return -1;
    *sid = slot / VF_DART_TTBR_PER_SID;
    *idx = slot % VF_DART_TTBR_PER_SID;
    return 0;
}

static void dart_reset_state(vf_dart_v1 *d) {
    unsigned sid;
    unsigned idx;
    d->params1 = VF_DART_PARAMS1_PAGE_SHIFT_VAL(VF_DART_DEFAULT_PAGE_SHIFT);
    d->params2 = 0;
    d->tlb_op = 0;
    d->sid_mask_lo = 0;
    d->sid_mask_hi = 0;
    d->error_status = 0;
    d->error_addr_lo = 0;
    d->error_addr_hi = 0;
    d->config = 0;
    d->tlb_generation = 0;
    for (unsigned i = 0; i < 4u; i++) d->sid_remap[i] = 0;
    for (sid = 0; sid < VF_DART_MAX_STREAMS; sid++) {
        d->tcr[sid] = 0;
        for (idx = 0; idx < VF_DART_TTBR_PER_SID; idx++) d->ttbr[sid][idx] = 0;
    }
}

int vf_dart_init(vf_dart_v1 *d) {
    if (!d) return -1;
    dart_reset_state(d);
    return 0;
}

uint32_t vf_dart_tlb_generation(const vf_dart_v1 *d) {
    return d ? d->tlb_generation : 0;
}

int vf_dart_irq_pending(const vf_dart_v1 *d) {
    return d && (d->error_status & VF_DART_ERROR_FLAG) != 0;
}

int vf_dart_simulate_fault(vf_dart_v1 *d, unsigned sid, uint32_t code) {
    if (!d || sid >= VF_DART_MAX_STREAMS) return -1;
    d->error_status = VF_DART_ERROR_FLAG | ((sid & 0xfu) << 24) | (code & 0xfffu);
    return 0;
}

int vf_dart_read(vf_dart_v1 *d, uint32_t off, unsigned width, uint32_t *value) {
    unsigned sid;
    unsigned idx;
    if (!d || !value || dart_bounds(off, width)) return -1;
    switch (off) {
    case VF_DART_REG_PARAMS1:
        *value = d->params1;
        return 0;
    case VF_DART_REG_PARAMS2:
        *value = d->params2;
        return 0;
    case VF_DART_REG_TLB_OP:
        *value = d->tlb_op & ~VF_DART_TLB_OP_BUSY;
        return 0;
    case VF_DART_REG_SID_MASK_LO:
        *value = d->sid_mask_lo;
        return 0;
    case VF_DART_REG_SID_MASK_HI:
        *value = d->sid_mask_hi;
        return 0;
    case VF_DART_REG_ERROR_STATUS:
        *value = d->error_status;
        return 0;
    case VF_DART_REG_ERROR_ADDR_LO:
        *value = d->error_addr_lo;
        return 0;
    case VF_DART_REG_ERROR_ADDR_HI:
        *value = d->error_addr_hi;
        return 0;
    case VF_DART_REG_CONFIG:
        *value = d->config;
        return 0;
    default:
        break;
    }
    if (off >= VF_DART_REG_SID_REMAP_BASE && off < VF_DART_REG_SID_REMAP_BASE + 16u) {
        *value = d->sid_remap[(off - VF_DART_REG_SID_REMAP_BASE) / 4u];
        return 0;
    }
    if (!dart_sid(off, VF_DART_REG_TCR_BASE, &sid)) {
        *value = d->tcr[sid];
        return 0;
    }
    if (!dart_ttbr(off, &sid, &idx)) {
        *value = d->ttbr[sid][idx];
        return 0;
    }
    return -1;
}

int vf_dart_write(vf_dart_v1 *d, uint32_t off, unsigned width, uint32_t value) {
    unsigned sid;
    unsigned idx;
    if (!d || dart_bounds(off, width)) return -1;
    if ((d->config & VF_DART_CONFIG_LOCK) != 0) {
        switch (off) {
        case VF_DART_REG_ERROR_STATUS:
        case VF_DART_REG_ERROR_ADDR_LO:
        case VF_DART_REG_ERROR_ADDR_HI:
        case VF_DART_REG_TLB_OP:
            break;
        default:
            return -1;
        }
    }
    switch (off) {
    case VF_DART_REG_PARAMS1:
        d->params1 = value;
        return 0;
    case VF_DART_REG_PARAMS2:
        d->params2 = value;
        return 0;
    case VF_DART_REG_TLB_OP:
        if (value & VF_DART_TLB_OP_INVALIDATE) {
            d->tlb_op = 0;
            d->tlb_generation = d->tlb_generation + 1u;
        } else {
            d->tlb_op = value & ~VF_DART_TLB_OP_BUSY;
        }
        return 0;
    case VF_DART_REG_SID_MASK_LO:
        d->sid_mask_lo = value;
        return 0;
    case VF_DART_REG_SID_MASK_HI:
        d->sid_mask_hi = value;
        return 0;
    case VF_DART_REG_ERROR_STATUS:
        d->error_status &= ~value;
        return 0;
    case VF_DART_REG_ERROR_ADDR_LO:
        d->error_addr_lo = value;
        return 0;
    case VF_DART_REG_ERROR_ADDR_HI:
        d->error_addr_hi = value;
        return 0;
    case VF_DART_REG_CONFIG:
        d->config = value;
        return 0;
    default:
        break;
    }
    if (off >= VF_DART_REG_SID_REMAP_BASE && off < VF_DART_REG_SID_REMAP_BASE + 16u) {
        d->sid_remap[(off - VF_DART_REG_SID_REMAP_BASE) / 4u] = value;
        return 0;
    }
    if (!dart_sid(off, VF_DART_REG_TCR_BASE, &sid)) {
        d->tcr[sid] = value;
        return 0;
    }
    if (!dart_ttbr(off, &sid, &idx)) {
        d->ttbr[sid][idx] = value;
        return 0;
    }
    return -1;
}
