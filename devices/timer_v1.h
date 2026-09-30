/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_TIMER_V1_H
#define VENFIRE_TIMER_V1_H
#include <stdint.h>

/*
 * Graph-local M1 timer window stub.
 * Aligns with Rust M1Timer / TIMER_SOURCE (0) MMIO at M1_LOGICAL_TIMER_BASE.
 * Not an Apple physical timer block or DT FIQ claim.
 */

#define VF_TIMER_FREQUENCY_HZ      UINT64_C(24000000)

/* Graph-local MMIO offsets (not Apple CNTP/CNTV sysreg addresses). */
#define VF_TIMER_MMIO_COUNTER      0x000u
#define VF_TIMER_MMIO_COMPARE      0x008u
#define VF_TIMER_MMIO_CTL          0x010u
#define VF_TIMER_MMIO_FREQUENCY    0x018u

/* Distinct from architectural VF_TIMER_CTL_* in jit.h (CNTP/CNTV). */
#define VF_M1_TIMER_CTL_ENABLE     (1u << 0)
#define VF_M1_TIMER_CTL_MASKED     (1u << 1)
#define VF_M1_TIMER_CTL_WRITABLE_MASK \
    (VF_M1_TIMER_CTL_ENABLE | VF_M1_TIMER_CTL_MASKED)

typedef struct {
    uint64_t frequency;
    uint64_t counter;
    uint64_t compare;
    uint32_t ctl;
} vf_timer_v1;

int vf_timer_init(vf_timer_v1 *t);
int vf_timer_advance(vf_timer_v1 *t, uint64_t ticks);
int vf_timer_irq_pending(const vf_timer_v1 *t);
int vf_timer_read(vf_timer_v1 *t, uint32_t offset, unsigned width_bits,
                  uint64_t *value);
int vf_timer_write(vf_timer_v1 *t, uint32_t offset, unsigned width_bits,
                   uint64_t value);

#endif
