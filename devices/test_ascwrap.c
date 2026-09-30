#include "ascwrap_v1.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    vf_ascwrap_v1 s;
    vf_ans_mbox_v1 mbox;
    vf_ans_pci_v1 pci;
    uint64_t v = 0;

    assert(!vf_ascwrap_init(&s));
    assert(vf_ascwrap_status(&s) == VF_ASCWRAP_STATUS_BOOTSTRAP);
    assert(vf_ascwrap_flags(&s) == VF_ASCWRAP_FLAG_PRESENT);
    assert(!vf_ascwrap_ready(&s));
    assert(!vf_ascwrap_core_write_seen(&s));
    assert(vf_ascwrap_last_core_write(&s) == 0);

    assert(!vf_ascwrap_read(&s, VF_ASCWRAP_MMIO_STATUS, 32, &v) &&
           v == VF_ASCWRAP_STATUS_BOOTSTRAP);
    assert(!vf_ascwrap_read(&s, VF_ASCWRAP_MMIO_FLAGS, 32, &v) &&
           v == VF_ASCWRAP_FLAG_PRESENT);
    assert(!vf_ascwrap_read(&s, VF_ASCWRAP_MMIO_TIER, 32, &v) &&
           v == VF_ASCWRAP_TIER_CORE_MAP);

    /* 8-byte core: RO → 0 before and after WO. */
    assert(!vf_ascwrap_read(&s, VF_ASCWRAP_MMIO_CORE, 64, &v) && v == 0);
    assert(!vf_ascwrap_write(&s, VF_ASCWRAP_MMIO_CORE, 64, 0xdeadbeefcafebabeull));
    assert(vf_ascwrap_core_write_seen(&s) == 1);
    assert(vf_ascwrap_last_core_write(&s) == 0xdeadbeefcafebabeull);
    assert(!vf_ascwrap_read(&s, VF_ASCWRAP_MMIO_CORE, 64, &v) && v == 0);
    /* Core WO never invents READY without mbox bind. */
    assert(!vf_ascwrap_ready(&s));
    assert(vf_ascwrap_flags(&s) == VF_ASCWRAP_FLAG_PRESENT);

    /* Status never advances to ACTIVE (would imply real ASC/IOP). */
    assert(vf_ascwrap_status(&s) == VF_ASCWRAP_STATUS_BOOTSTRAP);

    /* ASCWrap↔mbox: READY mirrors STARTED; unbound clears READY. */
    assert(!vf_ans_mbox_init(&mbox));
    assert(!vf_ascwrap_bind_mbox(&s, &mbox));
    assert(!vf_ascwrap_ready(&s));
    assert(vf_ascwrap_flags(&s) == VF_ASCWRAP_FLAG_PRESENT);
    assert(!vf_ans_mbox_start(&mbox));
    assert(vf_ans_mbox_started(&mbox) == 1);
    assert(vf_ascwrap_ready(&s) == 1);
    assert(vf_ascwrap_flags(&s) ==
           (VF_ASCWRAP_FLAG_PRESENT | VF_ASCWRAP_FLAG_READY));
    assert(!vf_ascwrap_read(&s, VF_ASCWRAP_MMIO_FLAGS, 32, &v) &&
           v == (VF_ASCWRAP_FLAG_PRESENT | VF_ASCWRAP_FLAG_READY));
    assert(!vf_ascwrap_bind_mbox(&s, NULL));
    assert(!vf_ascwrap_ready(&s));
    assert(vf_ascwrap_flags(&s) == VF_ASCWRAP_FLAG_PRESENT);

    /* Linked PCI BusMaster drop → mbox STARTED clear → ASCWrap READY clear. */
    assert(!vf_ascwrap_init(&s));
    assert(!vf_ans_mbox_init(&mbox));
    assert(!vf_ans_pci_init(&pci));
    assert(!vf_ans_mbox_bind_pci(&mbox, &pci));
    assert(!vf_ascwrap_bind_mbox(&s, &mbox));
    assert(!vf_ans_mbox_start(&mbox));
    assert(vf_ascwrap_ready(&s) == 1);
    assert(!vf_ans_pci_write(&pci, VF_ANS_PCI_MMIO_CONFIG, 64, 0));
    assert(!vf_ans_mbox_started(&mbox));
    assert(!vf_ascwrap_ready(&s));
    assert(vf_ascwrap_flags(&s) == VF_ASCWRAP_FLAG_PRESENT);

    /* Fail-closed: wrong width, RO writes, OOB, NULL. */
    assert(vf_ascwrap_read(&s, VF_ASCWRAP_MMIO_CORE, 32, &v) == -1);
    assert(vf_ascwrap_write(&s, VF_ASCWRAP_MMIO_CORE, 32, 1) == -1);
    assert(vf_ascwrap_write(&s, VF_ASCWRAP_MMIO_STATUS, 32, 1) == -1);
    assert(vf_ascwrap_write(&s, VF_ASCWRAP_MMIO_FLAGS, 32, 1) == -1);
    assert(vf_ascwrap_read(&s, VF_ASCWRAP_MMIO_SIZE, 32, &v) == -1);
    assert(vf_ascwrap_read(&s, 0x00Cu, 32, &v) == -1);
    assert(vf_ascwrap_init(NULL) == -1);
    assert(vf_ascwrap_bind_mbox(NULL, &mbox) == -1);
    assert(vf_ascwrap_status(NULL) == VF_ASCWRAP_STATUS_ABSENT);
    assert(vf_ascwrap_flags(NULL) == 0);
    assert(vf_ascwrap_ready(NULL) == 0);
    assert(vf_ascwrap_read(NULL, VF_ASCWRAP_MMIO_CORE, 64, &v) == -1);
    assert(vf_ascwrap_read(&s, VF_ASCWRAP_MMIO_CORE, 64, NULL) == -1);
    assert(vf_ascwrap_write(NULL, VF_ASCWRAP_MMIO_CORE, 64, 1) == -1);

    puts("PASS ASCWrap v1: present/bootstrap, 8-byte core RO/WO→0, "
         "mbox STARTED→READY bind, no MSI/DMA");
    return 0;
}
