/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_ADP_DISPLAY_V1_H
#define VENFIRE_ADP_DISPLAY_V1_H
#include <stdint.h>

/*
 * Graph-local ADP L1 framebuffer / vblank stub.
 * Behaviour contract: docs/research/INFERNO_DEVICE_MODELS.md (ADP tier).
 * Inferno reference (behaviour-only): apple_displaypipe_v4.c FRAME_PROCESSED /
 * OUTPUT_READY → irq[0] when int_status & int_enable. No scaler, DSI, or GPU.
 * Aligns with Rust M1Framebuffer present → DISPLAY_SOURCE (2).
 */

#define VF_ADP_TIER_L1_FRAMEBUFFER   1u

#define VF_ADP_FLAG_PRESENT          (1u << 0)
#define VF_ADP_FLAG_CONFIGURED       (1u << 1)
#define VF_ADP_FLAG_ENABLED          (1u << 2)
#define VF_ADP_FLAG_PRESENTED        (1u << 3)
#define VF_ADP_FLAG_VBLANK_PENDING   (1u << 4)

#define VF_ADP_MAX_WIDTH             8192u
#define VF_ADP_MAX_HEIGHT            8192u

/* Graph-local MMIO offsets (not Apple ADP V4 physical map). */
#define VF_ADP_MMIO_CTRL             0x000u
#define VF_ADP_MMIO_WIDTH            0x004u
#define VF_ADP_MMIO_HEIGHT           0x008u
#define VF_ADP_MMIO_STRIDE           0x00cu
#define VF_ADP_MMIO_FRAME_COUNT      0x010u
#define VF_ADP_MMIO_TIER             0x014u
#define VF_ADP_MMIO_VBLANK_ACK       0x018u
#define VF_ADP_MMIO_GENERATION       0x01cu
#define VF_ADP_MMIO_SIZE             0x020u

/* CTRL write values (WO); reads return packed flags. */
#define VF_ADP_CTRL_DISABLE          0u
#define VF_ADP_CTRL_PRESENT          1u

typedef struct {
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint64_t guest_address;
    uint64_t backing_bytes;
    uint64_t frame_count;
    uint32_t generation;
    int vblank_pending;
} vf_adp_display_v1;

int vf_adp_display_init(vf_adp_display_v1 *s);
int vf_adp_display_configure(vf_adp_display_v1 *s, uint64_t guest_address,
                             uint64_t backing_bytes, uint32_t width,
                             uint32_t height, uint32_t stride);
int vf_adp_display_present(vf_adp_display_v1 *s);
int vf_adp_display_ack_vblank(vf_adp_display_v1 *s);
int vf_adp_display_irq_pending(const vf_adp_display_v1 *s);
int vf_adp_display_read(vf_adp_display_v1 *s, uint32_t offset, unsigned width,
                        uint64_t *value);
int vf_adp_display_write(vf_adp_display_v1 *s, uint32_t offset, unsigned width,
                         uint64_t value);

#endif
