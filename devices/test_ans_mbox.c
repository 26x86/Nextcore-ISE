#include "ans_mbox_v1.h"
#include "ans_pci_v1.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    vf_ans_mbox_v1 s;
    vf_ans_pci_v1 pci;
    vf_ans_mbox_msg r;
    uint64_t v = 0;
    uint32_t expect_started =
        VF_ANS_MBOX_FLAG_PRESENT | VF_ANS_MBOX_FLAG_STARTED |
        VF_ANS_MBOX_FLAG_REPLY_PENDING;

    assert(!vf_ans_mbox_init(&s));
    assert(vf_ans_mbox_status(&s) == VF_ANS_MBOX_STATUS_BOOTSTRAP);
    assert(vf_ans_mbox_flags(&s) == VF_ANS_MBOX_FLAG_PRESENT);
    assert(!vf_ans_mbox_started(&s));
    assert(!vf_ans_mbox_a2i_write_seen(&s));
    assert(vf_ans_mbox_last_a2i_write(&s) == 0);
    assert(!vf_ans_mbox_reply_pending(&s));

    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_STATUS, 32, &v) &&
           v == VF_ANS_MBOX_STATUS_BOOTSTRAP);
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_FLAGS, 32, &v) &&
           v == VF_ANS_MBOX_FLAG_PRESENT);
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_TIER, 32, &v) &&
           v == VF_ANS_MBOX_TIER_AKF_REGMAP);

    /* Both directions empty before start; A2I stays empty always. */
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_A2I_QSTAT, 32, &v) &&
           v == VF_ANS_MBOX_Q_EMPTY);
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_QSTAT, 32, &v) &&
           v == VF_ANS_MBOX_Q_EMPTY);
    assert((v & VF_ANS_MBOX_Q_NOT_EMPTY) == 0);

    /* Pre-start A2I WO: accept observability only; no endpoint reply. */
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_A2I_MSG, 64, &v) && v == 0);
    assert(!vf_ans_mbox_write(&s, VF_ANS_MBOX_MMIO_A2I_MSG, 64,
                              vf_ans_mbox_pack_msg(VF_ANS_MBOX_EP_BOOTSTRAP, 1,
                                                   VF_ANS_MBOX_BOOTSTRAP_OP_PING,
                                                   0, 0)));
    assert(vf_ans_mbox_a2i_write_seen(&s) == 1);
    assert(!vf_ans_mbox_reply_pending(&s));
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_QSTAT, 32, &v) &&
           v == VF_ANS_MBOX_Q_EMPTY);
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_A2I_QSTAT, 32, &v) &&
           v == VF_ANS_MBOX_Q_EMPTY);

    /* Unbound start/wakeup: FLAG_STARTED + ANNOUNCE on I2A; status BOOTSTRAP. */
    assert(!vf_ans_mbox_start(&s));
    assert(vf_ans_mbox_started(&s) == 1);
    assert(vf_ans_mbox_reply_pending(&s) == 1);
    assert(vf_ans_mbox_flags(&s) == expect_started);
    assert(vf_ans_mbox_status(&s) == VF_ANS_MBOX_STATUS_BOOTSTRAP);
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_QSTAT, 32, &v) &&
           v == VF_ANS_MBOX_Q_NOT_EMPTY);
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_MSG, 64, &v));
    vf_ans_mbox_unpack_msg(v, &r);
    assert(r.ep == VF_ANS_MBOX_EP_BOOTSTRAP);
    assert(r.op == VF_ANS_MBOX_BOOTSTRAP_OP_ANNOUNCE);
    assert(r.data == VF_ANS_MBOX_STATUS_BOOTSTRAP);
    assert(!vf_ans_mbox_reply_pending(&s));
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_QSTAT, 32, &v) &&
           v == VF_ANS_MBOX_Q_EMPTY);
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_A2I_QSTAT, 32, &v) &&
           v == VF_ANS_MBOX_Q_EMPTY);

    /* Bootstrap PING → PING_ACK round-trip (I2A NOT_EMPTY until drain). */
    assert(!vf_ans_mbox_write(
        &s, VF_ANS_MBOX_MMIO_A2I_MSG, 64,
        vf_ans_mbox_pack_msg(VF_ANS_MBOX_EP_BOOTSTRAP, 0x11,
                             VF_ANS_MBOX_BOOTSTRAP_OP_PING, 0, 0)));
    assert(vf_ans_mbox_reply_pending(&s) == 1);
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_MSG, 64, &v));
    vf_ans_mbox_unpack_msg(v, &r);
    assert(r.ep == VF_ANS_MBOX_EP_BOOTSTRAP && r.tag == 0x11);
    assert(r.op == VF_ANS_MBOX_BOOTSTRAP_OP_PING_ACK);

    /* GET_STATUS → STATUS_REPLY with BOOTSTRAP data. */
    assert(!vf_ans_mbox_write(
        &s, VF_ANS_MBOX_MMIO_A2I_MSG, 64,
        vf_ans_mbox_pack_msg(VF_ANS_MBOX_EP_BOOTSTRAP, 0x22,
                             VF_ANS_MBOX_BOOTSTRAP_OP_GET_STATUS, 0, 0)));
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_MSG, 64, &v));
    vf_ans_mbox_unpack_msg(v, &r);
    assert(r.tag == 0x22 && r.op == VF_ANS_MBOX_BOOTSTRAP_OP_STATUS_REPLY);
    assert(r.data == VF_ANS_MBOX_STATUS_BOOTSTRAP);

    /* Unknown EP / FW op → L1_REJECT (fail-closed). */
    assert(!vf_ans_mbox_write(
        &s, VF_ANS_MBOX_MMIO_A2I_MSG, 64,
        vf_ans_mbox_pack_msg(1 /* non-bootstrap */, 0x33, 1, 0, 0)));
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_MSG, 64, &v));
    vf_ans_mbox_unpack_msg(v, &r);
    assert(r.ep == 1 && r.op == VF_ANS_MBOX_BOOTSTRAP_OP_L1_REJECT);
    assert(!vf_ans_mbox_write(
        &s, VF_ANS_MBOX_MMIO_A2I_MSG, 64,
        vf_ans_mbox_pack_msg(VF_ANS_MBOX_EP_BOOTSTRAP, 0x44, 6, 0, 0)));
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_MSG, 64, &v));
    vf_ans_mbox_unpack_msg(v, &r);
    assert(r.op == VF_ANS_MBOX_BOOTSTRAP_OP_L1_REJECT && r.param == 6);

    assert(!vf_ans_mbox_wakeup(&s)); /* idempotent alias; re-announce */
    assert(vf_ans_mbox_started(&s) == 1);
    assert(vf_ans_mbox_reply_pending(&s) == 1);
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_MSG, 64, &v));
    vf_ans_mbox_unpack_msg(v, &r);
    assert(r.op == VF_ANS_MBOX_BOOTSTRAP_OP_ANNOUNCE);

    /* Bound start latches PCI Memory|BusMaster; BusMaster clear drops STARTED. */
    assert(!vf_ans_mbox_init(&s));
    assert(!vf_ans_pci_init(&pci));
    assert(!vf_ans_mbox_bind_pci(&s, &pci));
    assert(!vf_ans_pci_bus_master_enabled(&pci));
    assert(!vf_ans_mbox_started(&s));
    assert(!vf_ans_mbox_start(&s));
    assert(vf_ans_mbox_started(&s) == 1);
    assert(vf_ans_pci_bus_master_enabled(&pci) == 1);
    assert(vf_ans_pci_command(&pci) == VF_ANS_PCI_COMMAND_ENABLE_MASK);
    assert(vf_ans_mbox_status(&s) == VF_ANS_MBOX_STATUS_BOOTSTRAP);
    assert(vf_ans_pci_status(&pci) == VF_ANS_PCI_STATUS_BOOTSTRAP);
    assert(vf_ans_mbox_reply_pending(&s) == 1);
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_MSG, 64, &v)); /* drain */

    /* BusMaster alone does not invent start. */
    assert(!vf_ans_mbox_init(&s));
    assert(!vf_ans_pci_init(&pci));
    assert(!vf_ans_mbox_bind_pci(&s, &pci));
    assert(!vf_ans_pci_enable_memory_bus_master(&pci));
    assert(vf_ans_pci_bus_master_enabled(&pci) == 1);
    assert(!vf_ans_mbox_started(&s));

    /* Linked: clear BusMaster → started fails closed on next observe. */
    assert(!vf_ans_mbox_start(&s));
    assert(vf_ans_mbox_started(&s) == 1);
    assert(!vf_ans_pci_write(&pci, VF_ANS_PCI_MMIO_CONFIG, 64, 0));
    assert(!vf_ans_pci_bus_master_enabled(&pci));
    assert(!vf_ans_mbox_started(&s));
    assert(vf_ans_mbox_flags(&s) ==
           (VF_ANS_MBOX_FLAG_PRESENT | VF_ANS_MBOX_FLAG_REPLY_PENDING));
    /* Without STARTED, A2I endpoint ops do not post replies. */
    assert(!vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_I2A_MSG, 64, &v)); /* drain */
    assert(!vf_ans_mbox_write(
        &s, VF_ANS_MBOX_MMIO_A2I_MSG, 64,
        vf_ans_mbox_pack_msg(VF_ANS_MBOX_EP_BOOTSTRAP, 1,
                             VF_ANS_MBOX_BOOTSTRAP_OP_PING, 0, 0)));
    assert(!vf_ans_mbox_reply_pending(&s));

    /* Fail-closed: wrong width, RO writes, OOB, NULL. */
    assert(vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_A2I_MSG, 32, &v) == -1);
    assert(vf_ans_mbox_write(&s, VF_ANS_MBOX_MMIO_A2I_MSG, 32, 1) == -1);
    assert(vf_ans_mbox_write(&s, VF_ANS_MBOX_MMIO_STATUS, 32, 1) == -1);
    assert(vf_ans_mbox_write(&s, VF_ANS_MBOX_MMIO_A2I_QSTAT, 32, 1) == -1);
    assert(vf_ans_mbox_write(&s, VF_ANS_MBOX_MMIO_I2A_QSTAT, 32, 1) == -1);
    assert(vf_ans_mbox_write(&s, VF_ANS_MBOX_MMIO_I2A_MSG, 64, 1) == -1);
    assert(vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_SIZE, 32, &v) == -1);
    assert(vf_ans_mbox_read(&s, 0x00Cu, 32, &v) == -1);
    assert(vf_ans_mbox_init(NULL) == -1);
    assert(vf_ans_mbox_start(NULL) == -1);
    assert(vf_ans_mbox_wakeup(NULL) == -1);
    assert(vf_ans_mbox_bind_pci(NULL, &pci) == -1);
    assert(vf_ans_mbox_status(NULL) == VF_ANS_MBOX_STATUS_ABSENT);
    assert(vf_ans_mbox_flags(NULL) == 0);
    assert(vf_ans_mbox_started(NULL) == 0);
    assert(vf_ans_mbox_read(NULL, VF_ANS_MBOX_MMIO_A2I_MSG, 64, &v) == -1);
    assert(vf_ans_mbox_read(&s, VF_ANS_MBOX_MMIO_A2I_MSG, 64, NULL) == -1);
    assert(vf_ans_mbox_write(NULL, VF_ANS_MBOX_MMIO_A2I_MSG, 64, 1) == -1);
    assert(vf_ans_pci_enable_memory_bus_master(NULL) == -1);

    puts("PASS ANS mbox v1: present/bootstrap, start/wakeup+BusMaster latch, "
         "bootstrap EP PING/GET_STATUS+ANNOUNCE, A2I EMPTY/I2A reply, no MSI/DMA");
    return 0;
}
