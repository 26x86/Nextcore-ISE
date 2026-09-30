/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_ASCWRAP_V1_H
#define VENFIRE_ASCWRAP_V1_H
#include <stdint.h>
#include "ans_mbox_v1.h"

/*
 * ANS composite AppleASCWrapV2 coreRegisterMap stub (region index 1).
 * Behaviour contract: docs/research/INFERNO_DEVICE_MODELS.md (ANS), ANS.md,
 * ASCWRAP.md. qemu-t8030 apple_ans maps ASCWrapV2 core as 8-byte RO/WO → 0.
 * Host/test present/bootstrap honesty plus optional mbox STARTED → READY
 * composite bind. No IOP ACTIVE, MSI, DT IRQ, or DMA.
 */

#define VF_ASCWRAP_TIER_CORE_MAP         1u

/* Stage status (honest bootstrap only; ACTIVE unreachable without real ASC). */
#define VF_ASCWRAP_STATUS_ABSENT         0u
#define VF_ASCWRAP_STATUS_BOOTSTRAP      1u
#define VF_ASCWRAP_STATUS_ACTIVE         2u /* unreachable at this stub tier */

#define VF_ASCWRAP_FLAG_PRESENT          (1u << 0)
/* Set when bound mbox reports STARTED; never invents READY unbound. */
#define VF_ASCWRAP_FLAG_READY            (1u << 1)

/* Graph-local MMIO (not Apple ASCWrap physical map). */
#define VF_ASCWRAP_MMIO_STATUS           0x000u /* 32 RO */
#define VF_ASCWRAP_MMIO_FLAGS            0x004u /* 32 RO */
#define VF_ASCWRAP_MMIO_TIER             0x008u /* 32 RO */
#define VF_ASCWRAP_MMIO_CORE             0x010u /* 64 RO→0 / WO accept */
#define VF_ASCWRAP_MMIO_SIZE             0x018u

/* Reference coreRegisterMap width (qemu-t8030 AppleASCWrapV2). */
#define VF_ASCWRAP_CORE_SIZE             8u

typedef struct {
    uint32_t status;
    uint32_t flags;
    /* Last accepted core write (test observability only; not guest-visible RO). */
    uint64_t last_core_write;
    int core_write_seen;
    /* Optional mbox link: READY mirrors vf_ans_mbox_started when bound. */
    vf_ans_mbox_v1 *mbox;
} vf_ascwrap_v1;

int vf_ascwrap_init(vf_ascwrap_v1 *s);
uint32_t vf_ascwrap_status(const vf_ascwrap_v1 *s);
/* Reconciles READY from bound mbox STARTED before returning flags. */
uint32_t vf_ascwrap_flags(vf_ascwrap_v1 *s);
/* 1 after at least one successful core WO; never implies IRQ/MSI. */
int vf_ascwrap_core_write_seen(const vf_ascwrap_v1 *s);
uint64_t vf_ascwrap_last_core_write(const vf_ascwrap_v1 *s);

/* Optional bind to ans_mbox_v1. NULL mbox unbinds (clears READY).
 * When bound, FLAG_READY mirrors vf_ans_mbox_started (fail-closed).
 * Fail-closed on NULL ascwrap. */
int vf_ascwrap_bind_mbox(vf_ascwrap_v1 *s, vf_ans_mbox_v1 *mbox);
/*
 * 1 when FLAG_READY is honest: bound mbox still STARTED.
 * Unbound never reports ready. Clears stale READY if mbox dropped STARTED.
 */
int vf_ascwrap_ready(vf_ascwrap_v1 *s);

/* Aligned access within [0, VF_ASCWRAP_MMIO_SIZE).
 * Core @ 0x010: width 64 only; read always 0; write accepted (no side effects).
 * Status/flags/tier: width 32 RO. Flags RO reconciles READY. Fail-closed
 * otherwise. No MSI / DT IRQ. */
int vf_ascwrap_read(vf_ascwrap_v1 *s, uint32_t offset, unsigned width,
                    uint64_t *value);
int vf_ascwrap_write(vf_ascwrap_v1 *s, uint32_t offset, unsigned width,
                     uint64_t value);

#endif
