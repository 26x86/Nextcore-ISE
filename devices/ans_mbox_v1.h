/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_ANS_MBOX_V1_H
#define VENFIRE_ANS_MBOX_V1_H
#include <stdint.h>
#include "ans_pci_v1.h"

/*
 * ANS composite AppleA7IOP akfRegMap mailbox stub (region index 0).
 * Behaviour contract: docs/research/INFERNO_DEVICE_MODELS.md (ANS), ANS.md,
 * ANS_MBOX.md. Public Asahi ASC mailbox facts (behaviour-only): queue-empty
 * status uses bit17; bit16 is not-empty. Host/test present/bootstrap plus
 * start/wakeup → FLAG_STARTED (+ optional PCI Memory|BusMaster latch) plus
 * graph-local bootstrap endpoint handshake (SEP L1 packing style).
 * Status never reaches ACTIVE. No MSI, DT IRQ, AIC, or command DMA.
 */

#define VF_ANS_MBOX_TIER_AKF_REGMAP      0u

/* Stage status (honest bootstrap only; ACTIVE unreachable without real IOP). */
#define VF_ANS_MBOX_STATUS_ABSENT        0u
#define VF_ANS_MBOX_STATUS_BOOTSTRAP     1u
#define VF_ANS_MBOX_STATUS_ACTIVE        2u /* unreachable at this stub tier */

#define VF_ANS_MBOX_FLAG_PRESENT         (1u << 0)
/* Set by start/wakeup; never implies ACTIVE / MSI / DMA / A2I queue fill. */
#define VF_ANS_MBOX_FLAG_STARTED         (1u << 1)
/* Set while an I2A bootstrap reply is waiting to be drained (no MSI/AIC). */
#define VF_ANS_MBOX_FLAG_REPLY_PENDING   (1u << 2)

/* Asahi ASC mailbox queue-status bits (public docs; behaviour-only). */
#define VF_ANS_MBOX_Q_NOT_EMPTY          (1u << 16)
#define VF_ANS_MBOX_Q_EMPTY              (1u << 17)

/* Graph-local bootstrap endpoint (SEP L1 style; not Apple ANS EP numbers). */
#define VF_ANS_MBOX_EP_BOOTSTRAP         255u
#define VF_ANS_MBOX_BOOTSTRAP_OP_PING           1u
#define VF_ANS_MBOX_BOOTSTRAP_OP_GET_STATUS     2u
#define VF_ANS_MBOX_BOOTSTRAP_OP_PING_ACK       101u
#define VF_ANS_MBOX_BOOTSTRAP_OP_STATUS_REPLY   102u
#define VF_ANS_MBOX_BOOTSTRAP_OP_ANNOUNCE       210u
/* Graph-local reject for unknown EP / FW ops this stub must not honour. */
#define VF_ANS_MBOX_BOOTSTRAP_OP_L1_REJECT      0xFEu

/* Graph-local MMIO (not Apple A7IOP/AKF physical map / +0x8000 layout). */
#define VF_ANS_MBOX_MMIO_STATUS          0x000u /* 32 RO */
#define VF_ANS_MBOX_MMIO_FLAGS           0x004u /* 32 RO */
#define VF_ANS_MBOX_MMIO_TIER            0x008u /* 32 RO */
#define VF_ANS_MBOX_MMIO_A2I_QSTAT       0x010u /* 32 RO: always EMPTY */
#define VF_ANS_MBOX_MMIO_I2A_QSTAT       0x014u /* 32 RO: EMPTY or NOT_EMPTY */
#define VF_ANS_MBOX_MMIO_A2I_MSG         0x018u /* 64 RO→0 / WO endpoint */
#define VF_ANS_MBOX_MMIO_I2A_MSG         0x020u /* 64 RO reply drain */
#define VF_ANS_MBOX_MMIO_SIZE            0x028u

typedef struct {
    uint8_t ep;
    uint8_t tag;
    uint8_t op;
    uint8_t param;
    uint32_t data;
} vf_ans_mbox_msg;

typedef struct vf_ans_mbox_v1 {
    uint32_t status;
    uint32_t flags;
    /* Last accepted A2I WO (test observability; guest A2I RO stays 0). */
    uint64_t last_a2i_write;
    int a2i_write_seen;
    /* Last I2A reply word (guest-visible via I2A_MSG while/after pending). */
    uint64_t i2a_msg;
    int reply_pending;
    /* Optional PCI link for start/wakeup → Memory|BusMaster latch. */
    vf_ans_pci_v1 *pci;
} vf_ans_mbox_v1;

int vf_ans_mbox_init(vf_ans_mbox_v1 *s);
uint32_t vf_ans_mbox_status(const vf_ans_mbox_v1 *s);
uint32_t vf_ans_mbox_flags(vf_ans_mbox_v1 *s);
/* 1 after at least one successful A2I WO; never implies IRQ/MSI or A2I fill. */
int vf_ans_mbox_a2i_write_seen(const vf_ans_mbox_v1 *s);
uint64_t vf_ans_mbox_last_a2i_write(const vf_ans_mbox_v1 *s);
/* 1 while I2A reply awaits drain (FLAG_REPLY_PENDING); not MSI/AIC. */
int vf_ans_mbox_reply_pending(const vf_ans_mbox_v1 *s);
uint64_t vf_ans_mbox_i2a_msg(const vf_ans_mbox_v1 *s);

/* Host/test message pack (SEP L1 bit layout; graph-local only). */
uint64_t vf_ans_mbox_pack_msg(uint8_t ep, uint8_t tag, uint8_t op,
                              uint8_t param, uint32_t data);
void vf_ans_mbox_unpack_msg(uint64_t raw, vf_ans_mbox_msg *out);

/* Optional bind to ans_pci_v1. NULL pci unbinds. Fail-closed on NULL mbox. */
int vf_ans_mbox_bind_pci(vf_ans_mbox_v1 *s, vf_ans_pci_v1 *pci);
/*
 * Host/test start/wakeup (qemu AppleMboxOps.start/wakeup → apple_ans_start).
 * Sets FLAG_STARTED; status stays BOOTSTRAP. When PCI is bound, also latches
 * COMMAND Memory|BusMaster. Posts bootstrap ANNOUNCE into I2A (reply pending;
 * I2A qstat NOT_EMPTY until drained). Never enables MSI, DMA, or A2I fill.
 * Idempotent start re-posts announce.
 */
int vf_ans_mbox_start(vf_ans_mbox_v1 *s);
int vf_ans_mbox_wakeup(vf_ans_mbox_v1 *s);
/*
 * 1 when FLAG_STARTED is honest: unbound, or linked PCI still has BusMaster.
 * Clears stale FLAG_STARTED if linked BusMaster was dropped (fail-closed).
 */
int vf_ans_mbox_started(vf_ans_mbox_v1 *s);

/* Aligned access within [0, VF_ANS_MBOX_MMIO_SIZE).
 * A2I qstat: always EMPTY (A2I writes consumed immediately by stub).
 * I2A qstat: NOT_EMPTY while reply_pending, else EMPTY.
 * A2I msg @ 0x018: width 64; read 0; write accept (+ endpoint when STARTED).
 * I2A msg @ 0x020: width 64 RO; drains reply_pending on read.
 * Status/flags/tier: width 32 RO. Fail-closed otherwise. No MSI / DT IRQ. */
int vf_ans_mbox_read(vf_ans_mbox_v1 *s, uint32_t offset, unsigned width,
                     uint64_t *value);
int vf_ans_mbox_write(vf_ans_mbox_v1 *s, uint32_t offset, unsigned width,
                      uint64_t value);

#endif
