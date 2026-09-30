#include "ans_autoboot_v1.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    vf_ans_autoboot_v1 s;
    vf_ascwrap_v1 wrap;
    vf_ans_mbox_v1 mbox;
    vf_ans_pci_v1 pci;
    uint64_t v = 0;

    assert(!vf_ans_autoboot_init(&s));
    assert(vf_ans_autoboot_status(&s) == VF_ANS_AUTOBOOT_STATUS_BOOTSTRAP);
    assert(vf_ans_autoboot_flags(&s) == VF_ANS_AUTOBOOT_FLAG_PRESENT);
    assert(!vf_ans_autoboot_armed(&s));
    assert(!vf_ans_autoboot_write_seen(&s));
    assert(vf_ans_autoboot_last_write(&s) == 0);

    assert(!vf_ans_autoboot_read(&s, VF_ANS_AUTOBOOT_MMIO_STATUS, 32, &v) &&
           v == VF_ANS_AUTOBOOT_STATUS_BOOTSTRAP);
    assert(!vf_ans_autoboot_read(&s, VF_ANS_AUTOBOOT_MMIO_FLAGS, 32, &v) &&
           v == VF_ANS_AUTOBOOT_FLAG_PRESENT);
    assert(!vf_ans_autoboot_read(&s, VF_ANS_AUTOBOOT_MMIO_TIER, 32, &v) &&
           v == VF_ANS_AUTOBOOT_TIER_REGMAP);

    /* Autoboot map unbound: RO → 0 before and after WO. */
    assert(!vf_ans_autoboot_read(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 64, &v) &&
           v == 0);
    assert(!vf_ans_autoboot_write(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 64,
                                  0xdeadbeefcafebabeull));
    assert(vf_ans_autoboot_write_seen(&s) == 1);
    assert(vf_ans_autoboot_last_write(&s) == 0xdeadbeefcafebabeull);
    assert(!vf_ans_autoboot_read(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 64, &v) &&
           v == 0);
    /* Map WO never invents ARMED without ASCWrap bind. */
    assert(!vf_ans_autoboot_armed(&s));
    assert(vf_ans_autoboot_flags(&s) == VF_ANS_AUTOBOOT_FLAG_PRESENT);

    /* Status never advances to ACTIVE (would imply real IOP firmware load). */
    assert(vf_ans_autoboot_status(&s) == VF_ANS_AUTOBOOT_STATUS_BOOTSTRAP);

    /* Autoboot↔ASCWrap: ARMED mirrors READY; bound WO fail-closes without it. */
    assert(!vf_ascwrap_init(&wrap));
    assert(!vf_ans_mbox_init(&mbox));
    assert(!vf_ans_autoboot_init(&s));
    assert(!vf_ascwrap_bind_mbox(&wrap, &mbox));
    assert(!vf_ans_autoboot_bind_ascwrap(&s, &wrap));
    assert(!vf_ans_autoboot_armed(&s));
    assert(vf_ans_autoboot_flags(&s) == VF_ANS_AUTOBOOT_FLAG_PRESENT);
    assert(vf_ans_autoboot_write(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 64, 1) == -1);
    assert(!vf_ans_autoboot_write_seen(&s));
    assert(!vf_ans_mbox_start(&mbox));
    assert(vf_ans_mbox_started(&mbox) == 1);
    assert(vf_ascwrap_ready(&wrap) == 1);
    assert(vf_ans_autoboot_armed(&s) == 1);
    assert(vf_ans_autoboot_flags(&s) ==
           (VF_ANS_AUTOBOOT_FLAG_PRESENT | VF_ANS_AUTOBOOT_FLAG_ARMED));
    assert(!vf_ans_autoboot_read(&s, VF_ANS_AUTOBOOT_MMIO_FLAGS, 32, &v) &&
           v == (VF_ANS_AUTOBOOT_FLAG_PRESENT | VF_ANS_AUTOBOOT_FLAG_ARMED));
    assert(!vf_ans_autoboot_write(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 64,
                                  0x1111222233334444ull));
    assert(vf_ans_autoboot_write_seen(&s) == 1);
    assert(vf_ans_autoboot_last_write(&s) == 0x1111222233334444ull);
    assert(!vf_ans_autoboot_bind_ascwrap(&s, NULL));
    assert(!vf_ans_autoboot_armed(&s));
    assert(vf_ans_autoboot_flags(&s) == VF_ANS_AUTOBOOT_FLAG_PRESENT);
    /* Unbound again: WO accepted without inventing ARMED. */
    assert(!vf_ans_autoboot_write(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 64, 2));
    assert(!vf_ans_autoboot_armed(&s));

    /* Linked PCI BusMaster drop → STARTED/READY clear → ARMED clear → WO fail. */
    assert(!vf_ans_autoboot_init(&s));
    assert(!vf_ascwrap_init(&wrap));
    assert(!vf_ans_mbox_init(&mbox));
    assert(!vf_ans_pci_init(&pci));
    assert(!vf_ans_mbox_bind_pci(&mbox, &pci));
    assert(!vf_ascwrap_bind_mbox(&wrap, &mbox));
    assert(!vf_ans_autoboot_bind_ascwrap(&s, &wrap));
    assert(!vf_ans_mbox_start(&mbox));
    assert(vf_ans_autoboot_armed(&s) == 1);
    assert(!vf_ans_autoboot_write(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 64, 3));
    assert(!vf_ans_pci_write(&pci, VF_ANS_PCI_MMIO_CONFIG, 64, 0));
    assert(!vf_ans_mbox_started(&mbox));
    assert(!vf_ascwrap_ready(&wrap));
    assert(!vf_ans_autoboot_armed(&s));
    assert(vf_ans_autoboot_flags(&s) == VF_ANS_AUTOBOOT_FLAG_PRESENT);
    assert(vf_ans_autoboot_write(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 64, 4) == -1);

    /* Fail-closed: wrong width, RO writes, OOB, NULL. */
    assert(vf_ans_autoboot_read(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 32, &v) == -1);
    assert(vf_ans_autoboot_write(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 32, 1) == -1);
    assert(vf_ans_autoboot_write(&s, VF_ANS_AUTOBOOT_MMIO_STATUS, 32, 1) == -1);
    assert(vf_ans_autoboot_write(&s, VF_ANS_AUTOBOOT_MMIO_FLAGS, 32, 1) == -1);
    assert(vf_ans_autoboot_read(&s, VF_ANS_AUTOBOOT_MMIO_SIZE, 32, &v) == -1);
    assert(vf_ans_autoboot_read(&s, 0x00Cu, 32, &v) == -1);
    assert(vf_ans_autoboot_init(NULL) == -1);
    assert(vf_ans_autoboot_bind_ascwrap(NULL, &wrap) == -1);
    assert(vf_ans_autoboot_status(NULL) == VF_ANS_AUTOBOOT_STATUS_ABSENT);
    assert(vf_ans_autoboot_flags(NULL) == 0);
    assert(vf_ans_autoboot_armed(NULL) == 0);
    assert(vf_ans_autoboot_read(NULL, VF_ANS_AUTOBOOT_MMIO_MAP, 64, &v) == -1);
    assert(vf_ans_autoboot_read(&s, VF_ANS_AUTOBOOT_MMIO_MAP, 64, NULL) == -1);
    assert(vf_ans_autoboot_write(NULL, VF_ANS_AUTOBOOT_MMIO_MAP, 64, 1) == -1);

    puts("PASS ANS autoboot v1: present/bootstrap, RO/WO→0, "
         "ASCWrap READY→ARMED bind, no MSI/DMA");
    return 0;
}
