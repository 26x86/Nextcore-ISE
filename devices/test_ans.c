#include "ans_v1.h"
#include "aic_v1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Pack a 64-byte Identify SQE with PRP1/PRP2 GPAs + NSID (host/test ASQ). */
static void fill_identify_sqe_prp_nsid(uint8_t *sqe, uint64_t prp1,
                                       uint64_t prp2, uint8_t cns,
                                       uint8_t psdt, uint32_t nsid) {
    uint32_t cdw0;
    unsigned i;
    memset(sqe, 0, VF_ANS_SQE_SIZE);
    cdw0 = (uint32_t)VF_ANS_OPC_IDENTIFY
        | (((uint32_t)psdt & VF_ANS_SQE_PSDT_MASK) << VF_ANS_SQE_PSDT_SHIFT);
    sqe[0] = (uint8_t)(cdw0);
    sqe[1] = (uint8_t)(cdw0 >> 8);
    sqe[2] = (uint8_t)(cdw0 >> 16);
    sqe[3] = (uint8_t)(cdw0 >> 24);
    sqe[VF_ANS_SQE_OFF_NSID] = (uint8_t)(nsid);
    sqe[VF_ANS_SQE_OFF_NSID + 1u] = (uint8_t)(nsid >> 8);
    sqe[VF_ANS_SQE_OFF_NSID + 2u] = (uint8_t)(nsid >> 16);
    sqe[VF_ANS_SQE_OFF_NSID + 3u] = (uint8_t)(nsid >> 24);
    for (i = 0; i < 8u; i++)
        sqe[VF_ANS_SQE_OFF_PRP1 + i] = (uint8_t)(prp1 >> (8u * i));
    for (i = 0; i < 8u; i++)
        sqe[VF_ANS_SQE_OFF_PRP2 + i] = (uint8_t)(prp2 >> (8u * i));
    sqe[VF_ANS_SQE_OFF_CDW10] = cns;
}

static void fill_identify_sqe_prp(uint8_t *sqe, uint64_t prp1, uint64_t prp2,
                                  uint8_t cns, uint8_t psdt) {
    /* CTRL Identify typically uses NSID=0; CNS=NS tests set stub NSID. */
    fill_identify_sqe_prp_nsid(sqe, prp1, prp2, cns, psdt, 0u);
}

static void fill_identify_sqe(uint8_t *sqe, uint64_t prp1, uint8_t cns,
                              uint8_t psdt) {
    fill_identify_sqe_prp(sqe, prp1, 0u, cns, psdt);
}

/* Host-buffer MSI message sink for bound PCI Identify CQ path. */
typedef struct {
    uint8_t buf[4];
    uint64_t last_addr;
    uint16_t last_data;
    int writes;
} ans_msi_sink_t;

static int ans_msi_sink_write(void *opaque, uint64_t addr, uint16_t data) {
    ans_msi_sink_t *sink = (ans_msi_sink_t *)opaque;
    if (!sink) return -1;
    sink->buf[0] = (uint8_t)(data & 0xffu);
    sink->buf[1] = (uint8_t)((data >> 8) & 0xffu);
    sink->last_addr = addr;
    sink->last_data = data;
    sink->writes++;
    return 0;
}

/* Pattern-matched STORAGE AIC line sink (Identify CQ irq_check path). */
static int ans_aic_storage_sink(void *opaque, unsigned irq, int high) {
    vf_aic_v1 *aic = (vf_aic_v1 *)opaque;
    if (!aic || irq != VF_ANS_PCI_AIC_STORAGE_IRQ_LINE) return -1;
    return vf_aic_set_line(aic, irq, high);
}

int main(void) {
    vf_ans_v1 a;
    uint32_t v = 0;

    assert(!vf_ans_init(&a));
    assert(!vf_ans_read(&a, VF_ANS_REG_BOOT_STATUS, 4, &v) && v == VF_ANS_BOOT_STATUS_OK);
    assert(!vf_ans_read(&a, VF_ANS_REG_MAX_PEND, 4, &v) && v == VF_ANS_MAX_PEND_VAL);
    assert(!vf_ans_read(&a, VF_ANS_REG_BASE_CMD_ID, 4, &v) && v == VF_ANS_BASE_CMD_ID_VAL);

    /* NVMe alias CAP/VS RO honesty (qemu-t8030 nvme_init_ctrl defaults). */
    assert(!vf_ans_read(&a, VF_ANS_REG_CAP_LO, 4, &v) && v == VF_ANS_CAP_LO_VAL);
    assert(!vf_ans_read(&a, VF_ANS_REG_CAP_HI, 4, &v) && v == VF_ANS_CAP_HI_VAL);
    assert(!vf_ans_read(&a, VF_ANS_REG_VS, 4, &v) && v == VF_ANS_VS_VAL);
    assert(!vf_ans_write(&a, VF_ANS_REG_CAP_LO, 4, 0xffffffffu));
    assert(!vf_ans_write(&a, VF_ANS_REG_CAP_HI, 4, 0xffffffffu));
    assert(!vf_ans_write(&a, VF_ANS_REG_VS, 4, 0xffffffffu));
    assert(!vf_ans_read(&a, VF_ANS_REG_CAP_LO, 4, &v) && v == VF_ANS_CAP_LO_VAL);
    assert(!vf_ans_read(&a, VF_ANS_REG_CAP_HI, 4, &v) && v == VF_ANS_CAP_HI_VAL);
    assert(!vf_ans_read(&a, VF_ANS_REG_VS, 4, &v) && v == VF_ANS_VS_VAL);

    /* CC enable path: AQA/ASQ/ACQ store; CSTS.RDY tracks CC.EN (no MSI). */
    assert(!vf_ans_read(&a, VF_ANS_REG_CC, 4, &v) && v == 0);
    assert(!vf_ans_read(&a, VF_ANS_REG_CSTS, 4, &v) && v == 0);
    assert(!vf_ans_write(&a, VF_ANS_REG_AQA, 4, 0x001f001fu));
    assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, 0x1000u));
    assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, 0x2u));
    assert(!vf_ans_write(&a, VF_ANS_REG_ACQ_LO, 4, 0x2000u));
    assert(!vf_ans_write(&a, VF_ANS_REG_ACQ_HI, 4, 0x3u));
    assert(!vf_ans_read(&a, VF_ANS_REG_AQA, 4, &v) && v == 0x001f001fu);
    assert(!vf_ans_read(&a, VF_ANS_REG_ASQ_LO, 4, &v) && v == 0x1000u);
    assert(!vf_ans_read(&a, VF_ANS_REG_ASQ_HI, 4, &v) && v == 0x2u);
    assert(!vf_ans_read(&a, VF_ANS_REG_ACQ_LO, 4, &v) && v == 0x2000u);
    assert(!vf_ans_read(&a, VF_ANS_REG_ACQ_HI, 4, &v) && v == 0x3u);
    assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN | (6u << 16) | (4u << 20)));
    assert(!vf_ans_read(&a, VF_ANS_REG_CC, 4, &v) && v == (VF_ANS_CC_EN | (6u << 16) | (4u << 20)));
    assert(!vf_ans_read(&a, VF_ANS_REG_CSTS, 4, &v) && v == VF_ANS_CSTS_RDY);
    assert(!vf_ans_write(&a, VF_ANS_REG_CSTS, 4, 0xffffffffu)); /* RO: ignore poison */
    assert(!vf_ans_read(&a, VF_ANS_REG_CSTS, 4, &v) && v == VF_ANS_CSTS_RDY);
    assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0)); /* disable clears RDY */
    assert(!vf_ans_read(&a, VF_ANS_REG_CSTS, 4, &v) && v == 0);
    /* Unmodeled NVMe alias gap: RO-zero (accept write; read stays 0). */
    assert(!vf_ans_write(&a, 0x0100u, 4, 0xdeadbeefu));
    assert(!vf_ans_read(&a, 0x0100u, 4, &v) && v == 0);
    assert(!vf_ans_write(&a, 0x0038u, 4, 0x11111111u));
    assert(!vf_ans_read(&a, 0x0038u, 4, &v) && v == 0);

    /* INTMS/INTMC: shared mask; INTMS ORs, INTMC clears; no MSI delivery. */
    assert(!vf_ans_read(&a, VF_ANS_REG_INTMS, 4, &v) && v == 0);
    assert(!vf_ans_read(&a, VF_ANS_REG_INTMC, 4, &v) && v == 0);
    assert(!vf_ans_write(&a, VF_ANS_REG_INTMS, 4, 0x00000005u));
    assert(!vf_ans_read(&a, VF_ANS_REG_INTMS, 4, &v) && v == 0x5u);
    assert(!vf_ans_read(&a, VF_ANS_REG_INTMC, 4, &v) && v == 0x5u); /* alias read */
    assert(!vf_ans_write(&a, VF_ANS_REG_INTMS, 4, 0x0000000au)); /* OR → 0xf */
    assert(!vf_ans_read(&a, VF_ANS_REG_INTMS, 4, &v) && v == 0xfu);
    assert(!vf_ans_write(&a, VF_ANS_REG_INTMC, 4, 0x00000001u)); /* clear bit0 → 0xe */
    assert(!vf_ans_read(&a, VF_ANS_REG_INTMS, 4, &v) && v == 0xeu);
    assert(!vf_ans_read(&a, VF_ANS_REG_INTMC, 4, &v) && v == 0xeu);
    assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
    assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0)); /* disable clears intm */
    assert(!vf_ans_read(&a, VF_ANS_REG_INTMS, 4, &v) && v == 0);
    assert(!vf_ans_read(&a, VF_ANS_REG_INTMC, 4, &v) && v == 0);

    /* Doorbells (DSTRD=0): admin @ 0x1000/0x1004; I/O qid≥1 @ 0x1008+ — store only. */
    assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
    assert(!vf_ans_read(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, &v) && v == 0);
    assert(!vf_ans_read(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, &v) && v == 0);
    assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x00010005u)); /* hi ignored */
    assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0xabcd0003u));
    assert(!vf_ans_read(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, &v) && v == 0x5u);
    assert(!vf_ans_read(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, &v) && v == 0x3u);
    /* I/O qid1 SQTDBL @ 0x1008 / CQHDBL @ 0x100C; qid2 SQ @ 0x1010 — no DMA. */
    assert(VF_ANS_REG_SQTDBL(1) == 0x1008u);
    assert(VF_ANS_REG_CQHDBL(1) == 0x100Cu);
    assert(VF_ANS_REG_SQTDBL(2) == 0x1010u);
    assert(!vf_ans_read(&a, VF_ANS_REG_SQTDBL(1), 4, &v) && v == 0);
    assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL(1), 4, 0x00ff0011u));
    assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL(1), 4, 0x12340022u));
    assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL(2), 4, 0x00000033u));
    assert(!vf_ans_read(&a, VF_ANS_REG_SQTDBL(1), 4, &v) && v == 0x11u);
    assert(!vf_ans_read(&a, VF_ANS_REG_CQHDBL(1), 4, &v) && v == 0x22u);
    assert(!vf_ans_read(&a, VF_ANS_REG_SQTDBL(2), 4, &v) && v == 0x33u);
    /* Last doorbell slot in NVMe alias (qid63 CQ @ 0x11FC) stores; 0x1200 is vendor. */
    assert(VF_ANS_REG_CQHDBL(63) == 0x11FCu);
    assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL(63), 4, 0x0000aaaau));
    assert(!vf_ans_read(&a, VF_ANS_REG_CQHDBL(63), 4, &v) && v == 0xaaaau);
    assert(!vf_ans_read(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, &v) && v == 0x5u); /* admin intact */
    assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0)); /* disable clears all doorbells */
    assert(!vf_ans_read(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, &v) && v == 0);
    assert(!vf_ans_read(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, &v) && v == 0);
    assert(!vf_ans_read(&a, VF_ANS_REG_SQTDBL(1), 4, &v) && v == 0);
    assert(!vf_ans_read(&a, VF_ANS_REG_CQHDBL(1), 4, &v) && v == 0);
    assert(!vf_ans_read(&a, VF_ANS_REG_SQTDBL(2), 4, &v) && v == 0);
    assert(!vf_ans_read(&a, VF_ANS_REG_CQHDBL(63), 4, &v) && v == 0);

    /* Sticky magic status: guest writes must not poison boot/status probes. */
    assert(!vf_ans_write(&a, VF_ANS_REG_BOOT_STATUS, 4, 0xdeadbeefu));
    assert(!vf_ans_read(&a, VF_ANS_REG_BOOT_STATUS, 4, &v) && v == VF_ANS_BOOT_STATUS_OK);
    assert(!vf_ans_write(&a, VF_ANS_REG_MAX_PEND, 4, 0x11112222u));
    assert(!vf_ans_read(&a, VF_ANS_REG_MAX_PEND, 4, &v) && v == VF_ANS_MAX_PEND_VAL);
    assert(!vf_ans_write(&a, VF_ANS_REG_BASE_CMD_ID, 4, 0xabcdffffu));
    assert(!vf_ans_read(&a, VF_ANS_REG_BASE_CMD_ID, 4, &v) && v == VF_ANS_BASE_CMD_ID_VAL);
    assert(a.base_cmd_id == 0xabcdffffu); /* write accepted; read sticky */

    assert(!vf_ans_write(&a, VF_ANS_REG_LINEAR_SQ, 4, VF_ANS_LINEAR_SQ_EN));
    assert(!vf_ans_read(&a, VF_ANS_REG_LINEAR_SQ, 4, &v) && v == VF_ANS_LINEAR_SQ_EN);
    assert(!vf_ans_write(&a, VF_ANS_REG_MODESEL, 4, 0x42u));
    assert(!vf_ans_read(&a, VF_ANS_REG_MODESEL, 4, &v) && v == 0x42u);

    /* Sparse vendor_reg[]: unmodeled vendor offs (≥0x1200) store/read honesty. */
    assert(!vf_ans_read(&a, 0x1400u, 4, &v) && v == 0);
    assert(!vf_ans_write(&a, 0x1400u, 4, 0xcafeu));
    assert(!vf_ans_write(&a, 0x2000u, 4, 0xbeefu));
    assert(!vf_ans_write(&a, 0x5FFFCu, 4, 0xabcd1234u));
    assert(!vf_ans_read(&a, 0x1400u, 4, &v) && v == 0xcafeu);
    assert(!vf_ans_read(&a, 0x2000u, 4, &v) && v == 0xbeefu);
    assert(!vf_ans_read(&a, 0x5FFFCu, 4, &v) && v == 0xabcd1234u);
    assert(!vf_ans_write(&a, 0x1400u, 4, 0x1111u)); /* overwrite same slot */
    assert(!vf_ans_read(&a, 0x1400u, 4, &v) && v == 0x1111u);
    assert(!vf_ans_read(&a, VF_ANS_REG_MODESEL, 4, &v) && v == 0x42u); /* named intact */
    /* Sticky magics still win after sparse poison store. */
    assert(!vf_ans_write(&a, VF_ANS_REG_BOOT_STATUS, 4, 0xdeadbeefu));
    assert(!vf_ans_read(&a, VF_ANS_REG_BOOT_STATUS, 4, &v) && v == VF_ANS_BOOT_STATUS_OK);
    assert(!vf_ans_write(&a, VF_ANS_REG_MAX_PEND, 4, 0x11112222u));
    assert(!vf_ans_read(&a, VF_ANS_REG_MAX_PEND, 4, &v) && v == VF_ANS_MAX_PEND_VAL);
    /* CC.EN clear must not wipe sparse vendor_reg[]. */
    assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
    assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
    assert(!vf_ans_read(&a, 0x2000u, 4, &v) && v == 0xbeefu);
    /* Sparse table full → fail closed. */
    {
        uint32_t i;
        vf_ans_v1 full;
        assert(!vf_ans_init(&full));
        for (i = 0; i < VF_ANS_VENDOR_SPARSE_CAP; i++) {
            uint32_t off = 0x1800u + i * 4u;
            assert(!vf_ans_write(&full, off, 4, 0x1000u + i));
            assert(!vf_ans_read(&full, off, 4, &v) && v == (0x1000u + i));
        }
        assert(vf_ans_write(&full, 0x1800u + VF_ANS_VENDOR_SPARSE_CAP * 4u, 4,
                            0xffffffffu) == -1);
    }

    /* is-apple-ans Identify SQES metadata (no command DMA). */
    {
        uint8_t sqes = 0;
        assert(vf_ans_get_is_apple_ans(&a) == 1);
        assert(!vf_ans_identify_sqes(&a, &sqes) && sqes == VF_ANS_ID_SQES_APPLE);
        assert(!vf_ans_set_is_apple_ans(&a, 0));
        assert(vf_ans_get_is_apple_ans(&a) == 0);
        assert(!vf_ans_identify_sqes(&a, &sqes) && sqes == VF_ANS_ID_SQES_STD);
        assert(!vf_ans_set_is_apple_ans(&a, 1));
        assert(!vf_ans_identify_sqes(&a, &sqes) && sqes == VF_ANS_ID_SQES_APPLE);
        assert(vf_ans_identify_sqes(NULL, &sqes) == -1);
        assert(vf_ans_identify_sqes(&a, NULL) == -1);
        assert(vf_ans_set_is_apple_ans(NULL, 1) == -1);
        assert(vf_ans_get_is_apple_ans(NULL) == -1);
    }

    /* Identify CNS=CTRL metadata (CQES/MDTS/NN/ver; no command DMA). */
    {
        uint8_t cqes = 0;
        vf_ans_id_ctrl_meta meta;
        assert(!vf_ans_identify_cns_supported(VF_ANS_ID_CNS_CTRL));
        assert(!vf_ans_identify_cns_supported(VF_ANS_ID_CNS_NS));
        assert(vf_ans_identify_cns_supported(0x02u) == -1);
        assert(!vf_ans_identify_cqes(&a, &cqes) && cqes == VF_ANS_ID_CQES);
        assert(vf_ans_identify_cqes(NULL, &cqes) == -1);
        assert(vf_ans_identify_cqes(&a, NULL) == -1);
        assert(!vf_ans_set_is_apple_ans(&a, 1));
        assert(!vf_ans_identify_cns_ctrl(&a, &meta));
        assert(meta.cns == VF_ANS_ID_CNS_CTRL);
        assert(meta.sqes == VF_ANS_ID_SQES_APPLE);
        assert(meta.cqes == VF_ANS_ID_CQES);
        assert(meta.mdts == VF_ANS_ID_MDTS_APPLE);
        assert(meta.cntrltype == VF_ANS_ID_CNTRLTYPE);
        assert(meta.nn == VF_ANS_ID_NN);
        assert(meta.ver == VF_ANS_ID_VER);
        assert(!vf_ans_set_is_apple_ans(&a, 0));
        assert(!vf_ans_identify_cns_ctrl(&a, &meta));
        assert(meta.sqes == VF_ANS_ID_SQES_STD);
        assert(meta.mdts == VF_ANS_ID_MDTS_STD);
        assert(meta.cqes == VF_ANS_ID_CQES);
        assert(meta.nn == VF_ANS_ID_NN);
        assert(!vf_ans_set_is_apple_ans(&a, 1));
        assert(vf_ans_identify_cns_ctrl(NULL, &meta) == -1);
        assert(vf_ans_identify_cns_ctrl(&a, NULL) == -1);
    }

    /* Identify CNS=NS metadata (zero-capacity stub NSID=1; no DMA). */
    {
        vf_ans_id_ns_meta ns;
        assert(!vf_ans_identify_cns_ns(&a, &ns));
        assert(ns.cns == VF_ANS_ID_CNS_NS);
        assert(ns.nsid == VF_ANS_ID_NS_STUB_NSID);
        assert(ns.nsze == 0 && ns.ncap == 0 && ns.nuse == 0);
        assert(ns.nlbaf == 0 && ns.flbas == 0);
        assert(vf_ans_identify_cns_ns(NULL, &ns) == -1);
        assert(vf_ans_identify_cns_ns(&a, NULL) == -1);
    }

    /* Identify CNS=CTRL DMA fill (metadata → 4096 buffer; no MSI/SQ/PRP). */
    {
        uint8_t buf[VF_ANS_ID_CTRL_SIZE];
        vf_ans_pci_v1 pci;
        uint32_t i, nn, ver;
        /* Fail closed without CC.EN. */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                            VF_ANS_ID_CTRL_SIZE) == -1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_read(&a, VF_ANS_REG_CSTS, 4, &v) &&
               (v & VF_ANS_CSTS_RDY));
        /* Unbound: CC.EN alone is enough for host/test DMA fill. */
        assert(!vf_ans_set_is_apple_ans(&a, 1));
        assert(!vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                             VF_ANS_ID_CTRL_SIZE));
        assert(buf[VF_ANS_ID_OFF_MDTS] == VF_ANS_ID_MDTS_APPLE);
        assert(buf[VF_ANS_ID_OFF_CNTRLTYPE] == VF_ANS_ID_CNTRLTYPE);
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        assert(buf[VF_ANS_ID_OFF_CQES] == VF_ANS_ID_CQES);
        ver = (uint32_t)buf[VF_ANS_ID_OFF_VER] |
              ((uint32_t)buf[VF_ANS_ID_OFF_VER + 1u] << 8) |
              ((uint32_t)buf[VF_ANS_ID_OFF_VER + 2u] << 16) |
              ((uint32_t)buf[VF_ANS_ID_OFF_VER + 3u] << 24);
        nn = (uint32_t)buf[VF_ANS_ID_OFF_NN] |
             ((uint32_t)buf[VF_ANS_ID_OFF_NN + 1u] << 8) |
             ((uint32_t)buf[VF_ANS_ID_OFF_NN + 2u] << 16) |
             ((uint32_t)buf[VF_ANS_ID_OFF_NN + 3u] << 24);
        assert(ver == VF_ANS_ID_VER);
        assert(nn == VF_ANS_ID_NN);
        /* Unmodeled id_ctrl bytes stay zero (no invented VID/SN/MN). */
        for (i = 0; i < 16u; i++) assert(buf[i] == 0);
        assert(!vf_ans_set_is_apple_ans(&a, 0));
        assert(!vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                             VF_ANS_ID_CTRL_SIZE));
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_STD);
        assert(buf[VF_ANS_ID_OFF_MDTS] == VF_ANS_ID_MDTS_STD);
        assert(!vf_ans_set_is_apple_ans(&a, 1));
        /* Wrong CNS / short buffer / NULL fail closed. */
        assert(vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                            VF_ANS_ID_CTRL_SIZE) == -1);
        assert(vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                            VF_ANS_ID_CTRL_SIZE - 1u) == -1);
        assert(vf_ans_identify_cns_ctrl_dma(NULL, VF_ANS_ID_CNS_CTRL, buf,
                                            VF_ANS_ID_CTRL_SIZE) == -1);
        assert(vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, NULL,
                                            VF_ANS_ID_CTRL_SIZE) == -1);
        /* Bound PCI without BusMaster fails closed; enable then succeeds. */
        assert(!vf_ans_pci_init(&pci));
        assert(!vf_ans_bind_pci(&a, &pci));
        assert(vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                            VF_ANS_ID_CTRL_SIZE) == -1);
        assert(!vf_ans_pci_enable_memory_bus_master(&pci));
        assert(!vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                             VF_ANS_ID_CTRL_SIZE));
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        /* Bound mbox without STARTED fails closed; start then succeeds. */
        {
            vf_ans_mbox_v1 mbox;
            assert(!vf_ans_mbox_init(&mbox));
            assert(!vf_ans_bind_mbox(&a, &mbox));
            assert(vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                                VF_ANS_ID_CTRL_SIZE) == -1);
            assert(!vf_ans_mbox_start(&mbox));
            assert(!vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                                 VF_ANS_ID_CTRL_SIZE));
            assert(!vf_ans_bind_mbox(&a, NULL));
            assert(vf_ans_bind_mbox(NULL, &mbox) == -1);
        }
        /* Bound ASCWrap without READY fails closed; mbox start→READY then OK. */
        {
            vf_ascwrap_v1 wrap;
            vf_ans_mbox_v1 mbox;
            assert(!vf_ascwrap_init(&wrap));
            assert(!vf_ans_mbox_init(&mbox));
            assert(!vf_ascwrap_bind_mbox(&wrap, &mbox));
            assert(!vf_ans_bind_ascwrap(&a, &wrap));
            assert(!vf_ascwrap_ready(&wrap));
            assert(vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                                VF_ANS_ID_CTRL_SIZE) == -1);
            assert(!vf_ans_mbox_start(&mbox));
            assert(vf_ascwrap_ready(&wrap) == 1);
            assert(!vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                                 VF_ANS_ID_CTRL_SIZE));
            assert(!vf_ans_bind_ascwrap(&a, NULL));
            assert(vf_ans_bind_ascwrap(NULL, &wrap) == -1);
            assert(!vf_ascwrap_bind_mbox(&wrap, NULL));
        }
        /* Bound autoboot without ARMED fails closed; READY→ARMED then OK. */
        {
            vf_ans_autoboot_v1 ab;
            vf_ascwrap_v1 wrap;
            vf_ans_mbox_v1 mbox;
            assert(!vf_ans_autoboot_init(&ab));
            assert(!vf_ascwrap_init(&wrap));
            assert(!vf_ans_mbox_init(&mbox));
            assert(!vf_ascwrap_bind_mbox(&wrap, &mbox));
            assert(!vf_ans_autoboot_bind_ascwrap(&ab, &wrap));
            assert(!vf_ans_bind_autoboot(&a, &ab));
            assert(!vf_ans_autoboot_armed(&ab));
            assert(vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                                VF_ANS_ID_CTRL_SIZE) == -1);
            assert(!vf_ans_mbox_start(&mbox));
            assert(vf_ans_autoboot_armed(&ab) == 1);
            assert(!vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                                 VF_ANS_ID_CTRL_SIZE));
            assert(!vf_ans_bind_autoboot(&a, NULL));
            assert(vf_ans_bind_autoboot(NULL, &ab) == -1);
            assert(!vf_ans_autoboot_bind_ascwrap(&ab, NULL));
            assert(!vf_ascwrap_bind_mbox(&wrap, NULL));
        }
        /* Unbind restores CC.EN-only path; clear CC.EN fails again. */
        assert(!vf_ans_bind_pci(&a, NULL));
        assert(vf_ans_bind_pci(NULL, &pci) == -1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(vf_ans_identify_cns_ctrl_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                            VF_ANS_ID_CTRL_SIZE) == -1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
    }

    /* Identify CNS=NS DMA fill (zero-capacity stub; no MSI/SQ/PRP). */
    {
        uint8_t buf[VF_ANS_ID_NS_SIZE];
        vf_ans_pci_v1 pci;
        uint32_t i;
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                          VF_ANS_ID_NS_SIZE) == -1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        memset(buf, 0xa5, sizeof(buf));
        assert(!vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                           VF_ANS_ID_NS_SIZE));
        for (i = 0; i < VF_ANS_ID_NS_SIZE; i++) assert(buf[i] == 0);
        assert(vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_NS_SIZE) == -1);
        assert(vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                          VF_ANS_ID_NS_SIZE - 1u) == -1);
        assert(vf_ans_identify_cns_ns_dma(NULL, VF_ANS_ID_CNS_NS, buf,
                                          VF_ANS_ID_NS_SIZE) == -1);
        assert(vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, NULL,
                                          VF_ANS_ID_NS_SIZE) == -1);
        assert(!vf_ans_pci_init(&pci));
        assert(!vf_ans_bind_pci(&a, &pci));
        assert(vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                          VF_ANS_ID_NS_SIZE) == -1);
        assert(!vf_ans_pci_enable_memory_bus_master(&pci));
        assert(!vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                           VF_ANS_ID_NS_SIZE));
        {
            vf_ans_mbox_v1 mbox;
            assert(!vf_ans_mbox_init(&mbox));
            assert(!vf_ans_bind_mbox(&a, &mbox));
            assert(vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                              VF_ANS_ID_NS_SIZE) == -1);
            assert(!vf_ans_mbox_start(&mbox));
            assert(!vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                               VF_ANS_ID_NS_SIZE));
            assert(!vf_ans_bind_mbox(&a, NULL));
        }
        {
            vf_ascwrap_v1 wrap;
            vf_ans_mbox_v1 mbox;
            assert(!vf_ascwrap_init(&wrap));
            assert(!vf_ans_mbox_init(&mbox));
            assert(!vf_ascwrap_bind_mbox(&wrap, &mbox));
            assert(!vf_ans_bind_ascwrap(&a, &wrap));
            assert(vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                              VF_ANS_ID_NS_SIZE) == -1);
            assert(!vf_ans_mbox_start(&mbox));
            assert(!vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                               VF_ANS_ID_NS_SIZE));
            assert(!vf_ans_bind_ascwrap(&a, NULL));
            assert(!vf_ascwrap_bind_mbox(&wrap, NULL));
        }
        {
            vf_ans_autoboot_v1 ab;
            vf_ascwrap_v1 wrap;
            vf_ans_mbox_v1 mbox;
            assert(!vf_ans_autoboot_init(&ab));
            assert(!vf_ascwrap_init(&wrap));
            assert(!vf_ans_mbox_init(&mbox));
            assert(!vf_ascwrap_bind_mbox(&wrap, &mbox));
            assert(!vf_ans_autoboot_bind_ascwrap(&ab, &wrap));
            assert(!vf_ans_bind_autoboot(&a, &ab));
            assert(vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                              VF_ANS_ID_NS_SIZE) == -1);
            assert(!vf_ans_mbox_start(&mbox));
            assert(!vf_ans_identify_cns_ns_dma(&a, VF_ANS_ID_CNS_NS, buf,
                                               VF_ANS_ID_NS_SIZE));
            assert(!vf_ans_bind_autoboot(&a, NULL));
            assert(!vf_ans_autoboot_bind_ascwrap(&ab, NULL));
            assert(!vf_ascwrap_bind_mbox(&wrap, NULL));
        }
        assert(!vf_ans_bind_pci(&a, NULL));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
    }

    /* Admin SQ doorbell → Identify DMA fill + admin CQE / CQH honesty. */
    {
        uint8_t buf[VF_ANS_ID_CTRL_SIZE];
        uint8_t poison[VF_ANS_ID_CTRL_SIZE];
        uint8_t cqe[VF_ANS_CQE_SIZE];
        uint8_t cqe_poison[VF_ANS_CQE_SIZE];
        vf_ans_pci_v1 pci;
        uint32_t i;
        uint32_t dw2;
        uint32_t dw3;
        /* Arm requires Identify + CNS=CTRL|NS + id buf + cq buf. */
        assert(vf_ans_arm_admin_identify(&a, 0x00u, VF_ANS_ID_CNS_CTRL, buf,
                                         VF_ANS_ID_CTRL_SIZE, cqe,
                                         VF_ANS_CQE_SIZE) == -1);
        assert(vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY, 0x02u, buf,
                                         VF_ANS_ID_CTRL_SIZE, cqe,
                                         VF_ANS_CQE_SIZE) == -1);
        assert(vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                         VF_ANS_ID_CNS_CTRL, buf,
                                         VF_ANS_ID_CTRL_SIZE - 1u, cqe,
                                         VF_ANS_CQE_SIZE) == -1);
        assert(vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                         VF_ANS_ID_CNS_CTRL, buf,
                                         VF_ANS_ID_CTRL_SIZE, cqe,
                                         VF_ANS_CQE_SIZE - 1u) == -1);
        assert(vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                         VF_ANS_ID_CNS_CTRL, buf,
                                         VF_ANS_ID_CTRL_SIZE, NULL,
                                         VF_ANS_CQE_SIZE) == -1);
        assert(vf_ans_arm_admin_identify(NULL, VF_ANS_OPC_IDENTIFY,
                                         VF_ANS_ID_CNS_CTRL, buf,
                                         VF_ANS_ID_CTRL_SIZE, cqe,
                                         VF_ANS_CQE_SIZE) == -1);
        assert(vf_ans_admin_identify_armed(NULL) == -1);
        assert(vf_ans_disarm_admin_identify(NULL) == -1);
        assert(vf_ans_admin_cq_tail(NULL) == -1);
        assert(vf_ans_admin_cq_pending(NULL) == -1);
        assert(vf_ans_irq_check(NULL) == -1);
        assert(vf_ans_admin_identify_armed(&a) == 0);
        assert(vf_ans_admin_cq_tail(&a) == 0);
        assert(vf_ans_admin_cq_pending(&a) == 0);
        assert(vf_ans_irq_check(&a) == 0);
        /* Happy path: CC.EN + arm + SQTDBL advance → DMA fill + CQE. */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        memset(buf, 0xa5, sizeof(buf));
        memset(cqe, 0x5a, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(vf_ans_admin_identify_armed(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_admin_identify_armed(&a) == 0); /* one-shot */
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        assert(buf[VF_ANS_ID_OFF_CQES] == VF_ANS_ID_CQES);
        assert(buf[VF_ANS_ID_OFF_MDTS] == VF_ANS_ID_MDTS_APPLE);
        assert(buf[VF_ANS_ID_OFF_CNTRLTYPE] == VF_ANS_ID_CNTRLTYPE);
        for (i = 0; i < 16u; i++) assert(buf[i] == 0);
        /* Simplified CQE: SQHD=1, SQID=0, CID=0, Phase=1, Status=0. */
        dw2 = (uint32_t)cqe[VF_ANS_CQE_OFF_DW2]
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW2 + 1u] << 8)
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW2 + 2u] << 16)
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW2 + 3u] << 24);
        dw3 = (uint32_t)cqe[VF_ANS_CQE_OFF_DW3]
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW3 + 1u] << 8)
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW3 + 2u] << 16)
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW3 + 3u] << 24);
        assert(dw2 == 0x1u);
        assert(dw3 == (1u << VF_ANS_CQE_PHASE_SHIFT));
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_admin_cq_pending(&a) == 1); /* CQH still 0 */
        assert(vf_ans_irq_check(&a) == 1); /* irq_status bit0; intm clear */
        /* INTMS bit0 fail-closed masks irq_check without clearing status. */
        assert(!vf_ans_write(&a, VF_ANS_REG_INTMS, 4, VF_ANS_IRQ_ADMIN_CQ));
        assert(vf_ans_irq_check(&a) == 0);
        assert(vf_ans_admin_cq_pending(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_INTMC, 4, VF_ANS_IRQ_ADMIN_CQ));
        assert(vf_ans_irq_check(&a) == 1); /* unmask re-exposes */
        /* CQH honesty: host drain via CQHDBL clears pending + irq_status. */
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_admin_cq_pending(&a) == 0);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 0);
        /* Same-value rewrite does not re-fire; re-arm required. */
        memset(buf, 0x5a, sizeof(buf));
        memset(cqe, 0x3c, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u)); /* no advance */
        assert(vf_ans_admin_identify_armed(&a) == 1);
        assert(buf[0] == 0x5a);
        assert(cqe[0] == 0x3c);
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x2u)); /* advance */
        assert(vf_ans_admin_identify_armed(&a) == 0);
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        dw3 = (uint32_t)cqe[VF_ANS_CQE_OFF_DW3]
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW3 + 1u] << 8)
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW3 + 2u] << 16)
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW3 + 3u] << 24);
        assert(dw3 == 0u); /* Phase toggled to 0 */
        assert(vf_ans_admin_cq_tail(&a) == 2);
        assert(vf_ans_admin_cq_pending(&a) == 1); /* CQH=1, tail=2 */
        assert(vf_ans_irq_check(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x2u));
        assert(vf_ans_admin_cq_pending(&a) == 0);
        assert(vf_ans_irq_check(&a) == 0);
        /* I/O SQ / admin CQ doorbells must not consume the arm. */
        memset(buf, 0x3c, sizeof(buf));
        memset(cqe, 0x7e, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x9u));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL(1), 4, 0x8u));
        assert(vf_ans_admin_identify_armed(&a) == 1);
        assert(buf[0] == 0x3c);
        assert(cqe[0] == 0x7e);
        assert(!vf_ans_disarm_admin_identify(&a));
        assert(vf_ans_admin_identify_armed(&a) == 0);
        /* Fail closed without CC.EN: doorbell stores; buffers untouched. */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(vf_ans_admin_cq_tail(&a) == 0); /* reset clears CQ Tail/phase */
        assert(vf_ans_admin_cq_pending(&a) == 0);
        assert(vf_ans_irq_check(&a) == 0);
        memset(poison, 0x11, sizeof(poison));
        memset(cqe_poison, 0x22, sizeof(cqe_poison));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x3u));
        assert(vf_ans_admin_identify_armed(&a) == 0);
        assert(poison[0] == 0x11);
        assert(poison[VF_ANS_ID_OFF_SQES] == 0x11);
        assert(cqe_poison[0] == 0x22);
        assert(vf_ans_admin_cq_tail(&a) == 0);
        assert(vf_ans_irq_check(&a) == 0); /* DMA fail: no irq_status */
        /* Bound PCI without BusMaster: arm + doorbell; buffers untouched. */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_pci_init(&pci));
        assert(!vf_ans_bind_pci(&a, &pci));
        memset(poison, 0x22, sizeof(poison));
        memset(cqe_poison, 0x44, sizeof(cqe_poison));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x4u));
        assert(vf_ans_admin_identify_armed(&a) == 0);
        assert(poison[0] == 0x22);
        assert(cqe_poison[0] == 0x44);
        assert(vf_ans_admin_cq_tail(&a) == 0);
        assert(vf_ans_irq_check(&a) == 0);
        assert(!vf_ans_pci_enable_memory_bus_master(&pci));
        memset(poison, 0x33, sizeof(poison));
        memset(cqe_poison, 0x55, sizeof(cqe_poison));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x5u));
        assert(poison[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        assert(cqe_poison[VF_ANS_CQE_OFF_DW2] == 0x5u); /* SQHD */
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 1);
        /* Bound PCI mirrors irq_check into INTx pending (no MSI / AIC). */
        assert(vf_ans_pci_irq_pending(&pci) == 1);
        /* INTMS fail-closed deasserts PCI INTx without clearing irq_status. */
        assert(!vf_ans_write(&a, VF_ANS_REG_INTMS, 4, VF_ANS_IRQ_ADMIN_CQ));
        assert(vf_ans_irq_check(&a) == 0);
        assert(vf_ans_pci_irq_pending(&pci) == 0);
        assert(vf_ans_admin_cq_pending(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_INTMC, 4, VF_ANS_IRQ_ADMIN_CQ));
        assert(vf_ans_irq_check(&a) == 1);
        assert(vf_ans_pci_irq_pending(&pci) == 1);
        /* MSI Enable: irq_check routes to msi_pending; INTx suppressed.
         * Unbound: pending only (fail-closed message write). Bound DMA
         * callback writes prepared addr/data into a host buffer — still
         * not AIC acceptance. */
        {
            ans_msi_sink_t msi_sink;
            memset(&msi_sink, 0, sizeof(msi_sink));
            assert(!vf_ans_pci_write(&pci, VF_ANS_PCI_MMIO_MSI_ADDR, 64,
                                     0xfee00000ull));
            assert(!vf_ans_pci_write(&pci, VF_ANS_PCI_MMIO_MSI_DATA, 32, 0x40u));
            assert(!vf_ans_pci_write(&pci, VF_ANS_PCI_MMIO_MSI_CTRL, 32,
                                     VF_ANS_PCI_MSI_ENABLE));
            assert(vf_ans_pci_msi_enabled(&pci) == 1);
            assert(vf_ans_pci_msi_pending(&pci) == 1);
            assert(!vf_ans_pci_irq_pending(&pci));
            assert(!vf_ans_pci_msi_message_written(&pci));
            assert(vf_ans_pci_msi_addr(&pci) == 0xfee00000ull);
            assert(vf_ans_pci_msi_data(&pci) == 0x40u);
            assert(!vf_ans_write(&a, VF_ANS_REG_INTMS, 4, VF_ANS_IRQ_ADMIN_CQ));
            assert(vf_ans_irq_check(&a) == 0);
            assert(!vf_ans_pci_msi_pending(&pci));
            /* Bind before unmask so irq_check assert delivers message write. */
            assert(!vf_ans_pci_bind_msi_message(&pci, ans_msi_sink_write,
                                               &msi_sink));
            assert(!vf_ans_write(&a, VF_ANS_REG_INTMC, 4, VF_ANS_IRQ_ADMIN_CQ));
            assert(vf_ans_irq_check(&a) == 1);
            assert(vf_ans_pci_msi_pending(&pci) == 1);
            assert(!vf_ans_pci_irq_pending(&pci));
            assert(vf_ans_pci_msi_message_written(&pci) == 1);
            assert(msi_sink.writes == 1);
            assert(msi_sink.last_addr == 0xfee00000ull);
            assert(msi_sink.last_data == 0x40u);
            assert(msi_sink.buf[0] == 0x40u && msi_sink.buf[1] == 0x00u);
            assert(vf_ans_pci_last_msi_message_addr(&pci) == 0xfee00000ull);
            assert(vf_ans_pci_last_msi_message_data(&pci) == 0x40u);
            /* Unbind before disable so INTx re-route does not invent a write. */
            assert(!vf_ans_pci_bind_msi_message(&pci, NULL, NULL));
            assert(!vf_ans_pci_msi_message_written(&pci));
            /* Disable MSI → pending returns to INTx; message write ≠ AIC alone. */
            assert(!vf_ans_pci_write(&pci, VF_ANS_PCI_MMIO_MSI_CTRL, 32, 0));
            assert(!vf_ans_pci_msi_enabled(&pci));
            assert(!vf_ans_pci_msi_pending(&pci));
            assert(vf_ans_pci_irq_pending(&pci) == 1);
            /* MSI-X Enable: irq_check routes to msix_pending; INTx/MSI
             * suppressed. Table BIR prepare only — no PBA/table MMIO write. */
            assert(!vf_ans_pci_write(&pci, VF_ANS_PCI_MMIO_MSIX_TABLE_BIR, 32,
                                     0x1u));
            assert(vf_ans_pci_msix_table_bir(&pci) == 0x1u);
            assert(!vf_ans_pci_write(&pci, VF_ANS_PCI_MMIO_MSIX_CTRL, 32,
                                     VF_ANS_PCI_MSIX_ENABLE));
            assert(vf_ans_pci_msix_enabled(&pci) == 1);
            assert(vf_ans_pci_msix_pending(&pci) == 1);
            assert(!vf_ans_pci_irq_pending(&pci));
            assert(!vf_ans_pci_msi_pending(&pci));
            assert(!vf_ans_write(&a, VF_ANS_REG_INTMS, 4, VF_ANS_IRQ_ADMIN_CQ));
            assert(vf_ans_irq_check(&a) == 0);
            assert(!vf_ans_pci_msix_pending(&pci));
            assert(!vf_ans_write(&a, VF_ANS_REG_INTMC, 4, VF_ANS_IRQ_ADMIN_CQ));
            assert(vf_ans_irq_check(&a) == 1);
            assert(vf_ans_pci_msix_pending(&pci) == 1);
            assert(!vf_ans_pci_irq_pending(&pci));
            /* Disable MSI-X → pending returns to INTx. */
            assert(!vf_ans_pci_write(&pci, VF_ANS_PCI_MMIO_MSIX_CTRL, 32, 0));
            assert(!vf_ans_pci_msix_enabled(&pci));
            assert(!vf_ans_pci_msix_pending(&pci));
            assert(vf_ans_pci_irq_pending(&pci) == 1);
            /* Bind AIC STORAGE line: INTx delivery pending mirrors line 1. */
            {
                vf_aic_v1 aic;
                assert(!vf_aic_init(&aic, 8, 1));
                assert(!vf_aic_write(&aic, 0, 0x4180, 4,
                                     1u << VF_ANS_PCI_AIC_STORAGE_IRQ_LINE));
                assert(!vf_ans_pci_bind_aic(&pci, ans_aic_storage_sink, &aic));
                assert(vf_ans_pci_aic_sync_seen(&pci) == 1);
                assert(vf_ans_pci_aic_line_high(&pci) == 1);
                assert(vf_aic_pending(&aic, 0) == 1);
                /* INTMS fail-closed deasserts PCI + AIC without clearing CQ. */
                assert(!vf_ans_write(&a, VF_ANS_REG_INTMS, 4, VF_ANS_IRQ_ADMIN_CQ));
                assert(vf_ans_irq_check(&a) == 0);
                assert(!vf_ans_pci_delivery_pending(&pci));
                assert(!vf_ans_pci_aic_line_high(&pci));
                assert(!vf_aic_pending(&aic, 0));
                assert(!vf_ans_write(&a, VF_ANS_REG_INTMC, 4, VF_ANS_IRQ_ADMIN_CQ));
                assert(vf_ans_irq_check(&a) == 1);
                assert(vf_ans_pci_irq_pending(&pci) == 1);
                assert(vf_ans_pci_aic_line_high(&pci) == 1);
                assert(vf_aic_pending(&aic, 0) == 1);
                assert(!vf_ans_pci_bind_aic(&pci, NULL, NULL));
            }
        }
        /* CQH drain clears irq_status and PCI INTx. */
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_irq_check(&a) == 0);
        assert(vf_ans_pci_irq_pending(&pci) == 0);
        /* Re-arm + doorbell to reassert before CC.EN clear path. */
        memset(poison, 0x33, sizeof(poison));
        memset(cqe_poison, 0x55, sizeof(cqe_poison));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x6u));
        assert(vf_ans_irq_check(&a) == 1);
        assert(vf_ans_pci_irq_pending(&pci) == 1);
        /* CC.EN clear disarms without DMA and resets CQ Tail + irq_status. */
        memset(poison, 0x44, sizeof(poison));
        memset(cqe_poison, 0x66, sizeof(cqe_poison));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(vf_ans_admin_identify_armed(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(vf_ans_admin_identify_armed(&a) == 0);
        assert(poison[0] == 0x44);
        assert(cqe_poison[0] == 0x66);
        assert(vf_ans_admin_cq_tail(&a) == 0);
        assert(vf_ans_irq_check(&a) == 0);
        assert(vf_ans_pci_irq_pending(&pci) == 0);
        assert(!vf_ans_bind_pci(&a, NULL));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
    }

    /* Guest-visible ASQ fetch + PRP/SGL honesty (optional bind; no ACQ MMIO). */
    {
        uint8_t asq[VF_ANS_SQE_SIZE * 2u];
        uint8_t buf[VF_ANS_ID_CTRL_SIZE];
        uint8_t cqe[VF_ANS_CQE_SIZE];
        uint8_t poison[VF_ANS_ID_CTRL_SIZE];
        uint8_t cqe_poison[VF_ANS_CQE_SIZE];
        uint32_t asq_lo_saved;
        uint32_t asq_hi_saved;
        assert(vf_ans_admin_asq_bound(NULL) == -1);
        assert(vf_ans_bind_admin_asq(NULL, asq, sizeof(asq)) == -1);
        assert(vf_ans_bind_admin_asq(&a, asq, VF_ANS_SQE_SIZE - 1u) == -1);
        assert(vf_ans_admin_asq_bound(&a) == 0);
        assert(!vf_ans_read(&a, VF_ANS_REG_ASQ_LO, 4, &asq_lo_saved));
        assert(!vf_ans_read(&a, VF_ANS_REG_ASQ_HI, 4, &asq_hi_saved));
        assert(asq_lo_saved != 0u || asq_hi_saved != 0u); /* set in CC path */
        /* Happy path: bound ASQ + Identify SQE + PRP1!=0 → DMA + CQE + irq. */
        fill_identify_sqe(asq, 0x1000u, VF_ANS_ID_CNS_CTRL, 0);
        assert(!vf_ans_bind_admin_asq(&a, asq, sizeof(asq)));
        assert(vf_ans_admin_asq_bound(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        memset(buf, 0xa5, sizeof(buf));
        memset(cqe, 0x5a, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_admin_identify_armed(&a) == 0);
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_irq_check(&a) == 0);
        /* SGL (PSDT=1) fail-closed: buffers untouched; no CQE / irq. */
        fill_identify_sqe(asq + VF_ANS_SQE_SIZE, 0x2000u, VF_ANS_ID_CNS_CTRL,
                          1u);
        memset(poison, 0x11, sizeof(poison));
        memset(cqe_poison, 0x22, sizeof(cqe_poison));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x2u));
        assert(vf_ans_admin_identify_armed(&a) == 0);
        assert(poison[0] == 0x11);
        assert(poison[VF_ANS_ID_OFF_SQES] == 0x11);
        assert(cqe_poison[0] == 0x22);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 0);
        /* Zero PRP1 fail-closed. */
        fill_identify_sqe(asq, 0u, VF_ANS_ID_CNS_CTRL, 0);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0u)); /* reset head */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0)); /* clear doorbells/tail */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, asq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, asq_hi_saved));
        memset(poison, 0x33, sizeof(poison));
        memset(cqe_poison, 0x44, sizeof(cqe_poison));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(poison[0] == 0x33);
        assert(cqe_poison[0] == 0x44);
        assert(vf_ans_admin_cq_tail(&a) == 0);
        assert(vf_ans_irq_check(&a) == 0);
        /* Wrong CNS in ASQ fail-closed. */
        fill_identify_sqe(asq + VF_ANS_SQE_SIZE, 0x3000u, VF_ANS_ID_CNS_NS, 0);
        memset(poison, 0x55, sizeof(poison));
        memset(cqe_poison, 0x66, sizeof(cqe_poison));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x2u));
        assert(poison[0] == 0x55);
        assert(cqe_poison[0] == 0x66);
        assert(vf_ans_admin_cq_tail(&a) == 0);
        /* Bound ASQ with ASQ base registers cleared fail-closed. */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, 0));
        fill_identify_sqe(asq, 0x4000u, VF_ANS_ID_CNS_CTRL, 0);
        memset(poison, 0x77, sizeof(poison));
        memset(cqe_poison, 0x88, sizeof(cqe_poison));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(poison[0] == 0x77);
        assert(cqe_poison[0] == 0x88);
        assert(vf_ans_admin_cq_tail(&a) == 0);
        /* Unbind restores legacy arm-only path (no ASQ fetch). */
        assert(!vf_ans_bind_admin_asq(&a, NULL, 0));
        assert(vf_ans_admin_asq_bound(&a) == 0);
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, asq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, asq_hi_saved));
        memset(buf, 0x99, sizeof(buf));
        memset(cqe, 0xaa, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x2u));
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, asq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, asq_hi_saved));
    }

    /* Guest-visible ACQ CQE write (optional bind; no MSI / AIC / PRP list). */
    {
        uint8_t acq[VF_ANS_CQE_SIZE * 2u];
        uint8_t buf[VF_ANS_ID_CTRL_SIZE];
        uint8_t cqe[VF_ANS_CQE_SIZE];
        uint8_t poison[VF_ANS_ID_CTRL_SIZE];
        uint8_t cqe_poison[VF_ANS_CQE_SIZE];
        uint32_t acq_lo_saved;
        uint32_t acq_hi_saved;
        uint32_t dw3;
        assert(vf_ans_admin_acq_bound(NULL) == -1);
        assert(vf_ans_bind_admin_acq(NULL, acq, sizeof(acq)) == -1);
        assert(vf_ans_bind_admin_acq(&a, acq, VF_ANS_CQE_SIZE - 1u) == -1);
        assert(vf_ans_admin_acq_bound(&a) == 0);
        assert(!vf_ans_read(&a, VF_ANS_REG_ACQ_LO, 4, &acq_lo_saved));
        assert(!vf_ans_read(&a, VF_ANS_REG_ACQ_HI, 4, &acq_hi_saved));
        assert(acq_lo_saved != 0u || acq_hi_saved != 0u); /* set in CC path */
        /* Happy path: bound ACQ → CQE at ACQ[0] + cq_buf + Tail + irq. */
        memset(acq, 0xee, sizeof(acq));
        assert(!vf_ans_bind_admin_acq(&a, acq, sizeof(acq)));
        assert(vf_ans_admin_acq_bound(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        memset(buf, 0xa5, sizeof(buf));
        memset(cqe, 0x5a, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_admin_identify_armed(&a) == 0);
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 1);
        assert(memcmp(acq, cqe, VF_ANS_CQE_SIZE) == 0);
        dw3 = (uint32_t)cqe[VF_ANS_CQE_OFF_DW3]
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW3 + 1u] << 8)
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW3 + 2u] << 16)
            | ((uint32_t)cqe[VF_ANS_CQE_OFF_DW3 + 3u] << 24);
        assert(((dw3 >> VF_ANS_CQE_PHASE_SHIFT) & 1u) == 1u); /* phase 1 */
        assert(acq[VF_ANS_CQE_SIZE] == 0xee); /* second slot untouched */
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_irq_check(&a) == 0);
        /* Second CQE lands at ACQ[1] (Tail was 1). */
        memset(buf, 0xb5, sizeof(buf));
        memset(cqe, 0x6a, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x2u));
        assert(vf_ans_admin_cq_tail(&a) == 2);
        assert(memcmp(acq + VF_ANS_CQE_SIZE, cqe, VF_ANS_CQE_SIZE) == 0);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x2u));
        /* Short ACQ page fail-closed (Tail=1 needs offset 16; len=16 → miss). */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ACQ_LO, 4, acq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ACQ_HI, 4, acq_hi_saved));
        /* Advance Tail to 1 via unbound path, then bind a one-slot ACQ. */
        assert(!vf_ans_bind_admin_acq(&a, NULL, 0));
        memset(buf, 0xc5, sizeof(buf));
        memset(cqe, 0x7a, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        assert(!vf_ans_bind_admin_acq(&a, acq, VF_ANS_CQE_SIZE)); /* Tail=1 → need 32 */
        memset(poison, 0x11, sizeof(poison));
        memset(cqe_poison, 0x22, sizeof(cqe_poison));
        memset(acq, 0xdd, sizeof(acq));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x2u));
        assert(poison[0] == 0x11);
        assert(poison[VF_ANS_ID_OFF_SQES] == 0x11);
        assert(cqe_poison[0] == 0x22);
        assert(acq[0] == 0xdd);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 0);
        /* Bound ACQ with ACQ base registers cleared fail-closed. */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ACQ_LO, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_ACQ_HI, 4, 0));
        assert(!vf_ans_bind_admin_acq(&a, acq, sizeof(acq)));
        memset(poison, 0x33, sizeof(poison));
        memset(cqe_poison, 0x44, sizeof(cqe_poison));
        memset(acq, 0xcc, sizeof(acq));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(poison[0] == 0x33);
        assert(cqe_poison[0] == 0x44);
        assert(acq[0] == 0xcc);
        assert(vf_ans_admin_cq_tail(&a) == 0);
        assert(vf_ans_irq_check(&a) == 0);
        /* Unbind restores cq_buf-only legacy path. */
        assert(!vf_ans_bind_admin_acq(&a, NULL, 0));
        assert(vf_ans_admin_acq_bound(&a) == 0);
        assert(!vf_ans_write(&a, VF_ANS_REG_ACQ_LO, 4, acq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ACQ_HI, 4, acq_hi_saved));
        memset(acq, 0xbb, sizeof(acq));
        memset(buf, 0x99, sizeof(buf));
        memset(cqe, 0xaa, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x2u));
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 1);
        assert(acq[0] == 0xbb); /* unbound: guest ACQ untouched */
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ACQ_LO, 4, acq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ACQ_HI, 4, acq_hi_saved));
    }

    /* Identify PRP1/PRP2 GPA→host buffer translate (optional binds; no list). */
    {
        uint8_t asq[VF_ANS_SQE_SIZE * 2u];
        uint8_t prp1_page[VF_ANS_ID_CTRL_SIZE];
        uint8_t prp2_page[VF_ANS_ID_CTRL_SIZE];
        uint8_t buf[VF_ANS_ID_CTRL_SIZE];
        uint8_t cqe[VF_ANS_CQE_SIZE];
        uint8_t poison[VF_ANS_ID_CTRL_SIZE];
        uint8_t cqe_poison[VF_ANS_CQE_SIZE];
        uint8_t expect[VF_ANS_ID_CTRL_SIZE];
        uint32_t asq_lo_saved;
        uint32_t asq_hi_saved;
        uint32_t prp_off;
        uint32_t first;
        assert(vf_ans_identify_prp1_bound(NULL) == -1);
        assert(vf_ans_identify_prp2_bound(NULL) == -1);
        assert(vf_ans_bind_identify_prp1(NULL, prp1_page, sizeof(prp1_page))
               == -1);
        assert(vf_ans_bind_identify_prp2(NULL, prp2_page, sizeof(prp2_page))
               == -1);
        assert(vf_ans_bind_identify_prp1(&a, prp1_page,
                                         VF_ANS_ID_CTRL_SIZE - 1u) == -1);
        assert(vf_ans_bind_identify_prp2(&a, prp2_page,
                                         VF_ANS_ID_CTRL_SIZE - 1u) == -1);
        assert(vf_ans_identify_prp1_bound(&a) == 0);
        assert(vf_ans_identify_prp2_bound(&a) == 0);
        assert(!vf_ans_read(&a, VF_ANS_REG_ASQ_LO, 4, &asq_lo_saved));
        assert(!vf_ans_read(&a, VF_ANS_REG_ASQ_HI, 4, &asq_hi_saved));
        /* Happy path: ASQ + page-aligned PRP1 + PRP2==0 → DMA into PRP1 page. */
        fill_identify_sqe(asq, 0x1000u, VF_ANS_ID_CNS_CTRL, 0);
        assert(!vf_ans_bind_admin_asq(&a, asq, sizeof(asq)));
        memset(prp1_page, 0xee, sizeof(prp1_page));
        assert(!vf_ans_bind_identify_prp1(&a, prp1_page, sizeof(prp1_page)));
        assert(vf_ans_identify_prp1_bound(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        memset(buf, 0xa5, sizeof(buf));
        memset(cqe, 0x5a, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_admin_identify_armed(&a) == 0);
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        assert(prp1_page[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        assert(prp1_page[VF_ANS_ID_OFF_CQES] == VF_ANS_ID_CQES);
        assert(prp1_page[VF_ANS_ID_OFF_MDTS] == VF_ANS_ID_MDTS_APPLE);
        assert(memcmp(prp1_page, buf, VF_ANS_ID_CTRL_SIZE) == 0);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        /* Unaligned PRP1 without PRP2 bind fail-closed. */
        fill_identify_sqe(asq + VF_ANS_SQE_SIZE, 0x1080u, VF_ANS_ID_CNS_CTRL,
                          0);
        memset(poison, 0x11, sizeof(poison));
        memset(cqe_poison, 0x22, sizeof(cqe_poison));
        memset(prp1_page, 0xdd, sizeof(prp1_page));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x2u));
        assert(poison[0] == 0x11);
        assert(poison[VF_ANS_ID_OFF_SQES] == 0x11);
        assert(cqe_poison[0] == 0x22);
        assert(prp1_page[0] == 0xdd);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 0);
        /* Aligned PRP1 + non-zero PRP2 fail-closed (page-list never walked). */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, asq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, asq_hi_saved));
        fill_identify_sqe_prp(asq, 0x2000u, 0x3000u, VF_ANS_ID_CNS_CTRL, 0);
        memset(poison, 0x33, sizeof(poison));
        memset(cqe_poison, 0x44, sizeof(cqe_poison));
        memset(prp1_page, 0xcc, sizeof(prp1_page));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(poison[0] == 0x33);
        assert(cqe_poison[0] == 0x44);
        assert(prp1_page[0] == 0xcc);
        assert(vf_ans_admin_cq_tail(&a) == 0);
        assert(vf_ans_irq_check(&a) == 0);
        /* Unaligned PRP1 + page-aligned PRP2 → split across PRP1+PRP2 pages. */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, asq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, asq_hi_saved));
        prp_off = 0x80u;
        first = VF_ANS_PRP_PAGE_SIZE - prp_off;
        fill_identify_sqe_prp(asq, 0x1000u + prp_off, 0x3000u,
                              VF_ANS_ID_CNS_CTRL, 0);
        memset(prp1_page, 0xaa, sizeof(prp1_page));
        memset(prp2_page, 0xbb, sizeof(prp2_page));
        assert(!vf_ans_bind_identify_prp2(&a, prp2_page, sizeof(prp2_page)));
        assert(vf_ans_identify_prp2_bound(&a) == 1);
        memset(buf, 0x77, sizeof(buf));
        memset(cqe, 0x88, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_admin_identify_armed(&a) == 0);
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        memcpy(expect, buf, VF_ANS_ID_CTRL_SIZE);
        assert(memcmp(prp1_page + prp_off, expect, first) == 0);
        assert(memcmp(prp2_page, expect + first,
                      VF_ANS_ID_CTRL_SIZE - first) == 0);
        assert(prp1_page[0] == 0xaa); /* bytes before PRP1 offset untouched */
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        /* Unaligned PRP2 (would be list pointer) fail-closed. */
        fill_identify_sqe_prp(asq + VF_ANS_SQE_SIZE, 0x2080u, 0x3080u,
                              VF_ANS_ID_CNS_CTRL, 0);
        memset(poison, 0x55, sizeof(poison));
        memset(cqe_poison, 0x66, sizeof(cqe_poison));
        memset(prp1_page, 0x11, sizeof(prp1_page));
        memset(prp2_page, 0x22, sizeof(prp2_page));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, poison,
                                          VF_ANS_ID_CTRL_SIZE, cqe_poison,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x2u));
        assert(poison[0] == 0x55);
        assert(cqe_poison[0] == 0x66);
        assert(prp1_page[0] == 0x11);
        assert(prp2_page[0] == 0x22);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 0);
        /* Unbind PRP1/PRP2: ASQ still fetches; PRP1!=0 honesty only. */
        assert(!vf_ans_bind_identify_prp1(&a, NULL, 0));
        assert(!vf_ans_bind_identify_prp2(&a, NULL, 0));
        assert(vf_ans_identify_prp1_bound(&a) == 0);
        assert(vf_ans_identify_prp2_bound(&a) == 0);
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, asq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, asq_hi_saved));
        fill_identify_sqe(asq, 0x4000u, VF_ANS_ID_CNS_CTRL, 0);
        memset(prp1_page, 0xbb, sizeof(prp1_page));
        memset(buf, 0x99, sizeof(buf));
        memset(cqe, 0xaa, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_CTRL, buf,
                                          VF_ANS_ID_CTRL_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(buf[VF_ANS_ID_OFF_SQES] == VF_ANS_ID_SQES_APPLE);
        assert(prp1_page[0] == 0xbb); /* unbound: PRP1 stand-in untouched */
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        assert(!vf_ans_bind_admin_asq(&a, NULL, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, asq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, asq_hi_saved));
    }

    /* Identify CNS=NS doorbell path reuses CQ/irq/INTx; ASQ NSID honesty. */
    {
        uint8_t buf[VF_ANS_ID_NS_SIZE];
        uint8_t cqe[VF_ANS_CQE_SIZE];
        uint8_t asq[VF_ANS_SQE_SIZE * 2u];
        uint8_t prp1_page[VF_ANS_ID_NS_SIZE];
        vf_ans_pci_v1 pci;
        uint32_t i;
        uint32_t asq_lo_saved = 0;
        uint32_t asq_hi_saved = 0;
        assert(!vf_ans_read(&a, VF_ANS_REG_ASQ_LO, 4, &asq_lo_saved));
        assert(!vf_ans_read(&a, VF_ANS_REG_ASQ_HI, 4, &asq_hi_saved));
        /* Arm-only CNS=NS → zero-capacity DMA + CQE + irq_check. */
        memset(buf, 0xa5, sizeof(buf));
        memset(cqe, 0x5a, sizeof(cqe));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_NS, buf,
                                          VF_ANS_ID_NS_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(vf_ans_admin_identify_armed(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_admin_identify_armed(&a) == 0);
        for (i = 0; i < VF_ANS_ID_NS_SIZE; i++) assert(buf[i] == 0);
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_irq_check(&a) == 0);
        /* Bound ASQ: NSID=0 fail-closed; stub NSID=1 succeeds + PRP1 copy. */
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, asq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, asq_hi_saved));
        assert(!vf_ans_bind_admin_asq(&a, asq, sizeof(asq)));
        assert(!vf_ans_bind_identify_prp1(&a, prp1_page, sizeof(prp1_page)));
        fill_identify_sqe_prp_nsid(asq, 0x1000u, 0u, VF_ANS_ID_CNS_NS, 0, 0u);
        memset(buf, 0x11, sizeof(buf));
        memset(cqe, 0x22, sizeof(cqe));
        memset(prp1_page, 0x33, sizeof(prp1_page));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_NS, buf,
                                          VF_ANS_ID_NS_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        assert(buf[0] == 0x11); /* NSID=0 fail-closed: buffers untouched */
        assert(prp1_page[0] == 0x33);
        assert(vf_ans_admin_cq_tail(&a) == 0);
        assert(vf_ans_irq_check(&a) == 0);
        fill_identify_sqe_prp_nsid(asq + VF_ANS_SQE_SIZE, 0x2000u, 0u,
                                   VF_ANS_ID_CNS_NS, 0, 2u);
        memset(buf, 0x44, sizeof(buf));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_NS, buf,
                                          VF_ANS_ID_NS_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x2u));
        assert(buf[0] == 0x44); /* NSID=2 fail-closed */
        assert(vf_ans_admin_cq_tail(&a) == 0);
        fill_identify_sqe_prp_nsid(asq, 0x3000u, 0u, VF_ANS_ID_CNS_NS, 0,
                                   VF_ANS_ID_NS_STUB_NSID);
        memset(buf, 0x55, sizeof(buf));
        memset(cqe, 0x66, sizeof(cqe));
        memset(prp1_page, 0x77, sizeof(prp1_page));
        assert(!vf_ans_pci_init(&pci));
        assert(!vf_ans_bind_pci(&a, &pci));
        assert(!vf_ans_pci_enable_memory_bus_master(&pci));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, asq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, asq_hi_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0));
        assert(!vf_ans_arm_admin_identify(&a, VF_ANS_OPC_IDENTIFY,
                                          VF_ANS_ID_CNS_NS, buf,
                                          VF_ANS_ID_NS_SIZE, cqe,
                                          VF_ANS_CQE_SIZE));
        assert(!vf_ans_write(&a, VF_ANS_REG_SQTDBL_ADMIN, 4, 0x1u));
        for (i = 0; i < VF_ANS_ID_NS_SIZE; i++) {
            assert(buf[i] == 0);
            assert(prp1_page[i] == 0);
        }
        assert(vf_ans_admin_cq_tail(&a) == 1);
        assert(vf_ans_irq_check(&a) == 1);
        assert(vf_ans_pci_irq_pending(&pci) == 1);
        assert(!vf_ans_write(&a, VF_ANS_REG_CQHDBL_ADMIN, 4, 0x1u));
        assert(vf_ans_pci_irq_pending(&pci) == 0);
        assert(!vf_ans_bind_identify_prp1(&a, NULL, 0));
        assert(!vf_ans_bind_admin_asq(&a, NULL, 0));
        assert(!vf_ans_bind_pci(&a, NULL));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, 0));
        assert(!vf_ans_write(&a, VF_ANS_REG_CC, 4, VF_ANS_CC_EN));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_LO, 4, asq_lo_saved));
        assert(!vf_ans_write(&a, VF_ANS_REG_ASQ_HI, 4, asq_hi_saved));
    }

    assert(vf_ans_read(&a, VF_ANS_REG_BOOT_STATUS, 8, &v) == -1);
    assert(vf_ans_read(&a, VF_ANS_VENDOR_SIZE, 4, &v) == -1);
    puts("PASS ANS v1 vendor MMIO: CAP/VS + INTMS/INTMC + CC/CSTS.RDY + AQA/ASQ/ACQ + SQ/CQ doorbells (admin+I/O) + sparse vendor_reg + RO-zero alias gaps + Identify CNS=CTRL|NS metadata+DMA fill + admin SQ Identify submit + admin CQ completion/CQH honesty + irq_check/INTMS mask + bound PCI INTx/MSI/MSI-X pending + bound MSI message write sync + bound AIC STORAGE line pending mirror + bound mbox STARTED Identify DMA gate + bound ASCWrap READY Identify DMA gate + bound autoboot ARMED Identify DMA gate + guest ASQ fetch/PRP honesty + guest ACQ CQE MMIO + Identify PRP1/PRP2 GPA translate + is-apple-ans sqes, sticky boot/max-pend/base-cmd, modesel, linear sq, bounds");
    return 0;
}
