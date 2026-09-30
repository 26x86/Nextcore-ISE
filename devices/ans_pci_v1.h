/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_ANS_PCI_V1_H
#define VENFIRE_ANS_PCI_V1_H
#include <stdint.h>

/*
 * ANS composite PCIe host present/bootstrap stub (region index 3).
 * Behaviour contract: docs/research/INFERNO_DEVICE_MODELS.md (ANS), ANS.md,
 * ANS_PCI.md. qemu-t8030 apple_ans creates empty ans_pci_mmio / ans_pci_ioport
 * containers and MMCFG for a PCIExpressHost root; this stub announces that
 * host presence and stores/reads PCI COMMAND Memory|BusMaster bits only.
 * Mailbox start/wakeup may latch Memory|BusMaster via
 * vf_ans_pci_enable_memory_bus_master (see ans_mbox_v1). Identify CQ
 * irq_check may latch INTx pending via vf_ans_pci_set_irq (pin level), MSI
 * pending when MSI Enable is set, or MSI-X pending when MSI-X Enable is set
 * (MSI-X > MSI > INTx). Optional vf_ans_pci_bind_msi_gpa binds a host buffer
 * as Message Address GPA stand-in (prefer over callback): on MSI pending
 * assert, translate msi_addr→offset and store LE16 msi_data (fail-closed OOB /
 * unbound). Optional vf_ans_pci_bind_msi_message routes a host/test DMA
 * callback when GPA is unbound. Optional vf_ans_pci_bind_aic mirrors
 * INTx|MSI|MSI-X pending into graph STORAGE AIC line 1 (host/test / bridge
 * callback; fail-closed unbound). Message write ≠ AIC by itself; AIC bind ≠
 * graph STORAGE window 0x4 ownership, Apple DT IRQ, or guest acceptance.
 * MSI-X stub is Enable + Table BIR prepare + pending latch only — no PBA /
 * table MMIO, ECAM enumeration, DT IRQ numbers, or command DMA.
 */

#define VF_ANS_PCI_TIER_HOST             3u

/* Stage status (honest bootstrap only; ACTIVE unreachable without real PCIe). */
#define VF_ANS_PCI_STATUS_ABSENT         0u
#define VF_ANS_PCI_STATUS_BOOTSTRAP      1u
#define VF_ANS_PCI_STATUS_ACTIVE         2u /* unreachable at this stub tier */

#define VF_ANS_PCI_FLAG_PRESENT          (1u << 0)
#define VF_ANS_PCI_FLAG_MMIO_CONTAINER   (1u << 1)
#define VF_ANS_PCI_FLAG_IOPORT_CONTAINER (1u << 2)
/* Capability present honesty only — not a real PCI cap list. */
#define VF_ANS_PCI_FLAG_MSI_CAPABLE      (1u << 3)
#define VF_ANS_PCI_FLAG_MSIX_CAPABLE     (1u << 4)

/* Public PCI COMMAND bits (config space; behaviour-only, not an ECAM claim). */
#define VF_ANS_PCI_COMMAND_MEMORY        (1u << 1) /* Memory Space Enable */
#define VF_ANS_PCI_COMMAND_MASTER        (1u << 2) /* Bus Master Enable */
#define VF_ANS_PCI_COMMAND_ENABLE_MASK   \
    (VF_ANS_PCI_COMMAND_MEMORY | VF_ANS_PCI_COMMAND_MASTER)

/* Graph-local MSI Message Control enable bit (not full MSI capability layout). */
#define VF_ANS_PCI_MSI_ENABLE            (1u << 0)
/* Graph-local MSI-X Message Control enable bit (not full MSI-X cap layout). */
#define VF_ANS_PCI_MSIX_ENABLE           (1u << 0)
/* PCI MSI-X Table BIR field (bits 2:0); other Table Offset bits deferred. */
#define VF_ANS_PCI_MSIX_BIR_MASK         0x7u

/* Pattern-matched M1GuestBus STORAGE_SOURCE / VF_M1_STORAGE_IRQ_LINE.
 * Bound AIC sync uses this line only; not Apple DT storage IRQ numbers and
 * not ownership of graph MMIO window 0x4 (storage_v1 remains separate). */
#define VF_ANS_PCI_AIC_STORAGE_IRQ_LINE  1u

/* qemu-t8030 apple_ans: memory_region_init(..., "ans_pci_ioport", 64 * 1024) */
#define VF_ANS_PCI_IOPORT_SIZE           (64u * 1024u)
/* qemu pcie_host_mmcfg_init(..., PCIE_MMCFG_SIZE_MAX) == 1<<28 */
#define VF_ANS_PCI_MMCFG_SIZE            (1u << 28)

/* Graph-local MMIO (not Apple APCIe / ECAM physical map). */
#define VF_ANS_PCI_MMIO_STATUS           0x000u /* 32 RO */
#define VF_ANS_PCI_MMIO_FLAGS            0x004u /* 32 RO */
#define VF_ANS_PCI_MMIO_TIER             0x008u /* 32 RO */
#define VF_ANS_PCI_MMIO_IOPORT_SIZE      0x010u /* 32 RO */
#define VF_ANS_PCI_MMIO_MMCFG_SIZE       0x014u /* 32 RO */
#define VF_ANS_PCI_MMIO_CONFIG           0x018u /* 64 R/W: COMMAND Memory|Master */
#define VF_ANS_PCI_MMIO_MSI_CTRL         0x020u /* 32 R/W: MSI Enable bit0 */
#define VF_ANS_PCI_MMIO_MSI_ADDR         0x028u /* 64 R/W: message address prepare */
#define VF_ANS_PCI_MMIO_MSI_DATA         0x030u /* 32 R/W: message data prepare (lo16) */
#define VF_ANS_PCI_MMIO_MSIX_CTRL        0x038u /* 32 R/W: MSI-X Enable bit0 */
#define VF_ANS_PCI_MMIO_MSIX_TABLE_BIR   0x03cu /* 32 R/W: Table BIR prepare (lo3) */
#define VF_ANS_PCI_MMIO_SIZE             0x040u

/*
 * Host/test MSI message sink. Invoked with prepared Message Address/Data when
 * MSI pending asserts and GPA buffer is unbound. Return 0 on success,
 * non-zero to fail the write (pending still latches). Opaque may be a
 * test-visible host buffer. Prefer vf_ans_pci_bind_msi_gpa for GPA→buffer
 * translate. Never implies AIC / DT IRQ / MSI-X table walk / live guest RAM.
 */
typedef int (*vf_ans_pci_msi_message_fn)(void *opaque, uint64_t addr,
                                         uint16_t data);

/* MSI message write stores LE16 Message Data into a bound GPA window. */
#define VF_ANS_PCI_MSI_MSG_BYTES         2u

/*
 * Host/test / bridge AIC line sink. Invoked with
 * VF_ANS_PCI_AIC_STORAGE_IRQ_LINE and INTx|MSI|MSI-X delivery pending level
 * when pending changes. Return 0 on success, non-zero to fail the sync
 * (pending latches still hold). Opaque may be vf_aic_v1 * or a bridge
 * context. Does not invent MMIO or claim Apple DT IRQ / guest acceptance.
 */
typedef int (*vf_ans_pci_aic_fn)(void *opaque, unsigned irq, int high);

typedef struct {
    uint32_t status;
    uint32_t flags;
    /* Latched COMMAND Memory|BusMaster only; other bits never stick. */
    uint16_t command;
    /* Last accepted WO (test observability only; raw, not masked). */
    uint64_t last_config_write;
    int config_write_seen;
    /* Pin/INTx pending level (qemu apple_ans_set_irq → s->irq). Host/test
     * honesty only — suppressed while MSI or MSI-X Enable is set. */
    int intx_pending;
    /* MSI Enable (Message Control bit0 honesty). When set (and MSI-X off),
     * set_irq routes to msi_pending and clears INTx. */
    int msi_enabled;
    /* Prepared MSI Message Address / Data (store; written out only via bind). */
    uint64_t msi_addr;
    uint16_t msi_data;
    /* MSI pending latch when enabled + irq asserted (and MSI-X off). */
    int msi_pending;
    /* MSI-X Enable (Message Control bit0 honesty). Takes precedence over MSI. */
    int msix_enabled;
    /* Prepared MSI-X Table BIR (bits 2:0 only; no Table Offset / PBA). */
    uint8_t msix_table_bir;
    /* MSI-X pending latch when enabled + irq asserted. */
    int msix_pending;
    /* Optional MSI Message Address GPA→host buffer (NULL = unbound).
     * When set, preferred over msi_message_fn for message write. */
    uint8_t *msi_gpa_mem;
    uint32_t msi_gpa_len;
    uint64_t msi_gpa_base;
    /* Optional MSI message DMA callback (NULL = unbound / fail-closed).
     * Used only when GPA buffer is unbound. */
    vf_ans_pci_msi_message_fn msi_message_fn;
    void *msi_message_opaque;
    /* 1 after at least one successful bound message write (not AIC alone). */
    int msi_message_written;
    /* Last successful message write address/data (observability). */
    uint64_t last_msi_message_addr;
    uint16_t last_msi_message_data;
    /* Optional AIC STORAGE-line sync (NULL = unbound / fail-closed). */
    vf_ans_pci_aic_fn aic_fn;
    void *aic_opaque;
    /* 1 after at least one successful bound AIC sync. */
    int aic_sync_seen;
    /* Last successfully delivered AIC level (0/1); observability only. */
    int aic_line_high;
} vf_ans_pci_v1;

int vf_ans_pci_init(vf_ans_pci_v1 *s);
uint32_t vf_ans_pci_status(const vf_ans_pci_v1 *s);
uint32_t vf_ans_pci_flags(const vf_ans_pci_v1 *s);
/* Latched COMMAND Memory|BusMaster mask; 0 on NULL. Never implies DMA/MSI. */
uint16_t vf_ans_pci_command(const vf_ans_pci_v1 *s);
/* 1 when COMMAND BusMaster bit is latched; never implies DMA/MSI/ACTIVE. */
int vf_ans_pci_bus_master_enabled(const vf_ans_pci_v1 *s);
/*
 * OR Memory|BusMaster into COMMAND (qemu apple_ans_start). Host/test /
 * ans_mbox start path only. Never enables DMA/MSI or promotes ACTIVE.
 * Fail-closed on NULL.
 */
int vf_ans_pci_enable_memory_bus_master(vf_ans_pci_v1 *s);
/* 1 after at least one successful config WO; never implies MSI/ECAM/DMA. */
int vf_ans_pci_config_write_seen(const vf_ans_pci_v1 *s);
uint64_t vf_ans_pci_last_config_write(const vf_ans_pci_v1 *s);
/*
 * Latch irq pending. Priority: MSI-X Enable → msix_pending (clears INTx/MSI);
 * else MSI Enable → msi_pending + optional message write; else INTx.
 * Fail-closed NULL. MSI-X path does not invent table/PBA MMIO writes.
 */
int vf_ans_pci_set_irq(vf_ans_pci_v1 *s, int level);
/* 1 when intx_pending latched; never implies MSI delivery alone. */
int vf_ans_pci_irq_pending(const vf_ans_pci_v1 *s);
/* 1 when MSI Enable latched; never implies message delivery alone. */
int vf_ans_pci_msi_enabled(const vf_ans_pci_v1 *s);
/* 1 when msi_pending latched; message write / AIC may be separate binds. */
int vf_ans_pci_msi_pending(const vf_ans_pci_v1 *s);
/* 1 when MSI-X Enable latched; never implies table walk / PBA alone. */
int vf_ans_pci_msix_enabled(const vf_ans_pci_v1 *s);
/* 1 when msix_pending latched; no table/PBA write claimed. */
int vf_ans_pci_msix_pending(const vf_ans_pci_v1 *s);
/* Prepared MSI-X Table BIR (0..7); 0 on NULL. */
uint8_t vf_ans_pci_msix_table_bir(const vf_ans_pci_v1 *s);
/* 1 when INTx or MSI or MSI-X pending (delivery level mirrored to bound AIC). */
int vf_ans_pci_delivery_pending(const vf_ans_pci_v1 *s);
/* Prepared MSI address / data (0 on NULL). */
uint64_t vf_ans_pci_msi_addr(const vf_ans_pci_v1 *s);
uint16_t vf_ans_pci_msi_data(const vf_ans_pci_v1 *s);
/*
 * Bind host stand-in for MSI Message Address GPA window.
 * On MSI pending assert: if msi_addr in [base_gpa, base_gpa+len) with room
 * for VF_ANS_PCI_MSI_MSG_BYTES, store LE16 msi_data at offset
 * (msi_addr - base_gpa). NULL mem unbinds; len < 2 with non-NULL mem
 * fail-closed (-1). Preferred over vf_ans_pci_bind_msi_message when both
 * set. Never ACTIVE / MSI-X table / live guest RAM / AIC alone.
 */
int vf_ans_pci_bind_msi_gpa(vf_ans_pci_v1 *s, void *mem, uint32_t len,
                            uint64_t base_gpa);
/* 1 when GPA buffer bound; 0 unbound; -1 on NULL. */
int vf_ans_pci_msi_gpa_bound(const vf_ans_pci_v1 *s);
/*
 * Bind host/test MSI message sink. NULL fn unbinds (fail-closed writes).
 * Used only when GPA buffer is unbound. Opaque may point at a test-visible
 * host buffer. Never ACTIVE.
 */
int vf_ans_pci_bind_msi_message(vf_ans_pci_v1 *s, vf_ans_pci_msi_message_fn fn,
                                void *opaque);
/* 1 after a successful bound message write; never implies AIC alone. */
int vf_ans_pci_msi_message_written(const vf_ans_pci_v1 *s);
uint64_t vf_ans_pci_last_msi_message_addr(const vf_ans_pci_v1 *s);
uint16_t vf_ans_pci_last_msi_message_data(const vf_ans_pci_v1 *s);
/*
 * Bind host/test / bridge AIC STORAGE-line sink. NULL fn unbinds
 * (fail-closed sync). Opaque may be vf_aic_v1 *. Mirrors
 * delivery_pending onto VF_ANS_PCI_AIC_STORAGE_IRQ_LINE. Does not claim
 * graph STORAGE window 0x4, Apple DT IRQ, or guest acceptance.
 */
int vf_ans_pci_bind_aic(vf_ans_pci_v1 *s, vf_ans_pci_aic_fn fn, void *opaque);
/* 1 after a successful bound AIC sync; never implies guest acceptance. */
int vf_ans_pci_aic_sync_seen(const vf_ans_pci_v1 *s);
/* Last successfully delivered AIC level (0/1); 0 on NULL / never synced. */
int vf_ans_pci_aic_line_high(const vf_ans_pci_v1 *s);

/* Aligned access within [0, VF_ANS_PCI_MMIO_SIZE).
 * Config @ 0x018: width 64 only; store/read COMMAND Memory|BusMaster mask.
 * MSI @ 0x020/0x028/0x030: enable + address/data prepare.
 * MSI-X @ 0x038/0x03c: enable + Table BIR prepare (no PBA/table MMIO).
 * Status/flags/tier/sizes: width 32 RO. Fail-closed otherwise.
 * Message write requires vf_ans_pci_bind_msi_gpa (preferred) or
 * vf_ans_pci_bind_msi_message; AIC requires vf_ans_pci_bind_aic. */
int vf_ans_pci_read(vf_ans_pci_v1 *s, uint32_t offset, unsigned width,
                    uint64_t *value);
int vf_ans_pci_write(vf_ans_pci_v1 *s, uint32_t offset, unsigned width,
                     uint64_t value);

#endif
