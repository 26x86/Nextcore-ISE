#include "sep_mailbox_v1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t pack(uint8_t ep, uint8_t tag, uint8_t op, uint8_t param,
                     uint32_t data) {
    vf_sep_mbox_msg m = {ep, tag, op, param, data};
    uint64_t raw = 0;
    memcpy(&raw, &m, sizeof(m));
    return raw;
}

static void unpack(uint64_t raw, vf_sep_mbox_msg *m) {
    memcpy(m, &raw, sizeof(*m));
}

int main(void) {
    vf_sep_mailbox_v1 s;
    uint64_t v = 0;
    vf_sep_mbox_msg r;

    assert(!vf_sep_mailbox_init(&s));
    assert(vf_sep_mailbox_status(&s) == VF_SEP_STATUS_BOOTSTRAP);
    assert(vf_sep_mailbox_irq_pending(&s));
    assert(!vf_sep_mailbox_read(&s, VF_SEP_MMIO_TIER, 32, &v) &&
           v == VF_SEP_TIER_L1_MAILBOX);
    assert(!vf_sep_mailbox_read(&s, VF_SEP_MMIO_FLAGS, 32, &v) &&
           (v & VF_SEP_FLAG_PRESENT) && (v & VF_SEP_FLAG_REPLY_PENDING));
    assert(!vf_sep_mailbox_read(&s, VF_SEP_MMIO_MSG_OUT, 64, &v));
    unpack(v, &r);
    assert(r.ep == VF_SEP_EP_BOOTSTRAP);
    assert(r.op == VF_SEP_BOOTSTRAP_OP_ANNOUNCE);
    assert(r.data == VF_SEP_STATUS_BOOTSTRAP);
    assert(!vf_sep_mailbox_irq_pending(&s));

    assert(!vf_sep_mailbox_write(
        &s, VF_SEP_MMIO_MSG_IN, 64,
        pack(VF_SEP_EP_BOOTSTRAP, 0x11, VF_SEP_BOOTSTRAP_OP_PING, 0, 0)));
    assert(vf_sep_mailbox_irq_pending(&s));
    assert(!vf_sep_mailbox_read(&s, VF_SEP_MMIO_MSG_OUT, 64, &v));
    unpack(v, &r);
    assert(r.tag == 0x11 && r.op == VF_SEP_BOOTSTRAP_OP_PING_ACK);

    assert(!vf_sep_mailbox_write(
        &s, VF_SEP_MMIO_MSG_IN, 64,
        pack(VF_SEP_EP_BOOTSTRAP, 0x22, VF_SEP_BOOTSTRAP_OP_GET_STATUS, 0, 0)));
    assert(!vf_sep_mailbox_read(&s, VF_SEP_MMIO_MSG_OUT, 64, &v));
    unpack(v, &r);
    assert(r.tag == 0x22 && r.op == VF_SEP_BOOTSTRAP_OP_STATUS_REPLY);
    assert(r.data == VF_SEP_STATUS_BOOTSTRAP);

    /* BOOT_IMG4-class op must not promote ACTIVE or accept firmware. */
    assert(!vf_sep_mailbox_write(
        &s, VF_SEP_MMIO_MSG_IN, 64,
        pack(VF_SEP_EP_BOOTSTRAP, 0x33, 6 /* BOOT_IMG4 ref */, 0, 0)));
    assert(!vf_sep_mailbox_read(&s, VF_SEP_MMIO_MSG_OUT, 64, &v));
    unpack(v, &r);
    assert(r.op == VF_SEP_BOOTSTRAP_OP_L1_REJECT);
    assert(r.param == 6);
    assert(vf_sep_mailbox_status(&s) == VF_SEP_STATUS_BOOTSTRAP);

    assert(vf_sep_mailbox_write(&s, VF_SEP_MMIO_STATUS, 32, 1) == -1);
    assert(vf_sep_mailbox_read(&s, VF_SEP_MMIO_MSG_IN, 64, &v) == -1);
    assert(vf_sep_mailbox_read(&s, VF_SEP_MMIO_SIZE, 32, &v) == -1);

    puts("PASS SEP mailbox L1: present/bootstrap, PING/GET_STATUS, L1_REJECT, no FW");
    return 0;
}
