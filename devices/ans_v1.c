/* 26x86 first-party ANS v1 vendor-MMIO stub. See ANS.md for register evidence.
 * Behaviour adapted from qemu-t8030 hw/block/apple_ans.c and Inferno
 * hw/block/apple-silicon/ans.c (GPL reference only).
 */
#include "ans_v1.h"

#ifdef VF_EFI_BUILD
static void *ans_memset(void *dst, int c, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
static void *ans_memcpy(void *dst, const void *src, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
#define memset(d, c, n) ans_memset((d), (c), (unsigned)(n))
#define memcpy(d, s, n) ans_memcpy((d), (s), (unsigned)(n))
#else
#include <string.h>
#endif

static int ans_bounds(uint32_t off, unsigned width) {
    if (width != 4 || (off & 3u)) return -1;
    if (off >= VF_ANS_VENDOR_SIZE) return -1;
    return 0;
}

/* DSTRD=0 doorbell index in [0, VF_ANS_DB_COUNT), or -1 if outside region. */
static int ans_db_index(uint32_t off) {
    if (off < VF_ANS_DB_BASE || off >= VF_ANS_DB_END || (off & 3u)) return -1;
    return (int)((off - VF_ANS_DB_BASE) / 4u);
}

static int ans_sparse_find(const vf_ans_v1 *a, uint32_t off) {
    uint32_t i;
    for (i = 0; i < a->vendor_sparse_used; i++) {
        if (a->vendor_sparse[i].off == off) return (int)i;
    }
    return -1;
}

/* Forward decl: doorbell path may one-shot Identify DMA fill. */
int vf_ans_identify_cns_ctrl_dma(vf_ans_v1 *, uint8_t cns, void *buf,
                                 uint32_t len);
int vf_ans_identify_cns_ns_dma(vf_ans_v1 *, uint8_t cns, void *buf,
                               uint32_t len);

/* Honest store into sparse vendor_reg[]; fail closed when table is full. */
static int ans_sparse_store(vf_ans_v1 *a, uint32_t off, uint32_t value) {
    int idx = ans_sparse_find(a, off);
    if (idx >= 0) {
        a->vendor_sparse[idx].val = value;
        return 0;
    }
    if (a->vendor_sparse_used >= VF_ANS_VENDOR_SPARSE_CAP) return -1;
    a->vendor_sparse[a->vendor_sparse_used].off = off;
    a->vendor_sparse[a->vendor_sparse_used].val = value;
    a->vendor_sparse_used++;
    return 0;
}

static uint32_t ans_sparse_load(const vf_ans_v1 *a, uint32_t off) {
    int idx = ans_sparse_find(a, off);
    return idx >= 0 ? a->vendor_sparse[idx].val : 0u;
}

static void ans_store_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void ans_clear_admin_identify(vf_ans_v1 *a) {
    a->admin_id_armed = 0;
    a->admin_id_opcode = 0;
    a->admin_id_cns = 0;
    a->admin_id_buf = 0;
    a->admin_id_len = 0;
    a->admin_id_cq_buf = 0;
    a->admin_id_cq_len = 0;
}

/*
 * When admin ACQ page is bound: require a writable CQE slot at
 * ACQ[admin_cq_tail]. Returns 0 to continue, -1 fail-closed.
 * Unbound ACQ keeps cq_buf-only legacy path. No GPA walk / MSI / AIC.
 */
static int ans_check_admin_acq_slot(const vf_ans_v1 *a) {
    uint32_t off;
    if (!a || !a->admin_acq_mem) return 0; /* unbound: cq_buf-only */
    if ((a->acq_lo | a->acq_hi) == 0u) return -1;
    off = (uint32_t)a->admin_cq_tail * VF_ANS_CQE_SIZE;
    if (a->admin_acq_len < off + VF_ANS_CQE_SIZE) return -1;
    return 0;
}

/* Copy CQE into guest ACQ[admin_cq_tail] when bound (caller validated slot). */
static void ans_write_admin_acq_cqe(vf_ans_v1 *a, const uint8_t *cqe) {
    uint32_t off;
    if (!a || !cqe || !a->admin_acq_mem) return;
    off = (uint32_t)a->admin_cq_tail * VF_ANS_CQE_SIZE;
    memcpy(a->admin_acq_mem + off, cqe, VF_ANS_CQE_SIZE);
}

/* Mirror local irq_check into bound PCI irq pending (qemu nvme_irq_check →
 * pci_irq_assert/deassert via apple_ans_set_irq, or MSI pending latch when
 * MSI Enable is set). set_irq routes INTx vs MSI pending; never writes MSI
 * message address/data to guest memory; never AIC. */
static void ans_sync_pci_irq(vf_ans_v1 *a) {
    int pending;
    if (!a || !a->pci) return;
    pending = ((a->irq_status & ~a->intm & VF_ANS_IRQ_ADMIN_CQ) != 0) ? 1 : 0;
    (void)vf_ans_pci_set_irq(a->pci, pending);
}

/* Write simplified admin CQE + optional guest ACQ + advance CQ Tail +
 * set irq_status bit0. CQHDBL is host drain; irq_check may sync bound PCI
 * INTx/MSI pending (+ optional bound MSI message write; ≠ AIC).
 * Caller must have validated ACQ slot when bound. */
static void ans_post_admin_identify_cqe(vf_ans_v1 *a, uint8_t *cq_buf) {
    uint16_t sqhd;
    uint32_t dw2;
    uint32_t dw3;
    if (!a || !cq_buf) return;
    memset(cq_buf, 0, VF_ANS_CQE_SIZE);
    /* SQHD honesty: catch up to current admin SQ Tail after submit. SQID=0. */
    sqhd = (uint16_t)(a->doorbell[0] & VF_ANS_DB_VALUE_MASK);
    dw2 = (uint32_t)sqhd;
    /* CID=0, Status=Successful Completion, Phase Tag from admin_cq_phase. */
    dw3 = ((uint32_t)(a->admin_cq_phase & 1u)) << VF_ANS_CQE_PHASE_SHIFT;
    ans_store_le32(cq_buf + VF_ANS_CQE_OFF_DW2, dw2);
    ans_store_le32(cq_buf + VF_ANS_CQE_OFF_DW3, dw3);
    /* Guest-visible ACQ: mirror CQE before Tail advance (slot = old Tail). */
    ans_write_admin_acq_cqe(a, cq_buf);
    a->admin_cq_phase ^= 1u;
    a->admin_cq_tail =
        (uint16_t)((a->admin_cq_tail + 1u) & VF_ANS_DB_VALUE_MASK);
    /* NVMe irq_status: admin CQ vector pending until CQH drains Tail. */
    a->irq_status |= VF_ANS_IRQ_ADMIN_CQ;
    ans_sync_pci_irq(a);
}

/* Clear irq_status bit0 when admin CQ has no undrained completions. */
static void ans_refresh_admin_cq_irq(vf_ans_v1 *a) {
    uint16_t cqh;
    uint16_t pending;
    if (!a) return;
    cqh = (uint16_t)(a->doorbell[1] & VF_ANS_DB_VALUE_MASK);
    pending = (uint16_t)((a->admin_cq_tail - cqh) & VF_ANS_DB_VALUE_MASK);
    if (pending == 0)
        a->irq_status &= ~VF_ANS_IRQ_ADMIN_CQ;
    ans_sync_pci_irq(a);
}

/* Load little-endian u64 from guest/host buffer (PRP1 honesty). */
static uint64_t ans_load_le64(const uint8_t *p) {
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16)
        | ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32)
        | ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48)
        | ((uint64_t)p[7] << 56);
}

/* Load little-endian u32 from guest/host buffer (SQE NSID honesty). */
static uint32_t ans_load_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

/* Store little-endian u64 into Identify Namespace buffer. */
static void ans_store_le64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    p[4] = (uint8_t)(v >> 32);
    p[5] = (uint8_t)(v >> 40);
    p[6] = (uint8_t)(v >> 48);
    p[7] = (uint8_t)(v >> 56);
}

/*
 * When admin ASQ page is bound: fetch SQE at ASQ[old_sq_tail] and apply
 * Identify + PRP/SGL honesty. Returns 0 to continue DMA, -1 fail-closed.
 * Unbound ASQ keeps legacy arm-only path (no fetch). When Identify PRP1
 * page is also bound: page-aligned PRP1 requires PRP2==0 (single-page
 * translate); with PRP2 also bound, unaligned PRP1 + page-aligned non-zero
 * PRP2 is a second-data-page split (PRP list never walked). Real GPA is not
 * walked — bound mem is the translate. out_prp1 receives SQE PRP1 on success
 * (0 when ASQ unbound). CNS=NS also requires SQE NSID == stub NSID=1.
 */
static int ans_fetch_admin_asq_identify(vf_ans_v1 *a, uint16_t old_sq_tail,
                                        uint8_t expect_opcode,
                                        uint8_t expect_cns,
                                        uint64_t *out_prp1) {
    uint8_t sqe[VF_ANS_SQE_SIZE];
    uint32_t off;
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t prp1;
    uint64_t prp2;
    uint8_t opcode;
    uint8_t psdt;
    uint8_t cns;
    uint32_t prp_off;
    if (out_prp1) *out_prp1 = 0;
    if (!a || !a->admin_asq_mem) return 0; /* unbound: arm-only honesty */
    /* Guest-visible ASQ base registers must be non-zero when page is bound. */
    if ((a->asq_lo | a->asq_hi) == 0u) return -1;
    off = (uint32_t)old_sq_tail * VF_ANS_SQE_SIZE;
    if (a->admin_asq_len < off + VF_ANS_SQE_SIZE) return -1;
    memcpy(sqe, a->admin_asq_mem + off, VF_ANS_SQE_SIZE);
    cdw0 = ans_load_le32(sqe + VF_ANS_SQE_OFF_CDW0);
    opcode = (uint8_t)(cdw0 & 0xffu);
    psdt = (uint8_t)((cdw0 >> VF_ANS_SQE_PSDT_SHIFT) & VF_ANS_SQE_PSDT_MASK);
    cns = sqe[VF_ANS_SQE_OFF_CDW10];
    nsid = ans_load_le32(sqe + VF_ANS_SQE_OFF_NSID);
    prp1 = ans_load_le64(sqe + VF_ANS_SQE_OFF_PRP1);
    prp2 = ans_load_le64(sqe + VF_ANS_SQE_OFF_PRP2);
    /* Shape must match armed Identify CNS; SGL / zero PRP1 fail closed. */
    if (opcode != expect_opcode || cns != expect_cns) return -1;
    if (psdt != 0u) return -1; /* PSDT!=00 → SGL — no SGL walk claimed */
    if (prp1 == 0u) return -1; /* PRP1 GPA honesty */
    /* CNS=NS: only stub NSID=1 exists (zero-capacity; no other namespaces). */
    if (expect_cns == VF_ANS_ID_CNS_NS && nsid != VF_ANS_ID_NS_STUB_NSID)
        return -1;
    /* Bound Identify PRP1 page: single-page or PRP2 second-page split. */
    if (a->admin_id_prp1_mem) {
        if (a->admin_id_prp1_len < VF_ANS_ID_CTRL_SIZE) return -1;
        prp_off = (uint32_t)(prp1 & VF_ANS_PRP_PAGE_MASK);
        if (prp_off == 0u) {
            /* Full 4KiB fits in PRP1 — PRP2 must be 0 (no page-list). */
            if (prp2 != 0u) return -1;
        } else {
            /* Unaligned: PRP2 is second data page only (list never walked). */
            if (!a->admin_id_prp2_mem) return -1;
            if (a->admin_id_prp2_len < VF_ANS_ID_CTRL_SIZE) return -1;
            if (prp2 == 0u) return -1;
            if ((prp2 & VF_ANS_PRP_PAGE_MASK) != 0u) return -1;
        }
    }
    if (out_prp1) *out_prp1 = prp1;
    return 0;
}

/* Copy Identify CNS=CTRL payload into bound PRP1 (+ optional PRP2) stand-ins. */
static void ans_write_identify_prp(vf_ans_v1 *a, const uint8_t *buf,
                                   uint64_t prp1) {
    uint32_t prp_off;
    uint32_t first;
    if (!a || !buf || !a->admin_id_prp1_mem) return;
    if (a->admin_id_prp1_len < VF_ANS_ID_CTRL_SIZE) return;
    prp_off = (uint32_t)(prp1 & VF_ANS_PRP_PAGE_MASK);
    if (prp_off == 0u) {
        memcpy(a->admin_id_prp1_mem, buf, VF_ANS_ID_CTRL_SIZE);
        return;
    }
    /* Split: first (PAGE-off) bytes at PRP1[off], remainder at PRP2[0]. */
    if (!a->admin_id_prp2_mem || a->admin_id_prp2_len < VF_ANS_ID_CTRL_SIZE)
        return;
    first = VF_ANS_PRP_PAGE_SIZE - prp_off;
    if (first >= VF_ANS_ID_CTRL_SIZE) return; /* defensive; off!=0 ⇒ first<4K */
    memcpy(a->admin_id_prp1_mem + prp_off, buf, first);
    memcpy(a->admin_id_prp2_mem, buf + first, VF_ANS_ID_CTRL_SIZE - first);
}

/* One-shot: admin SQTDBL advance with armed Identify CNS=CTRL|NS → optional ASQ
 * fetch / PRP honesty → optional ACQ slot check → DMA fill; on DMA success
 * optional PRP1/PRP2 GPA→buffer translate + write simplified admin CQE (+ guest
 * ACQ when bound) + advance CQ Tail + irq_status (no MSI / AIC / PRP list).
 * Clears arm first so fail-closed gates never leave a dishonest pending fill. */
static void ans_try_admin_identify_on_doorbell(vf_ans_v1 *a,
                                               uint16_t old_sq_tail) {
    uint8_t *buf;
    uint8_t *cq_buf;
    uint32_t len;
    uint32_t cq_len;
    uint8_t cns;
    uint8_t opcode;
    uint64_t prp1;
    int dma_rc;
    if (!a || !a->admin_id_armed) return;
    buf = a->admin_id_buf;
    len = a->admin_id_len;
    cq_buf = a->admin_id_cq_buf;
    cq_len = a->admin_id_cq_len;
    cns = a->admin_id_cns;
    opcode = a->admin_id_opcode;
    ans_clear_admin_identify(a);
    if (opcode != VF_ANS_OPC_IDENTIFY) return;
    if (cns != VF_ANS_ID_CNS_CTRL && cns != VF_ANS_ID_CNS_NS) return;
    if (!buf || !cq_buf || cq_len < VF_ANS_CQE_SIZE) return;
    /* Bound ASQ: fetch SQE + PRP/SGL honesty before DMA (fail-closed). */
    if (ans_fetch_admin_asq_identify(a, old_sq_tail, opcode, cns, &prp1) != 0)
        return;
    /* Bound ACQ: require writable CQE slot before DMA (fail-closed). */
    if (ans_check_admin_acq_slot(a) != 0) return;
    /* DMA fill fails closed without CC.EN / bound BusMaster; buf untouched. */
    if (cns == VF_ANS_ID_CNS_CTRL)
        dma_rc = vf_ans_identify_cns_ctrl_dma(a, cns, buf, len);
    else
        dma_rc = vf_ans_identify_cns_ns_dma(a, cns, buf, len);
    if (dma_rc != 0) return;
    /* Bound PRP1(+PRP2): GPA→host buffer translate for Identify payload. */
    ans_write_identify_prp(a, buf, prp1);
    ans_post_admin_identify_cqe(a, cq_buf);
}

static void ans_apply_cc_enable(vf_ans_v1 *a, uint32_t cc) {
    a->cc = cc;
    if (cc & VF_ANS_CC_EN) {
        a->csts |= VF_ANS_CSTS_RDY;
    } else {
        a->csts &= ~VF_ANS_CSTS_RDY;
        /* Controller reset: all SQ/CQ doorbells + interrupt mask + irq_status
         * → 0 (no queue DMA / MSI / AIC). Matches qemu-t8030 nvme_ctrl_reset.
         * Sparse vendor_reg[] is not cleared (reference vendor array persists).
         * Armed host/test Identify submit and CQ Tail/phase are also cleared. */
        memset(a->doorbell, 0, sizeof(a->doorbell));
        a->intm = 0;
        a->irq_status = 0;
        a->admin_cq_tail = 0;
        a->admin_cq_phase = 1;
        ans_clear_admin_identify(a);
    }
    ans_sync_pci_irq(a);
}

int vf_ans_init(vf_ans_v1 *a) {
    if (!a) return -1;
    a->modesel = 0;
    a->linear_sq = 0;
    a->base_cmd_id = VF_ANS_BASE_CMD_ID_VAL;
    a->cc = 0;
    a->csts = 0;
    a->intm = 0;
    a->irq_status = 0;
    a->aqa = 0;
    a->asq_lo = 0;
    a->asq_hi = 0;
    a->acq_lo = 0;
    a->acq_hi = 0;
    memset(a->doorbell, 0, sizeof(a->doorbell));
    memset(a->vendor_sparse, 0, sizeof(a->vendor_sparse));
    a->vendor_sparse_used = 0;
    a->is_apple_ans = 1; /* ans_v1 defaults to Apple ANS Identify CNS=CTRL */
    a->pci = 0;
    a->mbox = 0;
    a->ascwrap = 0;
    a->autoboot = 0;
    a->admin_cq_tail = 0;
    a->admin_cq_phase = 1; /* NVMe: first valid CQE uses Phase Tag 1 */
    a->admin_asq_mem = 0;
    a->admin_asq_len = 0;
    a->admin_acq_mem = 0;
    a->admin_acq_len = 0;
    a->admin_id_prp1_mem = 0;
    a->admin_id_prp1_len = 0;
    a->admin_id_prp2_mem = 0;
    a->admin_id_prp2_len = 0;
    ans_clear_admin_identify(a);
    return 0;
}

int vf_ans_bind_pci(vf_ans_v1 *a, vf_ans_pci_v1 *pci) {
    if (!a) return -1;
    /* Detach: deassert old pin so a stale INTx level does not linger. */
    if (a->pci && a->pci != pci)
        (void)vf_ans_pci_set_irq(a->pci, 0);
    a->pci = pci;
    if (pci)
        ans_sync_pci_irq(a);
    return 0;
}

int vf_ans_bind_mbox(vf_ans_v1 *a, vf_ans_mbox_v1 *mbox) {
    if (!a) return -1;
    a->mbox = mbox;
    return 0;
}

int vf_ans_bind_ascwrap(vf_ans_v1 *a, vf_ascwrap_v1 *ascwrap) {
    if (!a) return -1;
    a->ascwrap = ascwrap;
    return 0;
}

int vf_ans_bind_autoboot(vf_ans_v1 *a, vf_ans_autoboot_v1 *autoboot) {
    if (!a) return -1;
    a->autoboot = autoboot;
    return 0;
}

int vf_ans_set_is_apple_ans(vf_ans_v1 *a, int enabled) {
    if (!a) return -1;
    a->is_apple_ans = enabled ? 1 : 0;
    return 0;
}

int vf_ans_get_is_apple_ans(const vf_ans_v1 *a) {
    if (!a) return -1;
    return a->is_apple_ans ? 1 : 0;
}

int vf_ans_identify_sqes(const vf_ans_v1 *a, uint8_t *sqes) {
    if (!a || !sqes) return -1;
    /* Metadata only: mirrors qemu-t8030 nvme_init_ctrl id->sqes branch.
     * No admin Identify opcode, PRP list, or command DMA. */
    *sqes = a->is_apple_ans ? (uint8_t)VF_ANS_ID_SQES_APPLE
                            : (uint8_t)VF_ANS_ID_SQES_STD;
    return 0;
}

int vf_ans_identify_cqes(const vf_ans_v1 *a, uint8_t *cqes) {
    if (!a || !cqes) return -1;
    /* Fixed in nvme_init_ctrl for both Apple and std paths. */
    *cqes = (uint8_t)VF_ANS_ID_CQES;
    return 0;
}

int vf_ans_identify_cns_supported(uint8_t cns) {
    /* CNS=CTRL and CNS=NS have host/test metadata + DMA fill; lists deferred. */
    return (cns == VF_ANS_ID_CNS_CTRL || cns == VF_ANS_ID_CNS_NS) ? 0 : -1;
}

int vf_ans_identify_cns_ctrl(const vf_ans_v1 *a, vf_ans_id_ctrl_meta *out) {
    if (!a || !out) return -1;
    /* Identify CNS=CTRL field subset from qemu-t8030 nvme_init_ctrl +
     * apple_ans.mdts=8. No admin opcode, PRP, or command DMA. */
    out->cns = (uint8_t)VF_ANS_ID_CNS_CTRL;
    out->sqes = a->is_apple_ans ? (uint8_t)VF_ANS_ID_SQES_APPLE
                                : (uint8_t)VF_ANS_ID_SQES_STD;
    out->cqes = (uint8_t)VF_ANS_ID_CQES;
    out->mdts = a->is_apple_ans ? (uint8_t)VF_ANS_ID_MDTS_APPLE
                                : (uint8_t)VF_ANS_ID_MDTS_STD;
    out->cntrltype = (uint8_t)VF_ANS_ID_CNTRLTYPE;
    out->nn = VF_ANS_ID_NN;
    out->ver = VF_ANS_ID_VER;
    return 0;
}

int vf_ans_identify_cns_ctrl_dma(vf_ans_v1 *a, uint8_t cns, void *buf,
                                 uint32_t len) {
    vf_ans_id_ctrl_meta meta;
    uint8_t *out;
    if (!a || !buf || len < VF_ANS_ID_CTRL_SIZE) return -1;
    if (cns != VF_ANS_ID_CNS_CTRL) return -1;
    /* Fail closed without controller enable (CSTS.RDY tracks CC.EN). */
    if (!(a->cc & VF_ANS_CC_EN) || !(a->csts & VF_ANS_CSTS_RDY)) return -1;
    /* Bound PCI: DMA without BusMaster is dishonest. Unbound skips this gate. */
    if (a->pci && !vf_ans_pci_bus_master_enabled(a->pci)) return -1;
    /* Bound mbox: DMA without STARTED is dishonest. Unbound skips this gate. */
    if (a->mbox && !vf_ans_mbox_started(a->mbox)) return -1;
    /* Bound ASCWrap: DMA without READY is dishonest (READY←mbox STARTED). */
    if (a->ascwrap && !vf_ascwrap_ready(a->ascwrap)) return -1;
    /* Bound autoboot: DMA without ARMED is dishonest (ARMED←READY←STARTED). */
    if (a->autoboot && !vf_ans_autoboot_armed(a->autoboot)) return -1;
    if (vf_ans_identify_cns_ctrl(a, &meta)) return -1;
    out = (uint8_t *)buf;
    memset(out, 0, VF_ANS_ID_CTRL_SIZE);
    /* Place known metadata at NVMe id_ctrl offsets; other fields stay zero. */
    out[VF_ANS_ID_OFF_MDTS] = meta.mdts;
    ans_store_le32(out + VF_ANS_ID_OFF_VER, meta.ver);
    out[VF_ANS_ID_OFF_CNTRLTYPE] = meta.cntrltype;
    out[VF_ANS_ID_OFF_SQES] = meta.sqes;
    out[VF_ANS_ID_OFF_CQES] = meta.cqes;
    ans_store_le32(out + VF_ANS_ID_OFF_NN, meta.nn);
    return 0;
}

int vf_ans_identify_cns_ns(const vf_ans_v1 *a, vf_ans_id_ns_meta *out) {
    if (!a || !out) return -1;
    /* Zero-capacity stub NSID=1 only. No block backend / LBA formats. */
    out->cns = (uint8_t)VF_ANS_ID_CNS_NS;
    out->nsid = VF_ANS_ID_NS_STUB_NSID;
    out->nsze = 0;
    out->ncap = 0;
    out->nuse = 0;
    out->nlbaf = 0;
    out->flbas = 0;
    return 0;
}

int vf_ans_identify_cns_ns_dma(vf_ans_v1 *a, uint8_t cns, void *buf,
                               uint32_t len) {
    vf_ans_id_ns_meta meta;
    uint8_t *out;
    if (!a || !buf || len < VF_ANS_ID_NS_SIZE) return -1;
    if (cns != VF_ANS_ID_CNS_NS) return -1;
    if (!(a->cc & VF_ANS_CC_EN) || !(a->csts & VF_ANS_CSTS_RDY)) return -1;
    if (a->pci && !vf_ans_pci_bus_master_enabled(a->pci)) return -1;
    if (a->mbox && !vf_ans_mbox_started(a->mbox)) return -1;
    if (a->ascwrap && !vf_ascwrap_ready(a->ascwrap)) return -1;
    if (a->autoboot && !vf_ans_autoboot_armed(a->autoboot)) return -1;
    if (vf_ans_identify_cns_ns(a, &meta)) return -1;
    out = (uint8_t *)buf;
    memset(out, 0, VF_ANS_ID_NS_SIZE);
    /* Explicit zero-capacity fields at NVMe id_ns offsets (already 0 after
     * memset; written for honesty so callers can rely on the contract). */
    ans_store_le64(out + VF_ANS_ID_NS_OFF_NSZE, meta.nsze);
    ans_store_le64(out + VF_ANS_ID_NS_OFF_NCAP, meta.ncap);
    ans_store_le64(out + VF_ANS_ID_NS_OFF_NUSE, meta.nuse);
    out[VF_ANS_ID_NS_OFF_NLBAF] = meta.nlbaf;
    out[VF_ANS_ID_NS_OFF_FLBAS] = meta.flbas;
    return 0;
}

int vf_ans_arm_admin_identify(vf_ans_v1 *a, uint8_t opcode, uint8_t cns,
                              void *buf, uint32_t len, void *cq_buf,
                              uint32_t cq_len) {
    if (!a || !buf || len < VF_ANS_ID_CTRL_SIZE) return -1;
    if (!cq_buf || cq_len < VF_ANS_CQE_SIZE) return -1;
    /* Simplified SQE: Identify + CNS=CTRL|NS. Other opcodes/CNS fail closed. */
    if (opcode != VF_ANS_OPC_IDENTIFY) return -1;
    if (cns != VF_ANS_ID_CNS_CTRL && cns != VF_ANS_ID_CNS_NS) return -1;
    a->admin_id_opcode = opcode;
    a->admin_id_cns = cns;
    a->admin_id_buf = (uint8_t *)buf;
    a->admin_id_len = len;
    a->admin_id_cq_buf = (uint8_t *)cq_buf;
    a->admin_id_cq_len = cq_len;
    a->admin_id_armed = 1;
    return 0;
}

int vf_ans_admin_identify_armed(const vf_ans_v1 *a) {
    if (!a) return -1;
    return a->admin_id_armed ? 1 : 0;
}

int vf_ans_disarm_admin_identify(vf_ans_v1 *a) {
    if (!a) return -1;
    ans_clear_admin_identify(a);
    return 0;
}

int vf_ans_bind_admin_asq(vf_ans_v1 *a, void *mem, uint32_t len) {
    if (!a) return -1;
    if (!mem) {
        a->admin_asq_mem = 0;
        a->admin_asq_len = 0;
        return 0;
    }
    if (len < VF_ANS_SQE_SIZE) return -1;
    a->admin_asq_mem = (uint8_t *)mem;
    a->admin_asq_len = len;
    return 0;
}

int vf_ans_admin_asq_bound(const vf_ans_v1 *a) {
    if (!a) return -1;
    return a->admin_asq_mem ? 1 : 0;
}

int vf_ans_bind_admin_acq(vf_ans_v1 *a, void *mem, uint32_t len) {
    if (!a) return -1;
    if (!mem) {
        a->admin_acq_mem = 0;
        a->admin_acq_len = 0;
        return 0;
    }
    if (len < VF_ANS_CQE_SIZE) return -1;
    a->admin_acq_mem = (uint8_t *)mem;
    a->admin_acq_len = len;
    return 0;
}

int vf_ans_admin_acq_bound(const vf_ans_v1 *a) {
    if (!a) return -1;
    return a->admin_acq_mem ? 1 : 0;
}

int vf_ans_bind_identify_prp1(vf_ans_v1 *a, void *mem, uint32_t len) {
    if (!a) return -1;
    if (!mem) {
        a->admin_id_prp1_mem = 0;
        a->admin_id_prp1_len = 0;
        return 0;
    }
    if (len < VF_ANS_ID_CTRL_SIZE) return -1;
    a->admin_id_prp1_mem = (uint8_t *)mem;
    a->admin_id_prp1_len = len;
    return 0;
}

int vf_ans_identify_prp1_bound(const vf_ans_v1 *a) {
    if (!a) return -1;
    return a->admin_id_prp1_mem ? 1 : 0;
}

int vf_ans_bind_identify_prp2(vf_ans_v1 *a, void *mem, uint32_t len) {
    if (!a) return -1;
    if (!mem) {
        a->admin_id_prp2_mem = 0;
        a->admin_id_prp2_len = 0;
        return 0;
    }
    if (len < VF_ANS_ID_CTRL_SIZE) return -1;
    a->admin_id_prp2_mem = (uint8_t *)mem;
    a->admin_id_prp2_len = len;
    return 0;
}

int vf_ans_identify_prp2_bound(const vf_ans_v1 *a) {
    if (!a) return -1;
    return a->admin_id_prp2_mem ? 1 : 0;
}

int vf_ans_admin_cq_tail(const vf_ans_v1 *a) {
    if (!a) return -1;
    return (int)a->admin_cq_tail;
}

int vf_ans_admin_cq_pending(const vf_ans_v1 *a) {
    uint16_t cqh;
    if (!a) return -1;
    /* CQH honesty: host CQHDBL drains controller CQ Tail. */
    cqh = (uint16_t)(a->doorbell[1] & VF_ANS_DB_VALUE_MASK);
    return (int)((uint16_t)((a->admin_cq_tail - cqh) & VF_ANS_DB_VALUE_MASK));
}

int vf_ans_irq_check(const vf_ans_v1 *a) {
    if (!a) return -1;
    /* NVMe pin/legacy: irq pending when status bit set and not INTMS-masked.
     * When PCI is bound, ans_sync_pci_irq mirrors this into INTx or MSI
     * pending (MSI Enable routes to msi_pending; optional bound message
     * write — message write ≠ AIC). */
    return ((a->irq_status & ~a->intm & VF_ANS_IRQ_ADMIN_CQ) != 0) ? 1 : 0;
}

int vf_ans_read(vf_ans_v1 *a, uint32_t off, unsigned width, uint32_t *value) {
    int dbi;
    if (!a || !value || ans_bounds(off, width)) return -1;
    dbi = ans_db_index(off);
    if (dbi >= 0) {
        *value = a->doorbell[dbi];
        return 0;
    }
    switch (off) {
    case VF_ANS_REG_CAP_LO:
        /* RO sticky: NVMe CAP[31:0] (MQES/CQR/TO). */
        *value = VF_ANS_CAP_LO_VAL;
        return 0;
    case VF_ANS_REG_CAP_HI:
        /* RO sticky: NVMe CAP[63:32] (CSS/MPSMAX; CMBS/PMRS clear). */
        *value = VF_ANS_CAP_HI_VAL;
        return 0;
    case VF_ANS_REG_VS:
        /* RO sticky: NVMe Version 1.4 (NVME_SPEC_VER). */
        *value = VF_ANS_VS_VAL;
        return 0;
    case VF_ANS_REG_INTMS:
    case VF_ANS_REG_INTMC:
        /* Both aliases read the shared interrupt mask (no MSI side effects). */
        *value = a->intm;
        return 0;
    case VF_ANS_REG_CC:
        *value = a->cc;
        return 0;
    case VF_ANS_REG_CSTS:
        /* RO: RDY tracks CC.EN; other CSTS bits remain clear (no SHN/CFS). */
        *value = a->csts;
        return 0;
    case VF_ANS_REG_AQA:
        *value = a->aqa;
        return 0;
    case VF_ANS_REG_ASQ_LO:
        *value = a->asq_lo;
        return 0;
    case VF_ANS_REG_ASQ_HI:
        *value = a->asq_hi;
        return 0;
    case VF_ANS_REG_ACQ_LO:
        *value = a->acq_lo;
        return 0;
    case VF_ANS_REG_ACQ_HI:
        *value = a->acq_hi;
        return 0;
    case VF_ANS_REG_MAX_PEND:
        /* Sticky magic: writes may store but guest always sees capacity probe. */
        *value = VF_ANS_MAX_PEND_VAL;
        return 0;
    case VF_ANS_REG_BOOT_STATUS:
        /* Sticky magic: boot-unblock status is never poisoned by guest writes. */
        *value = VF_ANS_BOOT_STATUS_OK;
        return 0;
    case VF_ANS_REG_MODESEL:
        *value = a->modesel;
        return 0;
    case VF_ANS_REG_BASE_CMD_ID:
        /* Sticky constant (Inferno/t8030): stored write does not change read. */
        *value = VF_ANS_BASE_CMD_ID_VAL;
        return 0;
    case VF_ANS_REG_LINEAR_SQ:
        *value = a->linear_sq;
        return 0;
    default:
        if (off >= VF_ANS_NVME_ALIAS_SIZE) {
            /* Sparse vendor_reg[] honest read (unmodeled Apple vendor offs). */
            *value = ans_sparse_load(a, off);
        } else {
            /* Unmodeled NVMe alias gap: RO-zero policy. */
            *value = 0;
        }
        return 0;
    }
}

int vf_ans_write(vf_ans_v1 *a, uint32_t off, unsigned width, uint32_t value) {
    int dbi;
    if (!a || ans_bounds(off, width)) return -1;
    dbi = ans_db_index(off);
    if (dbi >= 0) {
        uint32_t newv = value & VF_ANS_DB_VALUE_MASK;
        uint32_t oldv = a->doorbell[dbi];
        /* Store low-16 always. Admin SQTDBL advance with armed Identify
         * CNS=CTRL drives optional ASQ fetch + PRP honesty + optional ACQ
         * CQE write + optional PRP1 GPA→buffer translate + DMA fill + admin
         * CQE + irq_status on success; no MSI / AIC / PRP list. Admin CQHDBL
         * drain may clear irq_status. */
        a->doorbell[dbi] = newv;
        if (dbi == 0 && newv != oldv && a->admin_id_armed)
            ans_try_admin_identify_on_doorbell(
                a, (uint16_t)(oldv & VF_ANS_DB_VALUE_MASK));
        else if (dbi == 1)
            ans_refresh_admin_cq_irq(a);
        return 0;
    }
    switch (off) {
    case VF_ANS_REG_INTMS:
        /* Interrupt Mask Set: OR into shared mask; masks irq_check fail-closed
         * and deasserts bound PCI INTx without clearing irq_status or
         * asserting MSI / AIC. */
        a->intm |= value;
        ans_sync_pci_irq(a);
        return 0;
    case VF_ANS_REG_INTMC:
        /* Interrupt Mask Clear: clear bits in shared mask; may re-expose
         * irq_check / PCI INTx when irq_status still set (no MSI / AIC). */
        a->intm &= ~value;
        ans_sync_pci_irq(a);
        return 0;
    case VF_ANS_REG_CC:
        ans_apply_cc_enable(a, value);
        return 0;
    case VF_ANS_REG_CSTS:
        /* Accept write; CSTS remains derived from CC.EN (RO honesty). */
        return 0;
    case VF_ANS_REG_AQA:
        a->aqa = value;
        return 0;
    case VF_ANS_REG_ASQ_LO:
        a->asq_lo = value;
        return 0;
    case VF_ANS_REG_ASQ_HI:
        a->asq_hi = value;
        return 0;
    case VF_ANS_REG_ACQ_LO:
        a->acq_lo = value;
        return 0;
    case VF_ANS_REG_ACQ_HI:
        a->acq_hi = value;
        return 0;
    case VF_ANS_REG_MODESEL:
        a->modesel = value;
        return 0;
    case VF_ANS_REG_BASE_CMD_ID:
        /* Accept write (reference vendor_reg store); read remains sticky. */
        a->base_cmd_id = value;
        return 0;
    case VF_ANS_REG_LINEAR_SQ:
        a->linear_sq = value;
        return 0;
    case VF_ANS_REG_CAP_LO:
    case VF_ANS_REG_CAP_HI:
    case VF_ANS_REG_VS:
        /* Accept write; sticky/RO magic overrides on subsequent reads. */
        return 0;
    case VF_ANS_REG_MAX_PEND:
    case VF_ANS_REG_BOOT_STATUS:
        /* Reference stores into vendor_reg[]; sticky read still overrides. */
        return ans_sparse_store(a, off, value);
    default:
        if (off >= VF_ANS_NVME_ALIAS_SIZE) {
            /* Sparse vendor_reg[] honest store (unmodeled Apple vendor offs). */
            return ans_sparse_store(a, off, value);
        }
        /* Unmodeled NVMe alias gap: RO-zero — accept, do not store. */
        return 0;
    }
}
