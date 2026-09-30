/* 26x86 first-party SART v1 region-table MMIO stub. See SART.md for register evidence.
 * Behaviour adapted from qemu-t8030 hw/arm/apple_sart.c (GPL reference only).
 * Region writes bump generation (IOMMU notifier stand-in).
 * Translate is identity at 4 KiB with versioned region hit detection — no DART walk.
 */
#include "sart_v1.h"

static int sart_bounds(uint32_t off, unsigned width) {
    if (width != 4 || (off & 3u)) return -1;
    if (off >= VF_SART_REG_SIZE) return -1;
    return 0;
}

static int sart_region_write(vf_sart_v1 *s, uint32_t off) {
    if (off < VF_SART_NUM_REGIONS * 4u) return 1;
    if (off >= VF_SART_BANK_ADDR && off < VF_SART_BANK_ADDR + VF_SART_NUM_REGIONS * 4u) return 1;
    if (s->version == VF_SART_VERSION_3
        && off >= VF_SART_BANK_SIZE_V3
        && off < VF_SART_BANK_SIZE_V3 + VF_SART_NUM_REGIONS * 4u) {
        return 1;
    }
    return 0;
}

static uint32_t sart_reg(const vf_sart_v1 *s, uint32_t off) {
    return s->regs[off / 4u];
}

int vf_sart_init(vf_sart_v1 *s, uint32_t version) {
    if (!s) return -1;
    if (version != VF_SART_VERSION_1 && version != VF_SART_VERSION_2
        && version != VF_SART_VERSION_3) {
        return -1;
    }
    s->version = version;
    s->generation = 0;
    for (unsigned i = 0; i < VF_SART_REG_WORDS; i++) s->regs[i] = 0;
    return 0;
}

uint32_t vf_sart_generation(const vf_sart_v1 *s) {
    return s ? s->generation : 0;
}

int vf_sart_read(vf_sart_v1 *s, uint32_t off, unsigned width, uint32_t *value) {
    if (!s || !value || sart_bounds(off, width)) return -1;
    *value = s->regs[off / 4u];
    return 0;
}

int vf_sart_write(vf_sart_v1 *s, uint32_t off, unsigned width, uint32_t value) {
    if (!s || sart_bounds(off, width)) return -1;
    if (sart_region_write(s, off)) s->generation++;
    s->regs[off / 4u] = value;
    return 0;
}

int vf_sart_region_decode(const vf_sart_v1 *s, unsigned region,
                          uint32_t *addr_pages, uint32_t *size_pages,
                          uint32_t *flags) {
    uint32_t size_word;
    uint32_t addr_word;
    uint32_t size;
    uint32_t addr;
    uint32_t fl;

    if (!s || region >= VF_SART_NUM_REGIONS || !addr_pages || !size_pages || !flags)
        return -1;

    size_word = sart_reg(s, VF_SART_REG_SIZE_OFF(region));
    addr_word = sart_reg(s, VF_SART_REG_ADDR_OFF(region));

    switch (s->version) {
    case VF_SART_VERSION_1:
        size = size_word & VF_SART_SIZE_MASK_V1;
        addr = addr_word & VF_SART_ADDR_MASK_V12;
        fl = size_word & ~VF_SART_SIZE_MASK_V1;
        break;
    case VF_SART_VERSION_2:
        size = size_word & VF_SART_SIZE_MASK_V2;
        addr = addr_word & VF_SART_ADDR_MASK_V12;
        fl = size_word & ~VF_SART_SIZE_MASK_V2;
        break;
    case VF_SART_VERSION_3:
        fl = size_word;
        addr = addr_word & VF_SART_ADDR_MASK_V3;
        size = sart_reg(s, VF_SART_REG_SIZE_V3_OFF(region)) & VF_SART_SIZE_MASK_V3;
        break;
    default:
        return -1;
    }

    *addr_pages = addr;
    *size_pages = size;
    *flags = fl;
    return 0;
}

int vf_sart_translate(const vf_sart_v1 *s, uint64_t iova, uint64_t *pa_out,
                      unsigned *region_out) {
    uint64_t page;
    unsigned i;

    if (!s || !pa_out) return -1;
    if (iova >> VF_SART_MAX_VA_BITS) return -1;

    *pa_out = iova & VF_SART_PAGE_MASK;
    if (region_out) *region_out = VF_SART_NUM_REGIONS;

    page = iova >> VF_SART_PAGE_SHIFT;
    for (i = 0; i < VF_SART_NUM_REGIONS; i++) {
        uint32_t addr_pages = 0;
        uint32_t size_pages = 0;
        uint32_t flags = 0;
        if (vf_sart_region_decode(s, i, &addr_pages, &size_pages, &flags) != 0)
            return -1;
        if (flags == 0 || size_pages == 0) continue;
        if (page >= addr_pages && page < (uint64_t)addr_pages + size_pages) {
            if (region_out) *region_out = i;
            return 1;
        }
    }
    return 0;
}
