/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_ANS_V1_H
#define VENFIRE_ANS_V1_H
#include <stdint.h>
#include "ans_pci_v1.h"
#include "ans_mbox_v1.h"
#include "ascwrap_v1.h"
#include "ans_autoboot_v1.h"

/* Apple ANS2 vendor MMIO within the qemu-t8030 composite block (0x60000 bytes).
 * Standard NVMe BAR registers occupy offsets 0x0000..0x11FF in the same window. */
#define VF_ANS_VENDOR_SIZE       0x60000u
#define VF_ANS_NVME_ALIAS_SIZE   0x1200u

/* NVMe controller register alias (u32 slices of CAP / VS / INTMS / INTMC /
 * CC / CSTS / queues). CAP/VS values match qemu-t8030 hw/nvme/ctrl.c
 * nvme_init_ctrl defaults used by apple_ans (MQES=0x7ff, CQR, TO=0xf,
 * CSS=NVM|CSI|ADMIN_ONLY, MPSMAX=4; CMBS/PMRS=0; DSTRD=0 → doorbell stride 4).
 * INTMS/INTMC mirror one mask (set OR / clear AND~); CC.EN → CSTS.RDY; SQ/CQ
 * doorbells (admin + I/O) store/read low-16 only.
 * Identify CNS=CTRL metadata + host/test DMA fill (SQES/CQES/MDTS/NN) follow
 * is-apple-ans; DMA fill requires CC.EN (+ bound BusMaster when PCI linked;
 * + bound mbox STARTED when mailbox linked; + bound ASCWrap READY when
 * ASCWrap linked; + bound autoboot ARMED when autoboot linked).
 * Admin SQTDBL advance with an armed Identify CNS=CTRL simplified SQE drives
 * that DMA fill (host/test); when a guest-visible ASQ page is bound, the
 * doorbell path fetches the 64-byte SQE from ASQ[old Tail] and fail-closes on
 * SGL (PSDT!=0) or zero PRP1; when a guest-visible Identify PRP1 page is also
 * bound, page-aligned PRP1 requires PRP2==0 and copies the full 4096-byte
 * payload into the PRP1 stand-in; when PRP2 is also bound, unaligned PRP1
 * plus page-aligned non-zero PRP2 splits the payload across PRP1 remainder +
 * PRP2 second data page (PRP list entries are never walked); on DMA success a
 * simplified admin CQE is written into the armed cq_buf and, when a
 * guest-visible ACQ page is bound, also into ACQ[CQ Tail] (fail-closed on
 * short page / ACQ base zero); CQ Tail advances (CQHDBL remains host drain);
 * irq_status bit0 is set and vf_ans_irq_check reports pending when unmasked
 * by INTMS; bound PCI mirrors that into INTx or MSI pending (optional bound
 * MSI message write via vf_ans_pci_bind_msi_message — message write ≠ AIC
 * alone; optional bound AIC STORAGE-line mirror via vf_ans_pci_bind_aic —
 * ≠ window 0x4 / Apple DT IRQ; no PRP list walk / real GPA walk of ASQ/ACQ
 * bases).
 * Unmodeled alias gaps (0x0000..0x11FF outside named regs) are RO-zero.
 * Unmodeled vendor offsets (0x1200..0x5FFFF) use sparse vendor_reg store/read. */
#define VF_ANS_IRQ_ADMIN_CQ      (1u << 0) /* irq_status / INTMS vector 0 */
#define VF_ANS_REG_CAP_LO        0x0000u
#define VF_ANS_REG_CAP_HI        0x0004u
#define VF_ANS_REG_VS            0x0008u
#define VF_ANS_REG_INTMS         0x000Cu /* Interrupt Mask Set (write OR) */
#define VF_ANS_REG_INTMC         0x0010u /* Interrupt Mask Clear (write AND~) */
#define VF_ANS_REG_CC            0x0014u
#define VF_ANS_REG_CSTS          0x001Cu
#define VF_ANS_REG_AQA           0x0024u
#define VF_ANS_REG_ASQ_LO        0x0028u
#define VF_ANS_REG_ASQ_HI        0x002Cu
#define VF_ANS_REG_ACQ_LO        0x0030u
#define VF_ANS_REG_ACQ_HI        0x0034u
/* Doorbells @ 0x1000..0x11FF (DSTRD=0): per qid SQ tail then CQ head.
 * Admin qid0 @ 0x1000/0x1004; I/O qid≥1 @ 0x1008+; stride 4 bytes. */
#define VF_ANS_DB_BASE           0x1000u
#define VF_ANS_DB_END            VF_ANS_NVME_ALIAS_SIZE /* exclusive; 0x1200 */
#define VF_ANS_DB_COUNT          ((VF_ANS_DB_END - VF_ANS_DB_BASE) / 4u) /* 128 */
#define VF_ANS_REG_SQTDBL_ADMIN  0x1000u
#define VF_ANS_REG_CQHDBL_ADMIN  0x1004u
#define VF_ANS_REG_SQTDBL(qid)   (VF_ANS_DB_BASE + (2u * (uint32_t)(qid)) * 4u)
#define VF_ANS_REG_CQHDBL(qid)   (VF_ANS_DB_BASE + (2u * (uint32_t)(qid) + 1u) * 4u)
#define VF_ANS_CAP_LO_VAL        0x0f0107ffu
#define VF_ANS_CAP_HI_VAL        0x00401820u
#define VF_ANS_VS_VAL            0x00010400u /* NVME_SPEC_VER 1.4 */
#define VF_ANS_CC_EN             (1u << 0)
#define VF_ANS_CSTS_RDY          (1u << 0)
#define VF_ANS_DB_VALUE_MASK     0xffffu /* NVMe doorbell low 16 bits */
/* Identify CNS values (NVMe Identify command CNS field). Host/test metadata
 * + DMA fill cover CNS=CTRL and CNS=NS (zero-capacity stub NSID=1); lists
 * and other CNS values remain deferred. */
#define VF_ANS_ID_CNS_NS         0x00u /* Identify Namespace — zero-cap stub */
#define VF_ANS_ID_CNS_CTRL       0x01u /* Identify Controller — metadata + DMA fill */
/* NVMe admin Identify opcode (simplified host/test SQE shape). */
#define VF_ANS_OPC_IDENTIFY      0x06u
/* Identify Controller / Namespace data size (NVMe Identify 4096-byte payload). */
#define VF_ANS_ID_CTRL_SIZE      4096u
#define VF_ANS_ID_NS_SIZE        4096u /* same payload size as id_ctrl */
/* NVMe 1.4 Identify Controller field offsets (little-endian layout). */
#define VF_ANS_ID_OFF_MDTS       77u
#define VF_ANS_ID_OFF_VER        80u
#define VF_ANS_ID_OFF_CNTRLTYPE  111u
#define VF_ANS_ID_OFF_SQES       512u
#define VF_ANS_ID_OFF_CQES       513u
#define VF_ANS_ID_OFF_NN         516u
/* NVMe Identify Namespace field offsets (little-endian layout). */
#define VF_ANS_ID_NS_OFF_NSZE    0u   /* le64 */
#define VF_ANS_ID_NS_OFF_NCAP    8u   /* le64 */
#define VF_ANS_ID_NS_OFF_NUSE    16u  /* le64 */
#define VF_ANS_ID_NS_OFF_NLBAF   25u  /* u8 */
#define VF_ANS_ID_NS_OFF_FLBAS   26u  /* u8 */
/* Identify Controller SQES (id_ctrl.sqes): max<<4 | required.
 * qemu-t8030 ctrl.c: is-apple-ans → IOSQES max 7; else both 6. */
#define VF_ANS_ID_SQES_APPLE     ((0x7u << 4) | 0x6u) /* 0x76 */
#define VF_ANS_ID_SQES_STD       ((0x6u << 4) | 0x6u) /* 0x66 */
/* CQES always (0x4<<4)|0x4 in nvme_init_ctrl (Apple and std). */
#define VF_ANS_ID_CQES           ((0x4u << 4) | 0x4u) /* 0x44 */
/* apple_ans.c sets mdts=8; generic NVMe prop default is 7. */
#define VF_ANS_ID_MDTS_APPLE     8u
#define VF_ANS_ID_MDTS_STD       7u
#define VF_ANS_ID_NN             16u /* NVME_MAX_NAMESPACES */
#define VF_ANS_ID_CNTRLTYPE      0x1u
#define VF_ANS_ID_VER            VF_ANS_VS_VAL /* id_ctrl.ver == bar.vs */
/* Only stub NSID=1 exists for CNS=NS (zero capacity; no block backend). */
#define VF_ANS_ID_NS_STUB_NSID   1u
/* NVMe Submission Queue Entry (64-byte) — guest-visible ASQ fetch honesty. */
#define VF_ANS_SQE_SIZE          64u
#define VF_ANS_SQE_OFF_CDW0      0u
#define VF_ANS_SQE_OFF_NSID      4u  /* little-endian u32; CNS=NS requires stub */
#define VF_ANS_SQE_OFF_PRP1      24u /* little-endian u64 */
#define VF_ANS_SQE_OFF_PRP2      32u /* little-endian u64; unused for aligned 4K */
#define VF_ANS_SQE_OFF_CDW10     40u /* Identify CNS in low 8 bits */
#define VF_ANS_SQE_PSDT_SHIFT    14u
#define VF_ANS_SQE_PSDT_MASK     0x3u /* CDW0[15:14]: 00=PRP, else SGL */
/* NVMe PRP page size / offset mask (MPS=4KiB assumed; CAP.MPSMAX=4). */
#define VF_ANS_PRP_PAGE_SIZE     0x1000u
#define VF_ANS_PRP_PAGE_MASK     (VF_ANS_PRP_PAGE_SIZE - 1u)
/* NVMe Completion Queue Entry (host/test simplified admin CQE). */
#define VF_ANS_CQE_SIZE          16u
#define VF_ANS_CQE_OFF_DW2       8u  /* SQHD[15:0] | SQID[31:16] */
#define VF_ANS_CQE_OFF_DW3       12u /* CID[15:0] | P[16] | Status[31:17] */
#define VF_ANS_CQE_PHASE_SHIFT   16u

#define VF_ANS_REG_MAX_PEND      0x1210u
#define VF_ANS_REG_BOOT_STATUS   0x1300u
#define VF_ANS_REG_MODESEL       0x1304u
#define VF_ANS_REG_BASE_CMD_ID   0x1308u
#define VF_ANS_REG_LINEAR_SQ     0x24908u

#define VF_ANS_MAX_PEND_VAL      ((64u << 16) | 64u)
#define VF_ANS_BOOT_STATUS_OK    0xde71ce55u
#define VF_ANS_BASE_CMD_ID_VAL   0x6000u
#define VF_ANS_LINEAR_SQ_EN      (1u << 0)

/* Sparse vendor_reg[] capacity for unmodeled Apple vendor offsets
 * (0x1200..0x5FFFF). Reference uses a dense 0x60000/4 array; this stub keeps
 * a bounded sparse table (fail-closed when full). Named vendor regs
 * (MAX_PEND/BOOT_STATUS/MODESEL/BASE_CMD_ID/LINEAR_SQ) stay dedicated. */
#define VF_ANS_VENDOR_SPARSE_CAP 48u

typedef struct {
    uint32_t off;
    uint32_t val;
} vf_ans_vendor_slot;

typedef struct {
    uint32_t modesel;
    uint32_t linear_sq;
    uint32_t base_cmd_id;
    uint32_t cc;
    uint32_t csts;
    uint32_t intm; /* shared INTMS/INTMC mask (qemu-t8030 bar.intms==intmc) */
    /* NVMe-style irq_status: bit0 set while admin CQ has undrained CQE.
     * vf_ans_irq_check = (irq_status & ~intm & ADMIN_CQ) != 0.
     * Bound PCI mirrors that into INTx or MSI pending (optional bound MSI
     * message write; message write ≠ AIC). */
    uint32_t irq_status;
    uint32_t aqa;
    uint32_t asq_lo;
    uint32_t asq_hi;
    uint32_t acq_lo;
    uint32_t acq_hi;
    /* Doorbell slots [0]=admin SQ, [1]=admin CQ, [2]=I/O qid1 SQ, ... */
    uint32_t doorbell[VF_ANS_DB_COUNT];
    /* Sparse backing for unmodeled vendor offsets (honest store/read). */
    vf_ans_vendor_slot vendor_sparse[VF_ANS_VENDOR_SPARSE_CAP];
    uint32_t vendor_sparse_used;
    int is_apple_ans; /* default 1; selects Identify CNS=CTRL SQES/MDTS */
    /* Optional PCI link: bound Identify DMA fails closed without BusMaster. */
    vf_ans_pci_v1 *pci;
    /* Optional mbox link: bound Identify DMA fails closed without STARTED. */
    vf_ans_mbox_v1 *mbox;
    /* Optional ASCWrap link: bound Identify DMA fails closed without READY
     * (READY mirrors bound mbox STARTED via vf_ascwrap_bind_mbox). */
    vf_ascwrap_v1 *ascwrap;
    /* Optional autoboot link: bound Identify DMA fails closed without ARMED
     * (ARMED mirrors bound ASCWrap READY via vf_ans_autoboot_bind_ascwrap). */
    vf_ans_autoboot_v1 *autoboot;
    /* Host/test armed admin Identify (simplified SQE + CQ buffer). Admin
     * SQTDBL advance with opcode=Identify + CNS=CTRL|NS drives DMA fill into
     * admin_id_buf; on success writes a 16-byte CQE into admin_id_cq_buf and
     * advances admin_cq_tail (CQHDBL is host drain) and sets irq_status bit0.
     * Cleared on one-shot attempt, CC.EN clear, or disarm. No MSI/AIC. */
    int admin_id_armed;
    uint8_t admin_id_opcode;
    uint8_t admin_id_cns;
    uint8_t *admin_id_buf;
    uint32_t admin_id_len;
    uint8_t *admin_id_cq_buf;
    uint32_t admin_id_cq_len;
    /* Optional guest-visible Admin SQ page (host/test stand-in for ASQ GPA).
     * When bound, doorbell Identify fetches SQE from ASQ[old Tail] and
     * fail-closes on SGL / zero PRP1 / shape mismatch. NULL = unbound
     * (legacy arm-only path). */
    uint8_t *admin_asq_mem;
    uint32_t admin_asq_len;
    /* Optional guest-visible Admin CQ page (host/test stand-in for ACQ GPA).
     * When bound, Identify CQE is also written to ACQ[admin_cq_tail] and
     * fail-closes on short page / ACQ base both zero. NULL = unbound
     * (cq_buf-only legacy path). No real GPA walk / MSI / AIC. */
    uint8_t *admin_acq_mem;
    uint32_t admin_acq_len;
    /* Optional Identify data page (host/test stand-in for PRP1 GPA).
     * When bound with ASQ: page-aligned PRP1 + PRP2==0 copies full payload
     * here; with PRP2 also bound, unaligned PRP1 splits across PRP1+PRP2.
     * NULL = unbound (armed id_buf only; PRP1 non-zero honesty). */
    uint8_t *admin_id_prp1_mem;
    uint32_t admin_id_prp1_len;
    /* Optional Identify second data page (host/test stand-in for PRP2 GPA
     * as a data page — never a PRP list). Required for unaligned PRP1 when
     * PRP1 is bound. NULL = unbound (unaligned / PRP2!=0 fail-closed). */
    uint8_t *admin_id_prp2_mem;
    uint32_t admin_id_prp2_len;
    /* Controller CQ Tail (low-16). Host CQHDBL drains; pending = tail - CQH. */
    uint16_t admin_cq_tail;
    uint8_t admin_cq_phase; /* next CQE Phase Tag (NVMe: starts at 1) */
} vf_ans_v1;

/* Host/test Identify CNS=CTRL metadata (subset of id_ctrl). Not a DMA buffer. */
typedef struct {
    uint8_t cns;       /* always VF_ANS_ID_CNS_CTRL when filled */
    uint8_t sqes;      /* is-apple-ans branch */
    uint8_t cqes;      /* fixed 0x44 */
    uint8_t mdts;      /* 8 Apple / 7 std */
    uint8_t cntrltype; /* 0x1 */
    uint32_t nn;       /* NVME_MAX_NAMESPACES */
    uint32_t ver;      /* NVMe 1.4 == VS */
} vf_ans_id_ctrl_meta;

/* Host/test Identify CNS=NS metadata (subset of id_ns). Zero-capacity stub
 * NSID=1 only — no block backend / invented LBA formats. Not a DMA buffer. */
typedef struct {
    uint8_t cns;       /* always VF_ANS_ID_CNS_NS when filled */
    uint32_t nsid;     /* always VF_ANS_ID_NS_STUB_NSID */
    uint64_t nsze;     /* 0 — no capacity claimed */
    uint64_t ncap;     /* 0 */
    uint64_t nuse;     /* 0 */
    uint8_t nlbaf;     /* 0 — no LBA formats claimed */
    uint8_t flbas;     /* 0 */
} vf_ans_id_ns_meta;

int vf_ans_init(vf_ans_v1 *);
/* Exactly aligned little-endian u32 within [0, VF_ANS_VENDOR_SIZE).
 * Unmodeled NVMe alias gaps: RO-zero (accept write; read stays 0).
 * Unmodeled vendor offsets (≥0x1200): sparse store/read; table-full fails.
 * BOOT_STATUS / MAX_PEND / BASE_CMD_ID and NVMe CAP/VS reads are sticky
 * (write-immune). INTMS write ORs into intm; INTMC write clears bits; both
 * read the same mask. CC.EN sets/clears CSTS.RDY; AQA/ASQ/ACQ are stored R/W.
 * SQTDBL/CQHDBL (admin + I/O, 0x1000..0x11FF) store low-16; admin SQTDBL
 * advance may one-shot Identify CNS=CTRL|NS DMA fill + admin CQE when armed
 * (optional bound ASQ fetch + PRP/SGL honesty; optional PRP1/PRP2 GPA→buffer
 * translate; optional bound ACQ CQE write); CC.EN clear resets all doorbells
 * + intm + irq_status + armed Identify + CQ Tail/phase (ASQ/ACQ/PRP1/PRP2
 * binds persist). irq_check may sync bound PCI INTx pending — no MSI
 * message delivery / AIC / DT IRQ. */
int vf_ans_read(vf_ans_v1 *, uint32_t offset, unsigned width, uint32_t *value);
int vf_ans_write(vf_ans_v1 *, uint32_t offset, unsigned width, uint32_t value);

/* Optional bind to ans_pci_v1. NULL pci unbinds (deasserts old INTx).
 * Bind syncs current irq_check into PCI INTx. Fail-closed on NULL ans. */
int vf_ans_bind_pci(vf_ans_v1 *, vf_ans_pci_v1 *pci);
/* Optional bind to ans_mbox_v1. NULL mbox unbinds. When bound, Identify DMA
 * fail-closes unless vf_ans_mbox_started. Fail-closed on NULL ans. */
int vf_ans_bind_mbox(vf_ans_v1 *, vf_ans_mbox_v1 *mbox);
/* Optional bind to ascwrap_v1. NULL ascwrap unbinds. When bound, Identify DMA
 * fail-closes unless vf_ascwrap_ready (composite ASCWrap↔mbox STARTED→READY).
 * Fail-closed on NULL ans. */
int vf_ans_bind_ascwrap(vf_ans_v1 *, vf_ascwrap_v1 *ascwrap);
/* Optional bind to ans_autoboot_v1. NULL autoboot unbinds. When bound,
 * Identify DMA fail-closes unless vf_ans_autoboot_armed (ARMED←ASCWrap READY
 * ←mbox STARTED). Fail-closed on NULL ans. */
int vf_ans_bind_autoboot(vf_ans_v1 *, vf_ans_autoboot_v1 *autoboot);

/* is-apple-ans Identify CNS=CTRL metadata (host/test surface).
 * Does not submit admin commands or perform PRP/DMA. */
int vf_ans_set_is_apple_ans(vf_ans_v1 *, int enabled);
int vf_ans_get_is_apple_ans(const vf_ans_v1 *);
int vf_ans_identify_sqes(const vf_ans_v1 *, uint8_t *sqes);
int vf_ans_identify_cqes(const vf_ans_v1 *, uint8_t *cqes);
/* 0 for CNS=CTRL or CNS=NS (metadata + DMA fill available); else -1. */
int vf_ans_identify_cns_supported(uint8_t cns);
int vf_ans_identify_cns_ctrl(const vf_ans_v1 *, vf_ans_id_ctrl_meta *out);
/* Host/test Identify CNS=NS metadata (zero-capacity stub NSID=1). */
int vf_ans_identify_cns_ns(const vf_ans_v1 *, vf_ans_id_ns_meta *out);
/*
 * Host/test Identify CNS=CTRL DMA fill into caller buffer (guest/host).
 * Zeroes VF_ANS_ID_CTRL_SIZE bytes, then writes known metadata fields at
 * NVMe id_ctrl offsets. Requires CC.EN; when PCI is bound, also requires
 * BusMaster; when mbox is bound, also requires STARTED; when ASCWrap is bound,
 * also requires READY; when autoboot is bound, also requires ARMED.
 * Fail-closed on wrong CNS, short buffer, or missing gates. Never MSI, SQ
 * fetch, PRP walk, or CQ completion (doorbell path may CQ).
 */
int vf_ans_identify_cns_ctrl_dma(vf_ans_v1 *, uint8_t cns, void *buf,
                                 uint32_t len);
/*
 * Host/test Identify CNS=NS DMA fill into caller buffer (guest/host).
 * Zeroes VF_ANS_ID_NS_SIZE bytes and writes zero-capacity stub metadata at
 * NVMe id_ns offsets (nsze/ncap/nuse/nlbaf/flbas all 0 — no invented disk /
 * LBA formats). Requires CC.EN; when PCI is bound, also requires BusMaster;
 * when mbox is bound, also requires STARTED; when ASCWrap is bound, also
 * requires READY; when autoboot is bound, also requires ARMED. Fail-closed on
 * wrong CNS, short buffer, or missing gates. No MSI / SQ / PRP / CQ
 * (doorbell path may CQ via shared Identify arm).
 */
int vf_ans_identify_cns_ns_dma(vf_ans_v1 *, uint8_t cns, void *buf,
                               uint32_t len);
/*
 * Arm one host/test admin Identify submit (simplified SQE + CQ buffer).
 * Requires opcode=VF_ANS_OPC_IDENTIFY, cns=CTRL|NS, id buf >=4096,
 * cq buf >=16. When admin SQTDBL (0x1000) advances (new low-16 != prior),
 * attempts CNS-matched DMA fill into the armed id buffer (one-shot; clears
 * arm). If vf_ans_bind_admin_asq is set, also fetches SQE from ASQ[old Tail]
 * and fail-closes on PSDT!=PRP, PRP1==0, opcode/CNS mismatch, short ASQ page,
 * ASQ base registers both zero, or (CNS=NS) SQE NSID != stub NSID=1. If
 * vf_ans_bind_identify_prp1 is also set, page-aligned PRP1 requires PRP2==0
 * (full payload → PRP1 stand-in); with vf_ans_bind_identify_prp2 also set,
 * unaligned PRP1 + page-aligned non-zero PRP2 splits across PRP1 remainder +
 * PRP2 second data page (PRP list never walked; unaligned PRP2 fail-closed).
 * If vf_ans_bind_admin_acq is set, requires a writable ACQ slot at
 * ACQ[admin_cq_tail] (fail-closed on short page / ACQ base both zero) before
 * DMA; on success also copies the CQE into that guest ACQ slot.
 * On DMA success: writes a simplified 16-byte admin CQE (status=0, Phase Tag,
 * SQHD=current admin SQTDBL, SQID/CID=0) into cq_buf (+ ACQ when bound),
 * advances admin_cq_tail, and sets irq_status bit0; CQHDBL remains the host
 * drain pointer (pending = tail - CQH). No MSI / AIC delivery / PRP list walk
 * / real GPA walk of ASQ/ACQ bases / block backend.
 * Fail-closed arm on bad shape / short buf / NULL. Doorbell store always
 * succeeds; DMA fill itself fail-closes without CC.EN / bound BusMaster /
 * bound mbox STARTED / bound ASCWrap READY (no CQE / no tail advance / no
 * irq_status on DMA failure). Never MSI, PRP list walk, or real GPA translate
 * of ACQ/ASQ bases.
 */
int vf_ans_arm_admin_identify(vf_ans_v1 *, uint8_t opcode, uint8_t cns,
                              void *buf, uint32_t len, void *cq_buf,
                              uint32_t cq_len);
/* 1 if armed, 0 if not, -1 on NULL. */
int vf_ans_admin_identify_armed(const vf_ans_v1 *);
/* Clear armed Identify without doorbell / DMA / CQ. Fail-closed on NULL. */
int vf_ans_disarm_admin_identify(vf_ans_v1 *);
/*
 * Bind guest-visible Admin SQ page (host/test stand-in for ASQ GPA).
 * mem=NULL unbinds. len must be >= VF_ANS_SQE_SIZE when binding.
 * When bound, Identify doorbell path fetches SQE from ASQ[old Tail].
 * Fail-closed on NULL ans or short len. Does not require CC.EN.
 */
int vf_ans_bind_admin_asq(vf_ans_v1 *, void *mem, uint32_t len);
/* 1 if ASQ page bound, 0 if not, -1 on NULL. */
int vf_ans_admin_asq_bound(const vf_ans_v1 *);
/*
 * Bind guest-visible Admin CQ page (host/test stand-in for ACQ GPA).
 * mem=NULL unbinds. len must be >= VF_ANS_CQE_SIZE when binding.
 * When bound, Identify doorbell path writes CQE to ACQ[admin_cq_tail].
 * Fail-closed on NULL ans or short len. Does not require CC.EN.
 */
int vf_ans_bind_admin_acq(vf_ans_v1 *, void *mem, uint32_t len);
/* 1 if ACQ page bound, 0 if not, -1 on NULL. */
int vf_ans_admin_acq_bound(const vf_ans_v1 *);
/*
 * Bind Identify data page (host/test stand-in for PRP1 GPA).
 * mem=NULL unbinds. len must be >= VF_ANS_ID_CTRL_SIZE when binding.
 * When bound with ASQ: page-aligned PRP1 + PRP2==0 copies full payload here;
 * with PRP2 also bound, unaligned PRP1 splits across PRP1+PRP2.
 * Fail-closed on NULL ans or short len. Does not require CC.EN.
 * No PRP list walk / MSI / AIC.
 */
int vf_ans_bind_identify_prp1(vf_ans_v1 *, void *mem, uint32_t len);
/* 1 if Identify PRP1 page bound, 0 if not, -1 on NULL. */
int vf_ans_identify_prp1_bound(const vf_ans_v1 *);
/*
 * Bind Identify second data page (host/test stand-in for PRP2 GPA as data).
 * mem=NULL unbinds. len must be >= VF_ANS_ID_CTRL_SIZE when binding.
 * Required for unaligned PRP1 when PRP1 is bound; PRP2 is never walked as a
 * PRP list (unaligned PRP2 fail-closed). Fail-closed on NULL ans or short len.
 */
int vf_ans_bind_identify_prp2(vf_ans_v1 *, void *mem, uint32_t len);
/* 1 if Identify PRP2 page bound, 0 if not, -1 on NULL. */
int vf_ans_identify_prp2_bound(const vf_ans_v1 *);
/* Controller admin CQ Tail (low-16). -1 on NULL. */
int vf_ans_admin_cq_tail(const vf_ans_v1 *);
/* Pending completions: (admin_cq_tail - CQHDBL) & 0xffff. -1 on NULL. */
int vf_ans_admin_cq_pending(const vf_ans_v1 *);
/*
 * Host/test NVMe-style irq_check (pin/legacy vector 0 only).
 * Returns 1 when (irq_status & ~intm & VF_ANS_IRQ_ADMIN_CQ) != 0, else 0.
 * INTMS bit0 fail-closed masks without clearing irq_status; INTMC unmask
 * re-exposes. Drain via CQHDBL or CC.EN clear clears irq_status.
 * When PCI is bound, the same pending mirrors into vf_ans_pci_irq_pending
 * (INTx), vf_ans_pci_msi_pending when MSI Enable is set, or
 * vf_ans_pci_msix_pending when MSI-X Enable is set (MSI-X > MSI > INTx) —
 * optional bound MSI message write via vf_ans_pci_bind_msi_message
 * (message write ≠ AIC alone) and optional bound AIC STORAGE-line mirror
 * via vf_ans_pci_bind_aic (≠ window 0x4 / Apple DT IRQ).
 */
int vf_ans_irq_check(const vf_ans_v1 *);

#endif
