/* 26x86 first-party ANS PCIe host stub for composite region index 3.
 * See ANS_PCI.md. Behaviour: present/bootstrap announce for empty
 * ans_pci_mmio / ans_pci_ioport containers + MMCFG size honesty from
 * qemu-t8030 apple_ans. Graph-local config slot stores/reads PCI COMMAND
 * Memory|BusMaster bits only. Mailbox start may OR Memory|BusMaster via
 * vf_ans_pci_enable_memory_bus_master. Identify CQ irq_check may latch
 * INTx pending via vf_ans_pci_set_irq, MSI pending when MSI Enable is set,
 * or MSI-X pending when MSI-X Enable is set (MSI-X > MSI > INTx). Optional
 * vf_ans_pci_bind_msi_gpa translates Message Address as GPA into a host
 * buffer (LE16 data; fail-closed OOB). Optional vf_ans_pci_bind_msi_message
 * delivers prepared Message Address/Data through a host/test DMA callback
 * when GPA is unbound (fail-closed if both unbound). Optional
 * vf_ans_pci_bind_aic mirrors INTx|MSI|MSI-X delivery pending into STORAGE
 * AIC line 1 (fail-closed unbound; ≠ graph window 0x4 ownership / Apple DT
 * IRQ / guest acceptance). MSI-X stub is Enable + Table BIR + pending only
 * (no PBA/table MMIO). Message write ≠ AIC by itself. No ECAM enumeration,
 * DT IRQ numbers, or command DMA. Status never reaches ACTIVE.
 */
#include "ans_pci_v1.h"

#ifdef VF_EFI_BUILD
static void *ans_pci_memset(void *dst, int c, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
#define memset(d, c, n) ans_pci_memset((d), (c), (unsigned)(n))
#else
#include <string.h>
#endif

static int ans_pci_delivery_pending_level(const vf_ans_pci_v1 *s) {
    return (s->intx_pending || s->msi_pending || s->msix_pending) ? 1 : 0;
}

/* Mirror delivery pending onto bound AIC STORAGE line. Fail-closed unbound /
 * callback error: pending latches still hold. */
static int ans_pci_sync_aic(vf_ans_pci_v1 *s) {
    int high = ans_pci_delivery_pending_level(s);
    if (!s->aic_fn) {
        return -1;
    }
    if (s->aic_fn(s->aic_opaque, VF_ANS_PCI_AIC_STORAGE_IRQ_LINE, high)
        != 0) {
        return -1;
    }
    s->aic_sync_seen = 1;
    s->aic_line_high = high;
    return 0;
}

/* Attempt bound MSI message write. Prefer GPA→host buffer translate when
 * msi_gpa_mem is bound; else DMA callback. Fail-closed unbound / OOB /
 * callback error: pending may still latch; never AIC by itself. */
static int ans_pci_try_msi_message_write(vf_ans_pci_v1 *s) {
    if (s->msi_gpa_mem) {
        uint64_t off;
        if (s->msi_addr < s->msi_gpa_base) {
            return -1;
        }
        off = s->msi_addr - s->msi_gpa_base;
        if (off > (uint64_t)s->msi_gpa_len) {
            return -1;
        }
        if ((uint64_t)s->msi_gpa_len - off < VF_ANS_PCI_MSI_MSG_BYTES) {
            return -1;
        }
        s->msi_gpa_mem[off] = (uint8_t)(s->msi_data & 0xffu);
        s->msi_gpa_mem[off + 1u] = (uint8_t)((s->msi_data >> 8) & 0xffu);
        s->msi_message_written = 1;
        s->last_msi_message_addr = s->msi_addr;
        s->last_msi_message_data = s->msi_data;
        return 0;
    }
    if (!s->msi_message_fn) {
        return -1;
    }
    if (s->msi_message_fn(s->msi_message_opaque, s->msi_addr, s->msi_data)
        != 0) {
        return -1;
    }
    s->msi_message_written = 1;
    s->last_msi_message_addr = s->msi_addr;
    s->last_msi_message_data = s->msi_data;
    return 0;
}

/* Re-route existing pending between INTx and MSI when Enable toggles.
 * MSI-X Enable owns delivery when set — MSI Enable is prepare-only then.
 * MSI assert path may attempt bound message write; then AIC sync. */
static void ans_pci_apply_msi_enable(vf_ans_pci_v1 *s, int enable) {
    int was = s->msi_enabled ? 1 : 0;
    int now = enable ? 1 : 0;
    s->msi_enabled = now;
    if (s->msix_enabled) {
        /* MSI-X owns delivery; do not steal msix_pending into MSI. */
        (void)ans_pci_sync_aic(s);
        return;
    }
    if (!was && now) {
        if (s->intx_pending) {
            s->msi_pending = 1;
            s->intx_pending = 0;
            (void)ans_pci_try_msi_message_write(s);
        }
    } else if (was && !now) {
        if (s->msi_pending) {
            s->intx_pending = 1;
            s->msi_pending = 0;
        }
    }
    (void)ans_pci_sync_aic(s);
}

/* Re-route existing pending into/out of MSI-X when Enable toggles.
 * No table/PBA MMIO write. On disable, prefer MSI if still enabled. */
static void ans_pci_apply_msix_enable(vf_ans_pci_v1 *s, int enable) {
    int was = s->msix_enabled ? 1 : 0;
    int now = enable ? 1 : 0;
    s->msix_enabled = now;
    if (!was && now) {
        if (s->intx_pending || s->msi_pending) {
            s->msix_pending = 1;
            s->intx_pending = 0;
            s->msi_pending = 0;
        }
    } else if (was && !now) {
        if (s->msix_pending) {
            s->msix_pending = 0;
            if (s->msi_enabled) {
                s->msi_pending = 1;
                s->intx_pending = 0;
                (void)ans_pci_try_msi_message_write(s);
            } else {
                s->intx_pending = 1;
            }
        }
    }
    (void)ans_pci_sync_aic(s);
}

int vf_ans_pci_init(vf_ans_pci_v1 *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    /* Honest announce: PCIe host containers present in bootstrap; no ECAM.
     * MSI/MSI-X_CAPABLE mark stub capability surfaces only (no cap-list). */
    s->status = VF_ANS_PCI_STATUS_BOOTSTRAP;
    s->flags = VF_ANS_PCI_FLAG_PRESENT |
               VF_ANS_PCI_FLAG_MMIO_CONTAINER |
               VF_ANS_PCI_FLAG_IOPORT_CONTAINER |
               VF_ANS_PCI_FLAG_MSI_CAPABLE |
               VF_ANS_PCI_FLAG_MSIX_CAPABLE;
    return 0;
}

uint32_t vf_ans_pci_status(const vf_ans_pci_v1 *s) {
    return s ? s->status : VF_ANS_PCI_STATUS_ABSENT;
}

uint32_t vf_ans_pci_flags(const vf_ans_pci_v1 *s) {
    return s ? s->flags : 0u;
}

uint16_t vf_ans_pci_command(const vf_ans_pci_v1 *s) {
    return s ? s->command : 0u;
}

int vf_ans_pci_bus_master_enabled(const vf_ans_pci_v1 *s) {
    return s && (s->command & VF_ANS_PCI_COMMAND_MASTER) ? 1 : 0;
}

int vf_ans_pci_enable_memory_bus_master(vf_ans_pci_v1 *s) {
    if (!s) return -1;
    /* qemu apple_ans_start: config |= Memory|BusMaster; no DMA/MSI/ACTIVE. */
    s->command = (uint16_t)(s->command | VF_ANS_PCI_COMMAND_ENABLE_MASK);
    return 0;
}

int vf_ans_pci_config_write_seen(const vf_ans_pci_v1 *s) {
    return s && s->config_write_seen ? 1 : 0;
}

uint64_t vf_ans_pci_last_config_write(const vf_ans_pci_v1 *s) {
    return s ? s->last_config_write : 0u;
}

int vf_ans_pci_set_irq(vf_ans_pci_v1 *s, int level) {
    if (!s) return -1;
    level = level ? 1 : 0;
    if (s->msix_enabled) {
        /* MSI-X path: pending latch only; suppress INTx + MSI; no table/PBA
         * write at this stub. AIC sync is a separate bind. */
        s->msix_pending = level;
        s->intx_pending = 0;
        s->msi_pending = 0;
    } else if (s->msi_enabled) {
        /* MSI path: pending latch; suppress INTx; bound message write on
         * assert (fail-closed if unbound). AIC sync is a separate bind. */
        s->msi_pending = level;
        s->intx_pending = 0;
        s->msix_pending = 0;
        if (level) {
            (void)ans_pci_try_msi_message_write(s);
        }
    } else {
        /* Pin/INTx path (qemu apple_ans_set_irq); clear unused MSI/MSI-X. */
        s->intx_pending = level;
        s->msi_pending = 0;
        s->msix_pending = 0;
    }
    (void)ans_pci_sync_aic(s);
    return 0;
}

int vf_ans_pci_irq_pending(const vf_ans_pci_v1 *s) {
    return s && s->intx_pending ? 1 : 0;
}

int vf_ans_pci_msi_enabled(const vf_ans_pci_v1 *s) {
    return s && s->msi_enabled ? 1 : 0;
}

int vf_ans_pci_msi_pending(const vf_ans_pci_v1 *s) {
    return s && s->msi_pending ? 1 : 0;
}

int vf_ans_pci_msix_enabled(const vf_ans_pci_v1 *s) {
    return s && s->msix_enabled ? 1 : 0;
}

int vf_ans_pci_msix_pending(const vf_ans_pci_v1 *s) {
    return s && s->msix_pending ? 1 : 0;
}

uint8_t vf_ans_pci_msix_table_bir(const vf_ans_pci_v1 *s) {
    return s ? s->msix_table_bir : 0u;
}

int vf_ans_pci_delivery_pending(const vf_ans_pci_v1 *s) {
    return s ? ans_pci_delivery_pending_level(s) : 0;
}

uint64_t vf_ans_pci_msi_addr(const vf_ans_pci_v1 *s) {
    return s ? s->msi_addr : 0u;
}

uint16_t vf_ans_pci_msi_data(const vf_ans_pci_v1 *s) {
    return s ? s->msi_data : 0u;
}

int vf_ans_pci_bind_msi_gpa(vf_ans_pci_v1 *s, void *mem, uint32_t len,
                            uint64_t base_gpa) {
    if (!s) return -1;
    if (!mem) {
        s->msi_gpa_mem = 0;
        s->msi_gpa_len = 0;
        s->msi_gpa_base = 0;
        /* Unbind clears write-seen; last_* kept for prior observability. */
        s->msi_message_written = 0;
        return 0;
    }
    if (len < VF_ANS_PCI_MSI_MSG_BYTES) return -1;
    s->msi_gpa_mem = (uint8_t *)mem;
    s->msi_gpa_len = len;
    s->msi_gpa_base = base_gpa;
    return 0;
}

int vf_ans_pci_msi_gpa_bound(const vf_ans_pci_v1 *s) {
    if (!s) return -1;
    return s->msi_gpa_mem ? 1 : 0;
}

int vf_ans_pci_bind_msi_message(vf_ans_pci_v1 *s, vf_ans_pci_msi_message_fn fn,
                                void *opaque) {
    if (!s) return -1;
    s->msi_message_fn = fn;
    s->msi_message_opaque = fn ? opaque : NULL;
    if (!fn) {
        /* Unbind clears write-seen honesty when GPA also unbound; last_* kept. */
        if (!s->msi_gpa_mem) {
            s->msi_message_written = 0;
        }
    }
    return 0;
}

int vf_ans_pci_msi_message_written(const vf_ans_pci_v1 *s) {
    return s && s->msi_message_written ? 1 : 0;
}

uint64_t vf_ans_pci_last_msi_message_addr(const vf_ans_pci_v1 *s) {
    return s ? s->last_msi_message_addr : 0u;
}

uint16_t vf_ans_pci_last_msi_message_data(const vf_ans_pci_v1 *s) {
    return s ? s->last_msi_message_data : 0u;
}

int vf_ans_pci_bind_aic(vf_ans_pci_v1 *s, vf_ans_pci_aic_fn fn, void *opaque) {
    if (!s) return -1;
    s->aic_fn = fn;
    s->aic_opaque = fn ? opaque : NULL;
    if (!fn) {
        /* Unbind clears sync-seen honesty; does not invent a deassert without
         * a sink (fail-closed). Prior AIC line state is caller-owned. */
        s->aic_sync_seen = 0;
        s->aic_line_high = 0;
        return 0;
    }
    /* Bind syncs current delivery pending immediately (fail-closed on fn err). */
    return ans_pci_sync_aic(s);
}

int vf_ans_pci_aic_sync_seen(const vf_ans_pci_v1 *s) {
    return s && s->aic_sync_seen ? 1 : 0;
}

int vf_ans_pci_aic_line_high(const vf_ans_pci_v1 *s) {
    return s && s->aic_line_high ? 1 : 0;
}

int vf_ans_pci_read(vf_ans_pci_v1 *s, uint32_t offset, unsigned width,
                    uint64_t *value) {
    if (!s || !value) return -1;
    if (offset >= VF_ANS_PCI_MMIO_SIZE) return -1;
    switch (offset) {
    case VF_ANS_PCI_MMIO_STATUS:
        if (width != 32) return -1;
        *value = s->status;
        return 0;
    case VF_ANS_PCI_MMIO_FLAGS:
        if (width != 32) return -1;
        *value = s->flags;
        return 0;
    case VF_ANS_PCI_MMIO_TIER:
        if (width != 32) return -1;
        *value = VF_ANS_PCI_TIER_HOST;
        return 0;
    case VF_ANS_PCI_MMIO_IOPORT_SIZE:
        if (width != 32) return -1;
        *value = VF_ANS_PCI_IOPORT_SIZE;
        return 0;
    case VF_ANS_PCI_MMIO_MMCFG_SIZE:
        if (width != 32) return -1;
        *value = VF_ANS_PCI_MMCFG_SIZE;
        return 0;
    case VF_ANS_PCI_MMIO_CONFIG:
        /* Guest-visible: latched Memory|BusMaster only (no ECAM device IDs). */
        if (width != 64) return -1;
        *value = s->command;
        return 0;
    case VF_ANS_PCI_MMIO_MSI_CTRL:
        if (width != 32) return -1;
        *value = s->msi_enabled ? VF_ANS_PCI_MSI_ENABLE : 0u;
        return 0;
    case VF_ANS_PCI_MMIO_MSI_ADDR:
        if (width != 64) return -1;
        *value = s->msi_addr;
        return 0;
    case VF_ANS_PCI_MMIO_MSI_DATA:
        if (width != 32) return -1;
        *value = s->msi_data;
        return 0;
    case VF_ANS_PCI_MMIO_MSIX_CTRL:
        if (width != 32) return -1;
        *value = s->msix_enabled ? VF_ANS_PCI_MSIX_ENABLE : 0u;
        return 0;
    case VF_ANS_PCI_MMIO_MSIX_TABLE_BIR:
        if (width != 32) return -1;
        *value = s->msix_table_bir;
        return 0;
    default:
        return -1;
    }
}

int vf_ans_pci_write(vf_ans_pci_v1 *s, uint32_t offset, unsigned width,
                     uint64_t value) {
    if (!s) return -1;
    switch (offset) {
    case VF_ANS_PCI_MMIO_CONFIG:
        if (width != 64) return -1;
        /* Latch Memory|BusMaster only; raw WO kept for host/test observability.
         * Does not enable command DMA, MSI, ECAM, or promote status to ACTIVE. */
        s->last_config_write = value;
        s->config_write_seen = 1;
        s->command = (uint16_t)(value & VF_ANS_PCI_COMMAND_ENABLE_MASK);
        return 0;
    case VF_ANS_PCI_MMIO_MSI_CTRL:
        if (width != 32) return -1;
        /* Enable bit only; other bits drop. No ACTIVE. */
        ans_pci_apply_msi_enable(s, (value & VF_ANS_PCI_MSI_ENABLE) ? 1 : 0);
        return 0;
    case VF_ANS_PCI_MMIO_MSI_ADDR:
        if (width != 64) return -1;
        /* Prepare; written out only via bound MSI message sink on pending. */
        s->msi_addr = value;
        return 0;
    case VF_ANS_PCI_MMIO_MSI_DATA:
        if (width != 32) return -1;
        s->msi_data = (uint16_t)(value & 0xffffu);
        return 0;
    case VF_ANS_PCI_MMIO_MSIX_CTRL:
        if (width != 32) return -1;
        /* Enable bit only; other bits drop. No table/PBA write / ACTIVE. */
        ans_pci_apply_msix_enable(s, (value & VF_ANS_PCI_MSIX_ENABLE) ? 1 : 0);
        return 0;
    case VF_ANS_PCI_MMIO_MSIX_TABLE_BIR:
        if (width != 32) return -1;
        /* BIR bits 2:0 only; Table Offset / PBA deferred. */
        s->msix_table_bir = (uint8_t)(value & VF_ANS_PCI_MSIX_BIR_MASK);
        return 0;
    default:
        /* Status/flags/tier/sizes stay RO; unknown offsets fail closed. */
        return -1;
    }
}
