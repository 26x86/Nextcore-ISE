#include "adp_display_v1.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    vf_adp_display_v1 s;
    uint64_t v = 0;

    assert(!vf_adp_display_init(&s));
    assert(!vf_adp_display_read(&s, VF_ADP_MMIO_TIER, 32, &v) &&
           v == VF_ADP_TIER_L1_FRAMEBUFFER);
    assert(!vf_adp_display_read(&s, VF_ADP_MMIO_CTRL, 32, &v) &&
           (v & VF_ADP_FLAG_PRESENT) && !(v & VF_ADP_FLAG_CONFIGURED));
    assert(!vf_adp_display_irq_pending(&s));
    assert(vf_adp_display_present(&s) == -1);

    assert(!vf_adp_display_configure(&s, 0x1000, 1920u * 4u * 2u, 1920, 2,
                                     1920u * 4u));
    assert(!vf_adp_display_read(&s, VF_ADP_MMIO_WIDTH, 32, &v) && v == 1920);
    assert(!vf_adp_display_read(&s, VF_ADP_MMIO_HEIGHT, 32, &v) && v == 2);
    assert(!vf_adp_display_read(&s, VF_ADP_MMIO_STRIDE, 32, &v) &&
           v == 1920u * 4u);
    assert(!vf_adp_display_read(&s, VF_ADP_MMIO_GENERATION, 32, &v) && v == 1);
    assert(!vf_adp_display_read(&s, VF_ADP_MMIO_CTRL, 32, &v) &&
           (v & VF_ADP_FLAG_CONFIGURED));

    assert(!vf_adp_display_write(&s, VF_ADP_MMIO_CTRL, 32, VF_ADP_CTRL_PRESENT));
    assert(vf_adp_display_irq_pending(&s));
    assert(!vf_adp_display_read(&s, VF_ADP_MMIO_CTRL, 32, &v));
    assert(v & VF_ADP_FLAG_ENABLED);
    assert(v & VF_ADP_FLAG_PRESENTED);
    assert(v & VF_ADP_FLAG_VBLANK_PENDING);
    assert(!vf_adp_display_read(&s, VF_ADP_MMIO_FRAME_COUNT, 32, &v) && v == 1);

    assert(!vf_adp_display_write(&s, VF_ADP_MMIO_VBLANK_ACK, 32, 1));
    assert(!vf_adp_display_irq_pending(&s));
    assert(!vf_adp_display_read(&s, VF_ADP_MMIO_CTRL, 32, &v));
    assert(!(v & VF_ADP_FLAG_VBLANK_PENDING));

    assert(vf_adp_display_configure(&s, 0x1001, 16, 4, 4, 16) == -1);
    assert(vf_adp_display_write(&s, VF_ADP_MMIO_CTRL, 32, 2) == -1);
    assert(vf_adp_display_write(&s, VF_ADP_MMIO_WIDTH, 32, 1) == -1);
    assert(vf_adp_display_read(&s, VF_ADP_MMIO_SIZE, 32, &v) == -1);

    puts("PASS ADP display L1: present/configure/present-vblank/W1C-ack, no GPU");
    return 0;
}
