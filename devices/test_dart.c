#include "dart_v1.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    vf_dart_v1 d;
    uint32_t v = 0;

    assert(!vf_dart_init(&d));
    assert(!vf_dart_read(&d, VF_DART_REG_PARAMS1, 4, &v));
    assert(v == VF_DART_PARAMS1_PAGE_SHIFT_VAL(VF_DART_DEFAULT_PAGE_SHIFT));
    assert(!vf_dart_read(&d, VF_DART_REG_PARAMS2, 4, &v) && v == 0);
    assert(!vf_dart_read(&d, VF_DART_REG_CONFIG, 4, &v) && v == 0);

    assert(!vf_dart_write(&d, VF_DART_REG_PARAMS1, 4, 0x0d000000u));
    assert(!vf_dart_read(&d, VF_DART_REG_PARAMS1, 4, &v) && v == 0x0d000000u);
    assert(!vf_dart_write(&d, VF_DART_REG_PARAMS2, 4, VF_DART_PARAMS2_BYPASS_SUPPORT));
    assert(!vf_dart_read(&d, VF_DART_REG_PARAMS2, 4, &v) && v == VF_DART_PARAMS2_BYPASS_SUPPORT);

    assert(!vf_dart_read(&d, VF_DART_REG_TLB_OP, 4, &v) && v == 0);
    assert(vf_dart_tlb_generation(&d) == 0u);
    assert(!vf_dart_write(&d, VF_DART_REG_TLB_OP, 4, VF_DART_TLB_OP_INVALIDATE));
    assert(!vf_dart_read(&d, VF_DART_REG_TLB_OP, 4, &v) && v == 0);
    assert(vf_dart_tlb_generation(&d) == 1u);
    assert(!vf_dart_write(&d, VF_DART_REG_TLB_OP, 4, VF_DART_TLB_OP_INVALIDATE));
    assert(vf_dart_tlb_generation(&d) == 2u);

    assert(!vf_dart_simulate_fault(&d, 3u, 0x5u));
    assert(vf_dart_irq_pending(&d));
    assert(!vf_dart_read(&d, VF_DART_REG_ERROR_STATUS, 4, &v)
           && v == (VF_DART_ERROR_FLAG | (3u << 24) | 0x5u));
    assert(!vf_dart_write(&d, VF_DART_REG_ERROR_STATUS, 4, VF_DART_ERROR_FLAG));
    assert(!vf_dart_irq_pending(&d));
    assert(!vf_dart_read(&d, VF_DART_REG_ERROR_STATUS, 4, &v) && v == ((3u << 24) | 0x5u));
    assert(vf_dart_simulate_fault(&d, 16u, 0x1u) == -1);

    assert(!vf_dart_write(&d, VF_DART_REG_TCR(1), 4, VF_DART_TCR_TXEN));
    assert(!vf_dart_read(&d, VF_DART_REG_TCR(1), 4, &v) && v == VF_DART_TCR_TXEN);
    assert(!vf_dart_write(&d, VF_DART_REG_TTBR(1, 0), 4, VF_DART_TTBR_VALID | 0x123u));
    assert(!vf_dart_read(&d, VF_DART_REG_TTBR(1, 0), 4, &v) && v == (VF_DART_TTBR_VALID | 0x123u));

    assert(!vf_dart_write(&d, VF_DART_REG_CONFIG, 4, VF_DART_CONFIG_LOCK));
    assert(!vf_dart_read(&d, VF_DART_REG_CONFIG, 4, &v) && v == VF_DART_CONFIG_LOCK);

    assert(vf_dart_write(&d, VF_DART_REG_PARAMS1, 4, 0) == -1);
    assert(!vf_dart_write(&d, VF_DART_REG_TLB_OP, 4, VF_DART_TLB_OP_INVALIDATE));
    assert(vf_dart_tlb_generation(&d) == 3u);
    assert(!vf_dart_write(&d, VF_DART_REG_ERROR_STATUS, 4, VF_DART_ERROR_FLAG));
    assert(!vf_dart_read(&d, VF_DART_REG_ERROR_STATUS, 4, &v) && v == ((3u << 24) | 0x5u));

    assert(vf_dart_read(&d, VF_DART_REG_TCR(16), 4, &v) == -1);
    assert(vf_dart_write(&d, VF_DART_REG_TCR(16), 4, 0) == -1);
    assert(vf_dart_read(&d, 0x200u + 16u * 16u, 4, &v) == -1);

    assert(vf_dart_read(&d, VF_DART_REG_PARAMS1, 8, &v) == -1);
    assert(vf_dart_write(&d, VF_DART_REG_PARAMS1, 2, 0) == -1);
    assert(vf_dart_read(&d, VF_DART_REG_PARAMS1 + 2u, 4, &v) == -1);
    assert(vf_dart_read(&d, VF_DART_INSTANCE_SIZE, 4, &v) == -1);
    assert(vf_dart_write(&d, VF_DART_INSTANCE_SIZE - 2u, 4, 0) == -1);

    puts("PASS DART v1 PARAMS/TLB-gen/ERROR-IRQ/TCR/TTBR stub");
    return 0;
}
