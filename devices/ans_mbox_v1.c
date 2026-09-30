/* 26x86 first-party ANS akfRegMap mailbox stub for composite region index 0.
 * See ANS_MBOX.md. Behaviour: present/bootstrap announce; A2I always EMPTY
 * (writes consumed by stub); I2A NOT_EMPTY while bootstrap reply pending;
 * start/wakeup → FLAG_STARTED + ANNOUNCE (+ optional PCI Memory|BusMaster).
 * Status never reaches ACTIVE. No MSI, DT IRQ, AIC, or command DMA.
 */
#include "ans_mbox_v1.h"

#ifdef VF_EFI_BUILD
static void *ans_mbox_memset(void *dst, int c, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
#define memset(d, c, n) ans_mbox_memset((d), (c), (unsigned)(n))
#else
#include <string.h>
#endif

uint64_t vf_ans_mbox_pack_msg(uint8_t ep, uint8_t tag, uint8_t op,
                              uint8_t param, uint32_t data) {
    return ((uint64_t)ep) | ((uint64_t)tag << 8) | ((uint64_t)op << 16) |
           ((uint64_t)param << 24) | ((uint64_t)data << 32);
}

void vf_ans_mbox_unpack_msg(uint64_t raw, vf_ans_mbox_msg *out) {
    if (!out) return;
    out->ep = (uint8_t)(raw & 0xffu);
    out->tag = (uint8_t)((raw >> 8) & 0xffu);
    out->op = (uint8_t)((raw >> 16) & 0xffu);
    out->param = (uint8_t)((raw >> 24) & 0xffu);
    out->data = (uint32_t)(raw >> 32);
}

/* Drop FLAG_STARTED when a linked PCI lost BusMaster (fail-closed honesty). */
static void ans_mbox_reconcile_started(vf_ans_mbox_v1 *s) {
    if (!s) return;
    if (!(s->flags & VF_ANS_MBOX_FLAG_STARTED)) return;
    if (!s->pci) return;
    if (!vf_ans_pci_bus_master_enabled(s->pci)) {
        s->flags &= ~VF_ANS_MBOX_FLAG_STARTED;
    }
}

static void ans_mbox_post_reply(vf_ans_mbox_v1 *s, uint64_t msg) {
    s->i2a_msg = msg;
    s->reply_pending = 1;
    s->flags |= VF_ANS_MBOX_FLAG_REPLY_PENDING;
}

static void ans_mbox_handle_endpoint(vf_ans_mbox_v1 *s, uint64_t raw) {
    vf_ans_mbox_msg in;
    uint64_t reply;

    vf_ans_mbox_unpack_msg(raw, &in);
    if (in.ep != VF_ANS_MBOX_EP_BOOTSTRAP) {
        reply = vf_ans_mbox_pack_msg(in.ep, in.tag,
                                     VF_ANS_MBOX_BOOTSTRAP_OP_L1_REJECT,
                                     in.op, 0);
        ans_mbox_post_reply(s, reply);
        return;
    }
    switch (in.op) {
    case VF_ANS_MBOX_BOOTSTRAP_OP_PING:
        reply = vf_ans_mbox_pack_msg(VF_ANS_MBOX_EP_BOOTSTRAP, in.tag,
                                     VF_ANS_MBOX_BOOTSTRAP_OP_PING_ACK, 0, 0);
        break;
    case VF_ANS_MBOX_BOOTSTRAP_OP_GET_STATUS:
        reply = vf_ans_mbox_pack_msg(VF_ANS_MBOX_EP_BOOTSTRAP, in.tag,
                                     VF_ANS_MBOX_BOOTSTRAP_OP_STATUS_REPLY, 0,
                                     s->status);
        break;
    default:
        /* Firmware / unknown bootstrap ops fail closed at this stub tier. */
        reply = vf_ans_mbox_pack_msg(VF_ANS_MBOX_EP_BOOTSTRAP, in.tag,
                                     VF_ANS_MBOX_BOOTSTRAP_OP_L1_REJECT,
                                     in.op, 0);
        break;
    }
    ans_mbox_post_reply(s, reply);
}

int vf_ans_mbox_init(vf_ans_mbox_v1 *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    /* Honest announce: akfRegMap present in bootstrap; no IOP FW / ACTIVE. */
    s->status = VF_ANS_MBOX_STATUS_BOOTSTRAP;
    s->flags = VF_ANS_MBOX_FLAG_PRESENT;
    return 0;
}

uint32_t vf_ans_mbox_status(const vf_ans_mbox_v1 *s) {
    return s ? s->status : VF_ANS_MBOX_STATUS_ABSENT;
}

uint32_t vf_ans_mbox_flags(vf_ans_mbox_v1 *s) {
    if (!s) return 0u;
    ans_mbox_reconcile_started(s);
    return s->flags;
}

int vf_ans_mbox_a2i_write_seen(const vf_ans_mbox_v1 *s) {
    return s && s->a2i_write_seen ? 1 : 0;
}

uint64_t vf_ans_mbox_last_a2i_write(const vf_ans_mbox_v1 *s) {
    return s ? s->last_a2i_write : 0u;
}

int vf_ans_mbox_reply_pending(const vf_ans_mbox_v1 *s) {
    return s && s->reply_pending ? 1 : 0;
}

uint64_t vf_ans_mbox_i2a_msg(const vf_ans_mbox_v1 *s) {
    return s ? s->i2a_msg : 0u;
}

int vf_ans_mbox_bind_pci(vf_ans_mbox_v1 *s, vf_ans_pci_v1 *pci) {
    if (!s) return -1;
    s->pci = pci;
    ans_mbox_reconcile_started(s);
    return 0;
}

int vf_ans_mbox_start(vf_ans_mbox_v1 *s) {
    if (!s) return -1;
    if (s->status != VF_ANS_MBOX_STATUS_BOOTSTRAP) return -1;
    /* qemu apple_ans_start: OR Memory|BusMaster when PCIe function exists. */
    if (s->pci) {
        if (vf_ans_pci_enable_memory_bus_master(s->pci) != 0) return -1;
        if (!vf_ans_pci_bus_master_enabled(s->pci)) return -1;
    }
    s->flags |= VF_ANS_MBOX_FLAG_STARTED;
    /* Status stays BOOTSTRAP: STARTED is flag honesty, not ACTIVE IOP FW. */
    ans_mbox_post_reply(
        s, vf_ans_mbox_pack_msg(VF_ANS_MBOX_EP_BOOTSTRAP, 0,
                                VF_ANS_MBOX_BOOTSTRAP_OP_ANNOUNCE, 0,
                                VF_ANS_MBOX_STATUS_BOOTSTRAP));
    return 0;
}

int vf_ans_mbox_wakeup(vf_ans_mbox_v1 *s) {
    /* qemu AppleMboxOps.wakeup aliases start for ANS. */
    return vf_ans_mbox_start(s);
}

int vf_ans_mbox_started(vf_ans_mbox_v1 *s) {
    if (!s) return 0;
    ans_mbox_reconcile_started(s);
    return (s->flags & VF_ANS_MBOX_FLAG_STARTED) ? 1 : 0;
}

int vf_ans_mbox_read(vf_ans_mbox_v1 *s, uint32_t offset, unsigned width,
                     uint64_t *value) {
    if (!s || !value) return -1;
    if (offset >= VF_ANS_MBOX_MMIO_SIZE) return -1;
    switch (offset) {
    case VF_ANS_MBOX_MMIO_STATUS:
        if (width != 32) return -1;
        *value = s->status;
        return 0;
    case VF_ANS_MBOX_MMIO_FLAGS:
        if (width != 32) return -1;
        ans_mbox_reconcile_started(s);
        *value = s->flags;
        return 0;
    case VF_ANS_MBOX_MMIO_TIER:
        if (width != 32) return -1;
        *value = VF_ANS_MBOX_TIER_AKF_REGMAP;
        return 0;
    case VF_ANS_MBOX_MMIO_A2I_QSTAT:
        /* A2I writes are consumed immediately; never report not-empty. */
        if (width != 32) return -1;
        *value = VF_ANS_MBOX_Q_EMPTY;
        return 0;
    case VF_ANS_MBOX_MMIO_I2A_QSTAT:
        if (width != 32) return -1;
        *value = s->reply_pending ? VF_ANS_MBOX_Q_NOT_EMPTY
                                  : VF_ANS_MBOX_Q_EMPTY;
        return 0;
    case VF_ANS_MBOX_MMIO_A2I_MSG:
        /* WO path is observability-only; guest-visible read stays 0. */
        if (width != 64) return -1;
        *value = 0;
        return 0;
    case VF_ANS_MBOX_MMIO_I2A_MSG:
        if (width != 64) return -1;
        *value = s->i2a_msg;
        /* Drain reply: I2A returns to EMPTY (no MSI/AIC). */
        s->reply_pending = 0;
        s->flags &= ~VF_ANS_MBOX_FLAG_REPLY_PENDING;
        return 0;
    default:
        return -1;
    }
}

int vf_ans_mbox_write(vf_ans_mbox_v1 *s, uint32_t offset, unsigned width,
                      uint64_t value) {
    if (!s) return -1;
    /* Only the A2I message slot is writable; status/flags/tier/qstat/I2A RO. */
    if (offset != VF_ANS_MBOX_MMIO_A2I_MSG) return -1;
    if (width != 64) return -1;
    s->last_a2i_write = value;
    s->a2i_write_seen = 1;
    /* Endpoint handshake only after start/wakeup (fail-closed otherwise). */
    ans_mbox_reconcile_started(s);
    if (s->flags & VF_ANS_MBOX_FLAG_STARTED)
        ans_mbox_handle_endpoint(s, value);
    return 0;
}
