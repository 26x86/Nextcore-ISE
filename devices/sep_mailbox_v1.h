/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_SEP_MAILBOX_V1_H
#define VENFIRE_SEP_MAILBOX_V1_H
#include <stdint.h>

/*
 * Graph-local SEP L1 mailbox stub.
 * Behaviour contract: docs/research/INFERNO_DEVICE_MODELS.md (SEP tier).
 * Inferno reference (behaviour-only): sep-sim.c bootstrap endpoint + status
 * stages. No SEP ROM, SEP OS, IMG4, keys, or crypto.
 */

#define VF_SEP_TIER_L1_MAILBOX       1u

/* Stage status (Inferno AppleSEPStatus behaviour reference). */
#define VF_SEP_STATUS_SLEEPING       0u
#define VF_SEP_STATUS_BOOTSTRAP      1u
#define VF_SEP_STATUS_ACTIVE         2u /* unreachable at L1 (needs FW) */

#define VF_SEP_EP_BOOTSTRAP          255u

#define VF_SEP_BOOTSTRAP_OP_PING           1u
#define VF_SEP_BOOTSTRAP_OP_GET_STATUS     2u
#define VF_SEP_BOOTSTRAP_OP_PING_ACK       101u
#define VF_SEP_BOOTSTRAP_OP_STATUS_REPLY   102u
#define VF_SEP_BOOTSTRAP_OP_ANNOUNCE       210u
/* Graph-local reject for FW/crypto ops that L1 must not pretend to honour. */
#define VF_SEP_BOOTSTRAP_OP_L1_REJECT      0xFEu

#define VF_SEP_FLAG_PRESENT          (1u << 0)
#define VF_SEP_FLAG_REPLY_PENDING    (1u << 1)

/* Graph-local MMIO offsets (not Apple A7IOP/AKF physical map). */
#define VF_SEP_MMIO_STATUS           0x000u
#define VF_SEP_MMIO_TIER             0x004u
#define VF_SEP_MMIO_FLAGS            0x008u
#define VF_SEP_MMIO_MSG_OUT          0x010u
#define VF_SEP_MMIO_MSG_IN           0x018u
#define VF_SEP_MMIO_SIZE             0x020u

typedef struct {
    uint8_t ep;
    uint8_t tag;
    uint8_t op;
    uint8_t param;
    uint32_t data;
} vf_sep_mbox_msg;

typedef struct {
    uint32_t status;
    uint32_t flags;
    uint64_t msg_out;
    int reply_pending;
} vf_sep_mailbox_v1;

int vf_sep_mailbox_init(vf_sep_mailbox_v1 *s);
int vf_sep_mailbox_irq_pending(const vf_sep_mailbox_v1 *s);
uint32_t vf_sep_mailbox_status(const vf_sep_mailbox_v1 *s);
int vf_sep_mailbox_handle_msg(vf_sep_mailbox_v1 *s, uint64_t msg_in,
                              uint64_t *out);
int vf_sep_mailbox_read(vf_sep_mailbox_v1 *s, uint32_t offset, unsigned width,
                        uint64_t *value);
int vf_sep_mailbox_write(vf_sep_mailbox_v1 *s, uint32_t offset, unsigned width,
                         uint64_t value);

#endif
