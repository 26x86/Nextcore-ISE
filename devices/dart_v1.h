/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_DART_V1_H
#define VENFIRE_DART_V1_H
#include <stdint.h>

/* Apple DART IOMMU per-instance MMIO (T8030 topology reference). See DART.md. */
#define VF_DART_INSTANCE_SIZE 0x4000u

#define VF_DART_REG_PARAMS1         0x000u
#define VF_DART_REG_PARAMS2         0x004u
#define VF_DART_REG_TLB_OP          0x020u
#define VF_DART_REG_SID_MASK_LO      0x034u
#define VF_DART_REG_SID_MASK_HI      0x038u
#define VF_DART_REG_ERROR_STATUS    0x040u
#define VF_DART_REG_ERROR_ADDR_LO   0x050u
#define VF_DART_REG_ERROR_ADDR_HI   0x054u
#define VF_DART_REG_CONFIG          0x060u
#define VF_DART_REG_SID_REMAP_BASE  0x080u
#define VF_DART_REG_TCR_BASE        0x100u
#define VF_DART_REG_TTBR_BASE       0x200u

#define VF_DART_MAX_STREAMS         16u
#define VF_DART_TTBR_PER_SID        4u

#define VF_DART_PARAMS1_PAGE_SHIFT        (0xfu << 24) /* bits 27-24 */
#define VF_DART_PARAMS1_PAGE_SHIFT_VAL(s) (((s) & 0xfu) << 24)
#define VF_DART_PARAMS2_BYPASS_SUPPORT    (1u << 0)
#define VF_DART_TLB_OP_BUSY               (1u << 2)
#define VF_DART_TLB_OP_INVALIDATE         (1u << 20)
#define VF_DART_ERROR_FLAG                (1u << 31)
#define VF_DART_CONFIG_LOCK               (1u << 15)
#define VF_DART_TCR_TXEN                  (1u << 7)
#define VF_DART_TCR_BYPASS_DART           (1u << 8)
#define VF_DART_TTBR_VALID                (1u << 31)

#define VF_DART_DEFAULT_PAGE_SHIFT 12u

#define VF_DART_REG_TCR(sid)  (VF_DART_REG_TCR_BASE + 4u * (sid))
#define VF_DART_REG_TTBR(sid, idx) \
    (VF_DART_REG_TTBR_BASE + 16u * (sid) + 4u * (idx))

typedef struct {
    uint32_t params1;
    uint32_t params2;
    uint32_t tlb_op;
    uint32_t sid_mask_lo;
    uint32_t sid_mask_hi;
    uint32_t error_status;
    uint32_t error_addr_lo;
    uint32_t error_addr_hi;
    uint32_t config;
    uint32_t sid_remap[4];
    uint32_t tcr[VF_DART_MAX_STREAMS];
    uint32_t ttbr[VF_DART_MAX_STREAMS][VF_DART_TTBR_PER_SID];
    /* Host/test-observable invalidate generation (IOTLB notifier stand-in). */
    uint32_t tlb_generation;
} vf_dart_v1;

int vf_dart_init(vf_dart_v1 *);
uint32_t vf_dart_tlb_generation(const vf_dart_v1 *);
/* True while ERROR_STATUS FLAG is latched (AIC line should stay asserted). */
int vf_dart_irq_pending(const vf_dart_v1 *);
/* Stand-in for DMA fault latch until translation path exists.
 * Encodes fault SID in ERROR_STATUS bits 24–27 when sid < VF_DART_MAX_STREAMS. */
int vf_dart_simulate_fault(vf_dart_v1 *, unsigned sid, uint32_t code);
/* Exactly aligned little-endian u32 within [0, VF_DART_INSTANCE_SIZE). Unknown fails. */
int vf_dart_read(vf_dart_v1 *, uint32_t offset, unsigned width, uint32_t *value);
int vf_dart_write(vf_dart_v1 *, uint32_t offset, unsigned width, uint32_t value);

#endif
