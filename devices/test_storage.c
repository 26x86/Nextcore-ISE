#include "storage_v1.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    vf_storage_v1 s;
    uint64_t v = 0;

    assert(!vf_storage_init(&s));
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_STATUS, 64, &v) && v == 0);
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_BLOCK_COUNT, 64, &v) && v == 0);
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_GENERATION, 64, &v) && v == 0);
    assert(!vf_storage_irq_pending(&s));

    assert(vf_storage_attach(&s, 0, 0) == -1);
    assert(!vf_storage_attach(&s, 2, 0));
    assert(vf_storage_irq_pending(&s));
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_STATUS, 64, &v) &&
           v == VF_STORAGE_STATUS_ATTACHED);
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_BLOCK_COUNT, 64, &v) && v == 2);
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_GENERATION, 64, &v) && v == 1);

    assert(!vf_storage_attach(&s, 4, 1));
    assert(vf_storage_irq_pending(&s));
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_STATUS, 64, &v) &&
           v == (VF_STORAGE_STATUS_ATTACHED | VF_STORAGE_STATUS_READ_ONLY));
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_BLOCK_COUNT, 64, &v) && v == 4);
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_GENERATION, 64, &v) && v == 2);

    assert(!vf_storage_detach(&s));
    assert(!vf_storage_irq_pending(&s));
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_STATUS, 64, &v) && v == 0);
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_BLOCK_COUNT, 64, &v) && v == 0);
    assert(!vf_storage_read(&s, VF_STORAGE_MMIO_GENERATION, 64, &v) && v == 2);

    assert(vf_storage_write(&s, VF_STORAGE_MMIO_STATUS, 64, 1) == -1);
    assert(vf_storage_read(&s, VF_STORAGE_MMIO_STATUS, 32, &v) == -1);
    assert(vf_storage_read(&s, 0x018u, 64, &v) == -1);

    puts("PASS STORAGE v1 attach/generation pending + detach");
    return 0;
}
