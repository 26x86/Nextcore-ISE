/* 26x86 first-party SEP L1 mailbox stub. See SEP.md for tier boundary.
 * Behaviour adapted from Inferno sep-sim.c bootstrap PING/GET_STATUS
 * (GPL reference only). No SEP ROM/FW embodiment.
 */
#include "sep_mailbox_v1.h"

#ifdef VF_EFI_BUILD
static void *sep_memset(void *dst, int c, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
static void *sep_memcpy(void *dst, const void *src, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
#define memset(d, c, n) sep_memset((d), (c), (unsigned)(n))
#define memcpy(d, s, n) sep_memcpy((d), (s), (unsigned)(n))
#else
#include <string.h>
#endif

static uint64_t pack_msg(uint8_t ep, uint8_t tag, uint8_t op, uint8_t param,
                         uint32_t data) {
    vf_sep_mbox_msg m;
    uint64_t raw = 0;
    m.ep = ep;
    m.tag = tag;
    m.op = op;
    m.param = param;
    m.data = data;
    memcpy(&raw, &m, sizeof(m));
    return raw;
}

static void unpack_msg(uint64_t raw, vf_sep_mbox_msg *m) {
    memcpy(m, &raw, sizeof(*m));
}

int vf_sep_mailbox_init(vf_sep_mailbox_v1 *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    s->status = VF_SEP_STATUS_BOOTSTRAP;
    s->flags = VF_SEP_FLAG_PRESENT;
    /* Honest announce: mailbox present in bootstrap; no FW claimed. */
    s->msg_out = pack_msg(VF_SEP_EP_BOOTSTRAP, 0, VF_SEP_BOOTSTRAP_OP_ANNOUNCE,
                          0, VF_SEP_STATUS_BOOTSTRAP);
    s->reply_pending = 1;
    s->flags |= VF_SEP_FLAG_REPLY_PENDING;
    return 0;
}

int vf_sep_mailbox_irq_pending(const vf_sep_mailbox_v1 *s) {
    return s && s->reply_pending ? 1 : 0;
}

uint32_t vf_sep_mailbox_status(const vf_sep_mailbox_v1 *s) {
    return s ? s->status : VF_SEP_STATUS_SLEEPING;
}

int vf_sep_mailbox_handle_msg(vf_sep_mailbox_v1 *s, uint64_t msg_in,
                              uint64_t *out) {
    vf_sep_mbox_msg in;
    uint64_t reply;

    if (!s) return -1;
    unpack_msg(msg_in, &in);

    if (in.ep != VF_SEP_EP_BOOTSTRAP) {
        reply = pack_msg(in.ep, in.tag, VF_SEP_BOOTSTRAP_OP_L1_REJECT, in.op, 0);
    } else {
        switch (in.op) {
        case VF_SEP_BOOTSTRAP_OP_PING:
            reply = pack_msg(VF_SEP_EP_BOOTSTRAP, in.tag,
                             VF_SEP_BOOTSTRAP_OP_PING_ACK, 0, 0);
            break;
        case VF_SEP_BOOTSTRAP_OP_GET_STATUS:
            reply = pack_msg(VF_SEP_EP_BOOTSTRAP, in.tag,
                             VF_SEP_BOOTSTRAP_OP_STATUS_REPLY, 0, s->status);
            break;
        default:
            /* BOOT_TZ0 / BOOT_IMG4 / nonce / crypto: L1 must not accept. */
            reply = pack_msg(VF_SEP_EP_BOOTSTRAP, in.tag,
                             VF_SEP_BOOTSTRAP_OP_L1_REJECT, in.op, 0);
            break;
        }
    }

    s->msg_out = reply;
    s->reply_pending = 1;
    s->flags |= VF_SEP_FLAG_REPLY_PENDING;
    if (out) *out = reply;
    return 0;
}

int vf_sep_mailbox_read(vf_sep_mailbox_v1 *s, uint32_t offset, unsigned width,
                        uint64_t *value) {
    if (!s || !value) return -1;
    switch (offset) {
    case VF_SEP_MMIO_STATUS:
        if (width != 32) return -1;
        *value = s->status;
        return 0;
    case VF_SEP_MMIO_TIER:
        if (width != 32) return -1;
        *value = VF_SEP_TIER_L1_MAILBOX;
        return 0;
    case VF_SEP_MMIO_FLAGS:
        if (width != 32) return -1;
        *value = s->flags;
        return 0;
    case VF_SEP_MMIO_MSG_OUT:
        if (width != 64) return -1;
        *value = s->msg_out;
        s->reply_pending = 0;
        s->flags &= ~VF_SEP_FLAG_REPLY_PENDING;
        return 0;
    default:
        return -1;
    }
}

int vf_sep_mailbox_write(vf_sep_mailbox_v1 *s, uint32_t offset, unsigned width,
                         uint64_t value) {
    if (!s) return -1;
    if (offset != VF_SEP_MMIO_MSG_IN) return -1;
    if (width != 64) return -1;
    return vf_sep_mailbox_handle_msg(s, value, 0);
}
