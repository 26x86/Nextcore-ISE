#include "timer_v1.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    vf_timer_v1 t;
    uint64_t v = 0;

    assert(!vf_timer_init(&t));
    assert(!vf_timer_read(&t, VF_TIMER_MMIO_FREQUENCY, 64, &v) &&
           v == VF_TIMER_FREQUENCY_HZ);
    assert(!vf_timer_read(&t, VF_TIMER_MMIO_COUNTER, 64, &v) && v == 0);
    assert(!vf_timer_read(&t, VF_TIMER_MMIO_COMPARE, 64, &v) && v == 0);
    assert(!vf_timer_read(&t, VF_TIMER_MMIO_CTL, 32, &v) && v == 0);
    assert(!vf_timer_irq_pending(&t));

    assert(!vf_timer_write(&t, VF_TIMER_MMIO_COMPARE, 64, 5));
    assert(!vf_timer_write(&t, VF_TIMER_MMIO_CTL, 32, VF_M1_TIMER_CTL_ENABLE));
    assert(!vf_timer_irq_pending(&t));

    assert(!vf_timer_advance(&t, 4));
    assert(!vf_timer_irq_pending(&t));
    assert(!vf_timer_advance(&t, 1));
    assert(vf_timer_irq_pending(&t));
    assert(!vf_timer_read(&t, VF_TIMER_MMIO_COUNTER, 64, &v) && v == 5);

    assert(!vf_timer_write(&t, VF_TIMER_MMIO_CTL, 32,
                           VF_M1_TIMER_CTL_ENABLE | VF_M1_TIMER_CTL_MASKED));
    assert(!vf_timer_irq_pending(&t));
    assert(!vf_timer_write(&t, VF_TIMER_MMIO_CTL, 32, VF_M1_TIMER_CTL_ENABLE));
    assert(vf_timer_irq_pending(&t));

    assert(vf_timer_write(&t, VF_TIMER_MMIO_COUNTER, 64, 1) == -1);
    assert(vf_timer_write(&t, VF_TIMER_MMIO_FREQUENCY, 64, 1) == -1);
    assert(vf_timer_write(&t, VF_TIMER_MMIO_CTL, 32, 0x4) == -1);
    assert(vf_timer_read(&t, VF_TIMER_MMIO_CTL, 64, &v) == -1);
    assert(vf_timer_read(&t, 0x020u, 64, &v) == -1);

    puts("PASS TIMER v1 counter/compare/ctl pending + advance");
    return 0;
}
