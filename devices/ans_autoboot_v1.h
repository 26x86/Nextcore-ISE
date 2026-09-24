/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_ANS_AUTOBOOT_V1_H
#define VENFIRE_ANS_AUTOBOOT_V1_H
#include <stdint.h>
#include "ascwrap_v1.h"

/*
 * ANS composite AppleA7IOP autoBootRegMap stub (region index 2).
 * Behaviour contract: docs/research/INFERNO_DEVICE_MODELS.md (ANS), ANS.md,
 * ANS_AUTOBOOT.md. qemu-t8030 apple_ans maps IOP autoboot as RO→0 / WO accept
 * with no modeled firmware load. Host/test present/bootstrap honesty plus
 * optional ASCWrap READY → ARMED composite bind (READY←mbox STARTED). No IOP
 * ACTIVE, MSI, DT IRQ, or DMA.
 */

#define VF_ANS_AUTOBOOT_TIER_REGMAP      2u

/* Stage status (honest bootstrap only; ACTIVE unreachable without real IOP). */
#define VF_ANS_AUTOBOOT_STATUS_ABSENT    0u
#define VF_ANS_AUTOBOOT_STATUS_BOOTSTRAP 1u
#define VF_ANS_AUTOBOOT_STATUS_ACTIVE    2u /* unreachable at this stub tier */

#define VF_ANS_AUTOBOOT_FLAG_PRESENT     (1u << 0)
/* Set when bound ASCWrap reports READY; never invents ARMED unbound. */
#define VF_ANS_AUTOBOOT_FLAG_ARMED       (1u << 1)

/* Graph-local MMIO (not Apple A7IOP autoBootRegMap physical map). */
#define VF_ANS_AUTOBOOT_MMIO_STATUS      0x000u /* 32 RO */
#define VF_ANS_AUTOBOOT_MMIO_FLAGS       0x004u /* 32 RO */
#define VF_ANS_AUTOBOOT_MMIO_TIER        0x008u /* 32 RO */
#define VF_ANS_AUTOBOOT_MMIO_MAP         0x010u /* 64 RO→0 / WO accept|gate */
#define VF_ANS_AUTOBOOT_MMIO_SIZE        0x018u

typedef struct {
    uint32_t status;
    uint32_t flags;
    /* Last accepted WO (test observability only; not guest-visible RO). */
    uint64_t last_autoboot_write;
    int autoboot_write_seen;
    /* Optional ASCWrap link: ARMED mirrors vf_ascwrap_ready when bound. */
    vf_ascwrap_v1 *ascwrap;
} vf_ans_autoboot_v1;

int vf_ans_autoboot_init(vf_ans_autoboot_v1 *s);
uint32_t vf_ans_autoboot_status(const vf_ans_autoboot_v1 *s);
/* Reconciles ARMED from bound ASCWrap READY before returning flags. */
uint32_t vf_ans_autoboot_flags(vf_ans_autoboot_v1 *s);
/* 1 after at least one successful WO; never implies IRQ/MSI or IOP load. */
int vf_ans_autoboot_write_seen(const vf_ans_autoboot_v1 *s);
uint64_t vf_ans_autoboot_last_write(const vf_ans_autoboot_v1 *s);

/* Optional bind to ascwrap_v1. NULL ascwrap unbinds (clears ARMED).
 * When bound, FLAG_ARMED mirrors vf_ascwrap_ready (READY←mbox STARTED).
 * Fail-closed on NULL autoboot. */
int vf_ans_autoboot_bind_ascwrap(vf_ans_autoboot_v1 *s, vf_ascwrap_v1 *ascwrap);
/*
 * 1 when FLAG_ARMED is honest: bound ASCWrap still READY.
 * Unbound never reports armed. Clears stale ARMED if READY dropped.
 */
int vf_ans_autoboot_armed(vf_ans_autoboot_v1 *s);

/* Aligned access within [0, VF_ANS_AUTOBOOT_MMIO_SIZE).
 * Map @ 0x010: width 64 only; read always 0; write accepted when unbound,
 * or when bound and ASCWrap READY (fail-closed otherwise). Status/flags/tier:
 * width 32 RO. Flags RO reconciles ARMED. Fail-closed otherwise. No MSI /
 * DT IRQ. */
int vf_ans_autoboot_read(vf_ans_autoboot_v1 *s, uint32_t offset, unsigned width,
                         uint64_t *value);
int vf_ans_autoboot_write(vf_ans_autoboot_v1 *s, uint32_t offset,
                          unsigned width, uint64_t value);

#endif
