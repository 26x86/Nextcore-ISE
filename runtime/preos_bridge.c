/* SPDX-License-Identifier: BSD-4-Clause
 * C owns this boundary's firmware-facing execution capability.  Rust can ask
 * for an execution but cannot dereference the capability or access vf_run().
 */
#include "preos_bridge.h"

#include "../devices/adp_display_v1.h"
#include "../devices/aic_v1.h"
#include "../devices/ans_pci_v1.h"
#include "../devices/ans_v1.h"
#include "../devices/apple_gxf_v1.h"
#include "../devices/apple_uart_v1.h"
#ifndef VF_EFI_BUILD
#include "../devices/apple_smc_v1.h"
#endif
#include "../devices/dart_v1.h"
#include "../devices/nvram_v1.h"
#include "../devices/recovery_v1.h"
#include "../devices/sart_v1.h"
#include "../devices/sep_mailbox_v1.h"
#include "../devices/storage_v1.h"
#include "../devices/timer_v1.h"

#include <stddef.h>
#ifdef VF_EFI_BUILD
/* The canonical EFI entry supplies this freestanding memory primitive. */
void *memset(void *destination, int value, size_t bytes);
#else
#include <string.h>
#endif

/* Graph-local NVRAM MMIO offsets (not Apple NVMe namespace layout). */
#define VF_M1_NVRAM_MMIO_NSID         0x000u
#define VF_M1_NVRAM_MMIO_NSTYPE       0x004u
#define VF_M1_NVRAM_MMIO_ENV_COUNT    0x008u
#define VF_M1_NVRAM_MMIO_BOOT_PRESENT 0x00cu
#define VF_M1_NVRAM_MMIO_COMMIT       0x010u
#define VF_M1_NVRAM_MMIO_APPEND       0x014u

static vf_aic_v1 g_m1_aic;
static vf_ans_pci_v1 g_m1_ans_pci;
static vf_ans_v1 g_m1_ans;
/*
 * Research/test MSI Message Address GPA stand-in (host buffer only).
 * Auto-bound to bridge ans_pci on mmio_reset via vf_ans_pci_bind_msi_gpa.
 * ≠ live guest RAM / MSI-X / Apple DT.
 */
#define VF_M1_ANS_PCI_MSI_GPA_BASE  0xfee00000ull
#define VF_M1_ANS_PCI_MSI_GPA_SIZE  16u
static uint8_t g_m1_ans_pci_msi_gpa[VF_M1_ANS_PCI_MSI_GPA_SIZE];
/* Host/test Identify CNS=CTRL DMA + admin CQE stand-ins (bridge-owned). */
static uint8_t g_m1_ans_id_buf[VF_ANS_ID_CTRL_SIZE];
static uint8_t g_m1_ans_cqe_buf[VF_ANS_CQE_SIZE];
static uint16_t g_m1_ans_admin_sq_tail;
static vf_timer_v1 g_m1_timer;
static vf_storage_v1 g_m1_storage;
static vf_recovery_v1 g_m1_recovery;
static vf_dart_v1 g_m1_dart;
static vf_sart_v1 g_m1_sart;
static vf_nvram_v1 g_m1_nvram;
static vf_nvram_blk_backend g_m1_nvram_blk;
static vf_nvram_file_blk g_m1_nvram_file = { .fd = -1, .length = -1 };
static int g_m1_nvram_blk_attached;
static vf_apple_uart_v1 g_m1_uart;
static vf_adp_display_v1 g_m1_adp;
static vf_sep_mailbox_v1 g_m1_sep;
#ifndef VF_EFI_BUILD
static vf_smc_v1 g_m1_smc;
static uint64_t g_m1_smc_last_resp;
#endif
static vf_apple_gxf_v1 g_m1_gxf;
/* Optional guest JIT CPU for SPRR→TLB flush. NULL = fail-closed (count only). */
static vf_cpu *g_m1_gxf_tlb_cpu;
static char g_m1_nvram_boot_args[VF_NVRAM_ENV_VALUE_MAX];
static size_t g_m1_nvram_boot_args_len;
static int g_m1_mmio_ready;

/* SPRR EL0 merge hook: real guest JIT TLB invalidate when a CPU is bound.
 * Not a QEMU SoftMMU / live TCG TLB; unbound ctx fails closed (no-op). */
static void m1_gxf_sprr_guest_tlb_flush(void *ctx, uint64_t perm_generation) {
    vf_cpu *cpu = ctx ? (vf_cpu *)ctx : g_m1_gxf_tlb_cpu;
    (void)perm_generation;
    if (!cpu) return;
    vf_cpu_invalidate_tlb(cpu);
}

static int mmio_width_valid(unsigned width_bits) {
    return width_bits == 8 || width_bits == 16 || width_bits == 32 || width_bits == 64;
}

static int aic_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    uint32_t word = 0;
    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (width_bits != 32) return VF_M1_MMIO_WIDTH;
    if (vf_aic_read(&g_m1_aic, 0, offset, 4, &word) != 0) return VF_M1_MMIO_INVALID;
    *value = word;
    return VF_M1_MMIO_OK;
}

static int aic_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    if (width_bits != 32) return VF_M1_MMIO_WIDTH;
    if (vf_aic_write(&g_m1_aic, 0, offset, 4, (uint32_t)value) != 0) return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int dart_sync_aic_irq(void) {
    return vf_aic_set_line(&g_m1_aic, VF_M1_DART_IRQ_LINE, vf_dart_irq_pending(&g_m1_dart));
}

static int dart_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    uint32_t word = 0;
    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (width_bits != 32) return VF_M1_MMIO_WIDTH;
    if (vf_dart_read(&g_m1_dart, offset, 4, &word) != 0) return VF_M1_MMIO_INVALID;
    *value = word;
    return VF_M1_MMIO_OK;
}

static int dart_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    if (width_bits != 32) return VF_M1_MMIO_WIDTH;
    if (vf_dart_write(&g_m1_dart, offset, 4, (uint32_t)value) != 0) return VF_M1_MMIO_INVALID;
    if (offset == VF_DART_REG_ERROR_STATUS && dart_sync_aic_irq() != 0)
        return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int sart_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    uint32_t word = 0;
    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (width_bits != 32) return VF_M1_MMIO_WIDTH;
    if (vf_sart_read(&g_m1_sart, offset, 4, &word) != 0) return VF_M1_MMIO_INVALID;
    *value = word;
    return VF_M1_MMIO_OK;
}

static int sart_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    if (width_bits != 32) return VF_M1_MMIO_WIDTH;
    if (vf_sart_write(&g_m1_sart, offset, 4, (uint32_t)value) != 0) return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int nvram_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    const vf_nvram_ns_info *meta = 0;
    const char *boot_args;

    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (width_bits != 32) return VF_M1_MMIO_WIDTH;
    switch (offset) {
    case VF_M1_NVRAM_MMIO_NSID:
        vf_nvram_get_namespace(&meta);
        *value = meta ? meta->nsid : 0;
        return VF_M1_MMIO_OK;
    case VF_M1_NVRAM_MMIO_NSTYPE:
        vf_nvram_get_namespace(&meta);
        *value = meta ? meta->nstype : 0;
        return VF_M1_MMIO_OK;
    case VF_M1_NVRAM_MMIO_ENV_COUNT:
        *value = g_m1_nvram.env_count;
        return VF_M1_MMIO_OK;
    case VF_M1_NVRAM_MMIO_BOOT_PRESENT:
        boot_args = vf_nvram_env_get(&g_m1_nvram, "boot-args");
        *value = boot_args && boot_args[0] ? 1u : 0u;
        return VF_M1_MMIO_OK;
    default:
        return VF_M1_MMIO_INVALID;
    }
}

static int uart_sync_aic_irq(void) {
    return vf_aic_set_line(&g_m1_aic, VF_M1_UART_IRQ_LINE,
                           vf_apple_uart_irq_pending(&g_m1_uart));
}

static int sep_sync_aic_irq(void) {
    return vf_aic_set_line(&g_m1_aic, VF_M1_SEP_IRQ_LINE,
                           vf_sep_mailbox_irq_pending(&g_m1_sep));
}

static int timer_sync_aic_irq(void) {
    return vf_aic_set_line(&g_m1_aic, VF_M1_TIMER_IRQ_LINE,
                           vf_timer_irq_pending(&g_m1_timer));
}

static int storage_sync_aic_irq(void) {
    return vf_aic_set_line(&g_m1_aic, VF_M1_STORAGE_IRQ_LINE,
                           vf_storage_irq_pending(&g_m1_storage));
}

/*
 * Bridge sink for vf_ans_pci_bind_aic: mirror INTx|MSI delivery_pending onto
 * graph STORAGE AIC line 1. Fail-closed if MMIO (AIC) not ready or irq
 * is not the pattern-matched STORAGE line. ≠ window 0x4 ownership.
 */
static int m1_ans_pci_aic_sink(void *opaque, unsigned irq, int high) {
    (void)opaque;
    if (!g_m1_mmio_ready) return -1;
    if (irq != VF_ANS_PCI_AIC_STORAGE_IRQ_LINE) return -1;
    return vf_aic_set_line(&g_m1_aic, VF_M1_STORAGE_IRQ_LINE, high);
}

/* Auto-wire ans_pci → STORAGE AIC. Fail-closed on bind error. */
static int ans_pci_auto_bind_aic(void) {
    return vf_ans_pci_bind_aic(&g_m1_ans_pci, m1_ans_pci_aic_sink, &g_m1_aic);
}

/*
 * Auto-wire research MSI GPA window → bridge ans_pci. Clears stand-in
 * buffer then binds via vf_ans_pci_bind_msi_gpa. Fail-closed on bind error.
 * ≠ live guest RAM / MSI-X / Apple DT.
 */
static int ans_pci_auto_bind_msi_gpa(void) {
    memset(g_m1_ans_pci_msi_gpa, 0, sizeof(g_m1_ans_pci_msi_gpa));
    return vf_ans_pci_bind_msi_gpa(&g_m1_ans_pci, g_m1_ans_pci_msi_gpa,
                                   (uint32_t)sizeof(g_m1_ans_pci_msi_gpa),
                                   VF_M1_ANS_PCI_MSI_GPA_BASE);
}

/* Auto-wire bridge ans_v1 → bridge ans_pci (Identify CQ irq_check → PCI). */
static int ans_auto_bind_pci(void) {
    return vf_ans_bind_pci(&g_m1_ans, &g_m1_ans_pci);
}

static int recovery_sync_aic_irq(void) {
    return vf_aic_set_line(&g_m1_aic, VF_M1_RECOVERY_IRQ_LINE,
                           vf_recovery_irq_pending(&g_m1_recovery));
}

static int storage_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (vf_storage_read(&g_m1_storage, offset, width_bits, value) != 0)
        return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int storage_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    (void)offset;
    (void)width_bits;
    (void)value;
    /* Match Rust M1LogicalWindow::Storage — guest MMIO is read-only. */
    return VF_M1_MMIO_READONLY;
}

static int recovery_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (vf_recovery_read(&g_m1_recovery, offset, width_bits, value) != 0)
        return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int recovery_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    (void)offset;
    (void)width_bits;
    (void)value;
    /* Match Rust M1LogicalWindow::Recovery — guest MMIO is read-only. */
    return VF_M1_MMIO_READONLY;
}

static int timer_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (vf_timer_read(&g_m1_timer, offset, width_bits, value) != 0)
        return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int timer_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    if (vf_timer_write(&g_m1_timer, offset, width_bits, value) != 0)
        return VF_M1_MMIO_INVALID;
    if (timer_sync_aic_irq() != 0) return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int adp_sync_aic_irq(void) {
    return vf_aic_set_line(&g_m1_aic, VF_M1_ADP_IRQ_LINE,
                           vf_adp_display_irq_pending(&g_m1_adp));
}

static int adp_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (vf_adp_display_read(&g_m1_adp, offset, width_bits, value) != 0)
        return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int adp_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    if (vf_adp_display_write(&g_m1_adp, offset, width_bits, value) != 0)
        return VF_M1_MMIO_INVALID;
    if (adp_sync_aic_irq() != 0) return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int uart_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    uint32_t word = 0;
    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (width_bits != 32) return VF_M1_MMIO_WIDTH;
    if (vf_apple_uart_read(&g_m1_uart, offset, 4, &word) != 0) return VF_M1_MMIO_INVALID;
    *value = word;
    return VF_M1_MMIO_OK;
}

static int uart_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    if (width_bits != 32) return VF_M1_MMIO_WIDTH;
    if (vf_apple_uart_write(&g_m1_uart, offset, 4, (uint32_t)value) != 0) return VF_M1_MMIO_INVALID;
    if (uart_sync_aic_irq() != 0) return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

#ifndef VF_EFI_BUILD
static int smc_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    uint32_t count = 0;
    uint8_t len = 0;

    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    switch (offset) {
    case VF_M1_SMC_MMIO_MSG_OUT:
        if (width_bits != 64) return VF_M1_MMIO_WIDTH;
        *value = g_m1_smc_last_resp;
        return VF_M1_MMIO_OK;
    case VF_M1_SMC_MMIO_KEY_COUNT:
        if (width_bits != 32) return VF_M1_MMIO_WIDTH;
        if (vf_smc_read_key(&g_m1_smc, VF_SMC_KEY_NKEY, &count, sizeof(count), &len) != 0)
            return VF_M1_MMIO_INVALID;
        *value = count;
        return VF_M1_MMIO_OK;
    case VF_M1_SMC_MMIO_SRAM_ADDR:
        if (width_bits != 64) return VF_M1_MMIO_WIDTH;
        *value = g_m1_smc.sram_addr;
        return VF_M1_MMIO_OK;
    default:
        return VF_M1_MMIO_INVALID;
    }
}

static int smc_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    if (offset != VF_M1_SMC_MMIO_MSG_IN) return VF_M1_MMIO_READONLY;
    if (width_bits != 64) return VF_M1_MMIO_WIDTH;
    g_m1_smc_last_resp = 0;
    if (vf_smc_handle_msg(&g_m1_smc, value, &g_m1_smc_last_resp) != 0) return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}
#else
static int smc_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (offset == VF_M1_SMC_MMIO_KEY_COUNT && width_bits == 32) {
        *value = VF_SMC_BOOT_KEY_COUNT;
        return VF_M1_MMIO_OK;
    }
    return VF_M1_MMIO_UNAVAILABLE;
}

static int smc_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    (void)offset;
    (void)width_bits;
    (void)value;
    return VF_M1_MMIO_UNAVAILABLE;
}
#endif

static int sep_dispatch_read(uint32_t offset, unsigned width_bits, uint64_t *value) {
    if (!g_m1_mmio_ready || !value) return VF_M1_MMIO_UNAVAILABLE;
    if (vf_sep_mailbox_read(&g_m1_sep, offset, width_bits, value) != 0)
        return VF_M1_MMIO_INVALID;
    /* MSG_OUT read clears reply_pending — deassert graph SEP AIC line. */
    if (offset == VF_SEP_MMIO_MSG_OUT && sep_sync_aic_irq() != 0)
        return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int sep_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    if (offset != VF_SEP_MMIO_MSG_IN) return VF_M1_MMIO_READONLY;
    if (vf_sep_mailbox_write(&g_m1_sep, offset, width_bits, value) != 0)
        return VF_M1_MMIO_INVALID;
    if (sep_sync_aic_irq() != 0) return VF_M1_MMIO_INVALID;
    return VF_M1_MMIO_OK;
}

static int nvram_dispatch_write(uint32_t offset, unsigned width_bits, uint64_t value) {
    if (!g_m1_mmio_ready) return VF_M1_MMIO_UNAVAILABLE;
    if (width_bits != 32) return VF_M1_MMIO_WIDTH;
    switch (offset) {
    case VF_M1_NVRAM_MMIO_APPEND:
        if (g_m1_nvram_boot_args_len >= sizeof(g_m1_nvram_boot_args) - 1u) return VF_M1_MMIO_INVALID;
        g_m1_nvram_boot_args[g_m1_nvram_boot_args_len++] = (char)(value & 0xffu);
        g_m1_nvram_boot_args[g_m1_nvram_boot_args_len] = '\0';
        return VF_M1_MMIO_OK;
    case VF_M1_NVRAM_MMIO_COMMIT:
        if (value != 1u) return VF_M1_MMIO_INVALID;
        if (vf_nvram_env_set(&g_m1_nvram, "boot-args", g_m1_nvram_boot_args, 0) != 0)
            return VF_M1_MMIO_INVALID;
        g_m1_nvram_boot_args_len = 0;
        g_m1_nvram_boot_args[0] = '\0';
        return VF_M1_MMIO_OK;
    default:
        return VF_M1_MMIO_READONLY;
    }
}

void VF_PREOS_ABI vf_m1_guest_mmio_reset(void) {
    g_m1_mmio_ready = 0;
    g_m1_nvram_boot_args_len = 0;
    g_m1_nvram_boot_args[0] = '\0';
    if (g_m1_nvram_blk_attached) {
        (void)vf_nvram_blk_detach(&g_m1_nvram_blk, &g_m1_nvram_file);
        g_m1_nvram_blk_attached = 0;
    }
    if (vf_aic_init(&g_m1_aic, VF_M1_AIC_IRQ_COUNT, VF_M1_AIC_CPU_COUNT) != 0) return;
    if (vf_ans_pci_init(&g_m1_ans_pci) != 0) return;
    if (vf_ans_init(&g_m1_ans) != 0) return;
    g_m1_ans_admin_sq_tail = 0;
    memset(g_m1_ans_id_buf, 0, sizeof(g_m1_ans_id_buf));
    memset(g_m1_ans_cqe_buf, 0, sizeof(g_m1_ans_cqe_buf));
    if (vf_timer_init(&g_m1_timer) != 0) return;
    if (vf_storage_init(&g_m1_storage) != 0) return;
    if (vf_recovery_init(&g_m1_recovery) != 0) return;
    if (vf_dart_init(&g_m1_dart) != 0) return;
    if (vf_sart_init(&g_m1_sart, VF_SART_VERSION_1) != 0) return;
    if (vf_nvram_init(&g_m1_nvram) != 0) return;
#ifndef VF_EFI_BUILD
    g_m1_smc_last_resp = 0;
    if (vf_smc_init(&g_m1_smc) != 0) return;
#endif
    if (vf_apple_uart_init(&g_m1_uart, 0, 0) != 0) return;
    if (vf_adp_display_init(&g_m1_adp) != 0) return;
    if (vf_sep_mailbox_init(&g_m1_sep) != 0) return;
    if (vf_apple_gxf_init(&g_m1_gxf) != 0) return;
    /* Re-arm SPRR→guest TLB hook after init clears callbacks; CPU bind is sticky. */
    vf_apple_gxf_set_sprr_tlb_flush(&g_m1_gxf, m1_gxf_sprr_guest_tlb_flush,
                                    g_m1_gxf_tlb_cpu);
    g_m1_mmio_ready = 1;
    /* Announce leaves reply_pending; mirror onto graph SEP AIC line 6. */
    if (sep_sync_aic_irq() != 0) {
        g_m1_mmio_ready = 0;
        return;
    }
    /* Auto-wire ans_v1 Identify CQ irq_check → ans_pci delivery_pending. */
    if (ans_auto_bind_pci() != 0) {
        g_m1_mmio_ready = 0;
        return;
    }
    /* Auto-wire ANS PCI delivery_pending → STORAGE AIC line 1. Fail-closed. */
    if (ans_pci_auto_bind_aic() != 0) {
        g_m1_mmio_ready = 0;
        return;
    }
    /* Auto-wire research MSI GPA stand-in → ans_pci. Fail-closed. */
    if (ans_pci_auto_bind_msi_gpa() != 0) {
        g_m1_mmio_ready = 0;
        return;
    }
}

int VF_PREOS_ABI vf_m1_guest_aic_set_line(unsigned irq, int high) {
    if (!g_m1_mmio_ready) return -1;
    return vf_aic_set_line(&g_m1_aic, irq, high);
}

int VF_PREOS_ABI vf_m1_guest_dart_simulate_fault(unsigned sid, uint32_t code) {
    if (!g_m1_mmio_ready) return -1;
    if (vf_dart_simulate_fault(&g_m1_dart, sid, code) != 0) return -1;
    return dart_sync_aic_irq();
}

uint32_t VF_PREOS_ABI vf_m1_guest_dart_tlb_generation(void) {
    if (!g_m1_mmio_ready) return 0;
    return vf_dart_tlb_generation(&g_m1_dart);
}

uint32_t VF_PREOS_ABI vf_m1_guest_sart_generation(void) {
    if (!g_m1_mmio_ready) return 0;
    return vf_sart_generation(&g_m1_sart);
}

int VF_PREOS_ABI vf_m1_guest_sart_translate(uint64_t iova, uint64_t *pa_out,
                                            unsigned *region_out) {
    if (!g_m1_mmio_ready) return -1;
    return vf_sart_translate(&g_m1_sart, iova, pa_out, region_out);
}

int VF_PREOS_ABI vf_m1_guest_uart_push_rx(uint8_t byte) {
    if (!g_m1_mmio_ready) return -1;
    if (vf_apple_uart_push_rx(&g_m1_uart, byte) != 0) return -1;
    return uart_sync_aic_irq();
}

int VF_PREOS_ABI vf_m1_guest_timer_advance(uint64_t ticks) {
    if (!g_m1_mmio_ready) return -1;
    if (vf_timer_advance(&g_m1_timer, ticks) != 0) return -1;
    return timer_sync_aic_irq();
}

int VF_PREOS_ABI vf_m1_guest_storage_attach(uint64_t block_count, int read_only) {
    if (!g_m1_mmio_ready) return -1;
    if (vf_storage_attach(&g_m1_storage, block_count, read_only) != 0) return -1;
    return storage_sync_aic_irq();
}

int VF_PREOS_ABI vf_m1_guest_storage_detach(void) {
    if (!g_m1_mmio_ready) return -1;
    if (vf_storage_detach(&g_m1_storage) != 0) return -1;
    return storage_sync_aic_irq();
}

/*
 * Re-bind bridge ans_pci → STORAGE AIC line 1 (after unbind). Fail-closed if
 * MMIO not ready. Reset already auto-binds; this restores the wire.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_bind_aic(void) {
    if (!g_m1_mmio_ready) return -1;
    return ans_pci_auto_bind_aic();
}

/*
 * Unbind ans_pci AIC wire. Fail-closed: delivery pending may latch without
 * AIC sync. Does not invent a deassert. Idempotent.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_unbind_aic(void) {
    if (!g_m1_mmio_ready) return -1;
    return vf_ans_pci_bind_aic(&g_m1_ans_pci, NULL, NULL);
}

/* Host/test: drive ans_pci INTx/MSI pending (exercises bound AIC mirror). */
int VF_PREOS_ABI vf_m1_guest_ans_pci_set_irq(int level) {
    if (!g_m1_mmio_ready) return -1;
    return vf_ans_pci_set_irq(&g_m1_ans_pci, level);
}

int VF_PREOS_ABI vf_m1_guest_ans_pci_delivery_pending(void) {
    if (!g_m1_mmio_ready) return 0;
    return vf_ans_pci_delivery_pending(&g_m1_ans_pci);
}

int VF_PREOS_ABI vf_m1_guest_ans_pci_aic_sync_seen(void) {
    if (!g_m1_mmio_ready) return 0;
    return vf_ans_pci_aic_sync_seen(&g_m1_ans_pci);
}

int VF_PREOS_ABI vf_m1_guest_ans_pci_aic_line_high(void) {
    if (!g_m1_mmio_ready) return 0;
    return vf_ans_pci_aic_line_high(&g_m1_ans_pci);
}

/*
 * Re-bind bridge research MSI GPA window → ans_pci (after unbind).
 * Fail-closed if MMIO not ready. Reset already auto-binds.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_bind_msi_gpa(void) {
    if (!g_m1_mmio_ready) return -1;
    return ans_pci_auto_bind_msi_gpa();
}

/*
 * Unbind ans_pci MSI GPA wire. Fail-closed: MSI pending may latch without
 * message write. Idempotent. ≠ MSI-X / live guest RAM.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_unbind_msi_gpa(void) {
    if (!g_m1_mmio_ready) return -1;
    return vf_ans_pci_bind_msi_gpa(&g_m1_ans_pci, NULL, 0, 0);
}

/* 1 when research MSI GPA window bound; 0 unbound; -1 if MMIO not ready. */
int VF_PREOS_ABI vf_m1_guest_ans_pci_msi_gpa_bound(void) {
    if (!g_m1_mmio_ready) return -1;
    return vf_ans_pci_msi_gpa_bound(&g_m1_ans_pci);
}

/*
 * Host/test: MSI Enable + prepare Message Address/Data on bridge ans_pci.
 * addr==0 uses research GPA base. Clear with enable=0 (addr/data ignored).
 * Fail-closed if MMIO not ready. Does not claim MSI-X / live guest RAM.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_prepare_msi(int enable, uint64_t addr,
                                                 uint16_t data) {
    uint64_t use_addr;

    if (!g_m1_mmio_ready) return -1;
    if (!enable) {
        if (vf_ans_pci_write(&g_m1_ans_pci, VF_ANS_PCI_MMIO_MSI_CTRL, 32, 0)
            != 0)
            return -1;
        return 0;
    }
    use_addr = addr ? addr : VF_M1_ANS_PCI_MSI_GPA_BASE;
    if (vf_ans_pci_write(&g_m1_ans_pci, VF_ANS_PCI_MMIO_MSI_ADDR, 64, use_addr)
        != 0)
        return -1;
    if (vf_ans_pci_write(&g_m1_ans_pci, VF_ANS_PCI_MMIO_MSI_DATA, 32,
                         (uint64_t)data) != 0)
        return -1;
    if (vf_ans_pci_write(&g_m1_ans_pci, VF_ANS_PCI_MMIO_MSI_CTRL, 32,
                         VF_ANS_PCI_MSI_ENABLE) != 0)
        return -1;
    return 0;
}

/* 1 after a successful bound MSI message write on bridge ans_pci. */
int VF_PREOS_ABI vf_m1_guest_ans_pci_msi_message_written(void) {
    if (!g_m1_mmio_ready) return 0;
    return vf_ans_pci_msi_message_written(&g_m1_ans_pci);
}

/*
 * Host/test: peek one byte from the research MSI GPA stand-in at off.
 * Returns 0..255 on success, -1 if MMIO not ready or off OOB.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_msi_gpa_byte(uint32_t off) {
    if (!g_m1_mmio_ready) return -1;
    if (off >= sizeof(g_m1_ans_pci_msi_gpa)) return -1;
    return (int)g_m1_ans_pci_msi_gpa[off];
}

/*
 * Host/test: MSI-X Enable + Table BIR prepare on bridge ans_pci.
 * Clear with enable=0 (table_bir ignored). Fail-closed if MMIO not ready.
 * Does not claim PBA/table MMIO / MSI-X message write / Apple DT.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_prepare_msix(int enable,
                                                   uint8_t table_bir) {
    if (!g_m1_mmio_ready) return -1;
    if (!enable) {
        if (vf_ans_pci_write(&g_m1_ans_pci, VF_ANS_PCI_MMIO_MSIX_CTRL, 32, 0)
            != 0)
            return -1;
        return 0;
    }
    if (vf_ans_pci_write(&g_m1_ans_pci, VF_ANS_PCI_MMIO_MSIX_TABLE_BIR, 32,
                         (uint64_t)(table_bir & VF_ANS_PCI_MSIX_BIR_MASK))
        != 0)
        return -1;
    if (vf_ans_pci_write(&g_m1_ans_pci, VF_ANS_PCI_MMIO_MSIX_CTRL, 32,
                         VF_ANS_PCI_MSIX_ENABLE) != 0)
        return -1;
    return 0;
}

/* 1 when bridge ans_pci msix_pending is latched; 0 otherwise / MMIO not ready. */
int VF_PREOS_ABI vf_m1_guest_ans_pci_msix_pending(void) {
    if (!g_m1_mmio_ready) return 0;
    return vf_ans_pci_msix_pending(&g_m1_ans_pci);
}

/*
 * Re-bind bridge ans_v1 → ans_pci (Identify CQ irq_check → PCI). Fail-closed
 * if MMIO not ready. Reset already auto-binds.
 */
int VF_PREOS_ABI vf_m1_guest_ans_bind_pci(void) {
    if (!g_m1_mmio_ready) return -1;
    return ans_auto_bind_pci();
}

/*
 * Unbind ans→pci. Fail-closed: irq_check may stay set without PCI/AIC sync.
 * Idempotent.
 */
int VF_PREOS_ABI vf_m1_guest_ans_unbind_pci(void) {
    if (!g_m1_mmio_ready) return -1;
    return vf_ans_bind_pci(&g_m1_ans, NULL);
}

/*
 * Host/test: enable BusMaster on bridge ans_pci + CC.EN on bridge ans_v1 so
 * Identify DMA/CQ may proceed. Fail-closed if MMIO not ready.
 */
int VF_PREOS_ABI vf_m1_guest_ans_enable_identify_path(void) {
    if (!g_m1_mmio_ready) return -1;
    if (vf_ans_pci_enable_memory_bus_master(&g_m1_ans_pci) != 0) return -1;
    /* CC clear resets doorbells so bridge SQ-tail shadow stays honest. */
    if (vf_ans_write(&g_m1_ans, VF_ANS_REG_CC, 4, 0) != 0) return -1;
    if (vf_ans_write(&g_m1_ans, VF_ANS_REG_CC, 4, VF_ANS_CC_EN) != 0) return -1;
    g_m1_ans_admin_sq_tail = 0;
    return 0;
}

/*
 * Host/test: arm Identify CNS=CTRL + admin SQ doorbell one-shot. On success
 * bound PCI mirrors irq_check into delivery_pending (+ AIC when bound).
 * Fail-closed without CC.EN / BusMaster (when pci bound) / arm errors.
 */
int VF_PREOS_ABI vf_m1_guest_ans_submit_admin_identify(void) {
    uint16_t next_tail;

    if (!g_m1_mmio_ready) return -1;
    memset(g_m1_ans_id_buf, 0xA5, sizeof(g_m1_ans_id_buf));
    memset(g_m1_ans_cqe_buf, 0x5A, sizeof(g_m1_ans_cqe_buf));
    if (vf_ans_arm_admin_identify(&g_m1_ans, VF_ANS_OPC_IDENTIFY,
                                  VF_ANS_ID_CNS_CTRL, g_m1_ans_id_buf,
                                  VF_ANS_ID_CTRL_SIZE, g_m1_ans_cqe_buf,
                                  VF_ANS_CQE_SIZE) != 0)
        return -1;
    next_tail = (uint16_t)((g_m1_ans_admin_sq_tail + 1u) & VF_ANS_DB_VALUE_MASK);
    if (vf_ans_write(&g_m1_ans, VF_ANS_REG_SQTDBL_ADMIN, 4, next_tail) != 0)
        return -1;
    g_m1_ans_admin_sq_tail = next_tail;
    return 0;
}

int VF_PREOS_ABI vf_m1_guest_ans_irq_check(void) {
    if (!g_m1_mmio_ready) return 0;
    return vf_ans_irq_check(&g_m1_ans);
}

/* Host/test: INTMS (mask!=0) or INTMC (mask==0) on admin CQ bit0. */
int VF_PREOS_ABI vf_m1_guest_ans_mask_admin_irq(int mask) {
    uint32_t reg;

    if (!g_m1_mmio_ready) return -1;
    reg = mask ? VF_ANS_REG_INTMS : VF_ANS_REG_INTMC;
    return vf_ans_write(&g_m1_ans, reg, 4, VF_ANS_IRQ_ADMIN_CQ);
}

/* Host/test: drain admin CQ Head to current Tail (clears irq_check + PCI). */
int VF_PREOS_ABI vf_m1_guest_ans_drain_admin_cq(void) {
    int tail;

    if (!g_m1_mmio_ready) return -1;
    tail = vf_ans_admin_cq_tail(&g_m1_ans);
    if (tail < 0) return -1;
    return vf_ans_write(&g_m1_ans, VF_ANS_REG_CQHDBL_ADMIN, 4, (uint32_t)tail);
}

int VF_PREOS_ABI vf_m1_guest_recovery_ingest(const uint8_t *frame, size_t frame_len) {
    if (!g_m1_mmio_ready || !frame) return -1;
    if (vf_recovery_ingest(&g_m1_recovery, frame, frame_len) != VF_RECOVERY_OK) return -1;
    return recovery_sync_aic_irq();
}

int VF_PREOS_ABI vf_m1_guest_recovery_clear(void) {
    if (!g_m1_mmio_ready) return -1;
    if (vf_recovery_clear_ready(&g_m1_recovery) != 0) return -1;
    return recovery_sync_aic_irq();
}

int VF_PREOS_ABI vf_m1_guest_nvram_ingest_bank(const uint8_t *buf, size_t len) {
    if (!g_m1_mmio_ready || !buf) return -1;
    if (vf_nvram_ingest_bank(&g_m1_nvram, buf, len) != 0) return -1;
    /* Staging buffer is independent of bank ingest; clear to avoid mixed paths. */
    g_m1_nvram_boot_args_len = 0;
    g_m1_nvram_boot_args[0] = '\0';
    return 0;
}

int VF_PREOS_ABI vf_m1_guest_nvram_load_from_blk(const vf_nvram_blk_backend *blk) {
    if (!g_m1_mmio_ready || !blk) return -1;
    if (vf_nvram_load_from_blk(&g_m1_nvram, blk) != 0) return -1;
    g_m1_nvram_boot_args_len = 0;
    g_m1_nvram_boot_args[0] = '\0';
    return 0;
}

int VF_PREOS_ABI vf_m1_guest_nvram_attach_research_image(const char *path) {
    if (!g_m1_mmio_ready || !path || path[0] == '\0') return -1;
    if (g_m1_nvram_blk_attached) {
        (void)vf_nvram_blk_detach(&g_m1_nvram_blk, &g_m1_nvram_file);
        g_m1_nvram_blk_attached = 0;
    }
    if (vf_nvram_blk_attach_path(&g_m1_nvram_blk, &g_m1_nvram_file, path) != 0) {
        return -1;
    }
    g_m1_nvram_blk_attached = 1;
    if (vf_nvram_load_from_blk(&g_m1_nvram, &g_m1_nvram_blk) != 0) {
        (void)vf_nvram_blk_detach(&g_m1_nvram_blk, &g_m1_nvram_file);
        g_m1_nvram_blk_attached = 0;
        return -1;
    }
    g_m1_nvram_boot_args_len = 0;
    g_m1_nvram_boot_args[0] = '\0';
    return 0;
}

int VF_PREOS_ABI vf_m1_guest_nvram_detach_blk(void) {
    if (!g_m1_mmio_ready) return -1;
    if (g_m1_nvram_blk_attached) {
        (void)vf_nvram_blk_detach(&g_m1_nvram_blk, &g_m1_nvram_file);
        g_m1_nvram_blk_attached = 0;
    }
    return 0;
}

int VF_PREOS_ABI vf_m1_guest_nvram_save_to_attached(void) {
    if (!g_m1_mmio_ready || !g_m1_nvram_blk_attached) return -1;
    return vf_nvram_save_to_blk(&g_m1_nvram, &g_m1_nvram_blk);
}

int VF_PREOS_ABI vf_m1_guest_nvram_boot_args_handoff(char *out, size_t out_len) {
    if (!g_m1_mmio_ready || !out) return -1;
    return vf_nvram_boot_args_handoff(&g_m1_nvram, out, out_len);
}

int VF_PREOS_ABI vf_m1_guest_adp_configure(uint64_t guest_address,
                                           uint64_t backing_bytes,
                                           uint32_t width, uint32_t height,
                                           uint32_t stride) {
    if (!g_m1_mmio_ready) return -1;
    if (vf_adp_display_configure(&g_m1_adp, guest_address, backing_bytes,
                                 width, height, stride) != 0)
        return -1;
    return adp_sync_aic_irq();
}

int VF_PREOS_ABI vf_m1_guest_gxf_read(uint32_t reg, uint64_t *value) {
    if (!g_m1_mmio_ready || !value) return VF_M1_GXF_UNAVAILABLE;
    return vf_apple_gxf_read(&g_m1_gxf, reg, value) == 0 ? VF_M1_GXF_OK : VF_M1_GXF_INVALID;
}

int VF_PREOS_ABI vf_m1_guest_gxf_write_el(uint32_t reg, uint64_t value, unsigned el) {
    if (!g_m1_mmio_ready) return VF_M1_GXF_UNAVAILABLE;
    return vf_apple_gxf_write_el(&g_m1_gxf, reg, value, el) == 0
               ? VF_M1_GXF_OK
               : VF_M1_GXF_INVALID;
}

int VF_PREOS_ABI vf_m1_guest_gxf_write(uint32_t reg, uint64_t value) {
    return vf_m1_guest_gxf_write_el(reg, value, 1u);
}

int VF_PREOS_ABI vf_m1_guest_gxf_genter(uint64_t return_pc, uint64_t spsr,
                                        uint64_t *enter_pc) {
    if (!g_m1_mmio_ready || !enter_pc) return VF_M1_GXF_UNAVAILABLE;
    return vf_apple_gxf_genter(&g_m1_gxf, return_pc, spsr, enter_pc) == 0
               ? VF_M1_GXF_OK
               : VF_M1_GXF_INVALID;
}

int VF_PREOS_ABI vf_m1_guest_gxf_abort(uint64_t return_pc, uint64_t spsr,
                                       uint64_t esr, uint64_t far,
                                       uint64_t *abort_pc) {
    if (!g_m1_mmio_ready || !abort_pc) return VF_M1_GXF_UNAVAILABLE;
    return vf_apple_gxf_abort(&g_m1_gxf, return_pc, spsr, esr, far, abort_pc) == 0
               ? VF_M1_GXF_OK
               : VF_M1_GXF_INVALID;
}

int VF_PREOS_ABI vf_m1_guest_gxf_take_exception(unsigned kind, unsigned table,
                                                uint64_t return_pc, uint64_t spsr,
                                                uint64_t esr, uint64_t far,
                                                uint64_t *vector_pc) {
    if (!g_m1_mmio_ready || !vector_pc) return VF_M1_GXF_UNAVAILABLE;
    return vf_apple_gxf_take_exception(&g_m1_gxf, kind, table, return_pc, spsr,
                                       esr, far, vector_pc) == 0
               ? VF_M1_GXF_OK
               : VF_M1_GXF_INVALID;
}

int VF_PREOS_ABI vf_m1_guest_gxf_gexit(uint64_t *return_pc) {
    if (!g_m1_mmio_ready || !return_pc) return VF_M1_GXF_UNAVAILABLE;
    return vf_apple_gxf_gexit(&g_m1_gxf, return_pc) == 0
               ? VF_M1_GXF_OK
               : VF_M1_GXF_INVALID;
}

int VF_PREOS_ABI vf_m1_guest_gxf_tcg_classify(uint32_t insn) {
    return vf_apple_gxf_tcg_classify(insn);
}

int VF_PREOS_ABI vf_m1_guest_gxf_tcg_exec(uint32_t insn, unsigned el,
                                          uint64_t return_pc, uint64_t spsr,
                                          uint64_t *next_pc) {
    if (!g_m1_mmio_ready || !next_pc) return VF_M1_GXF_UNAVAILABLE;
    return vf_apple_gxf_tcg_exec(&g_m1_gxf, insn, el, return_pc, spsr, next_pc) == 0
               ? VF_M1_GXF_OK
               : VF_M1_GXF_INVALID;
}

int VF_PREOS_ABI vf_m1_guest_gxf_set_hcr_el2(uint64_t hcr_el2) {
    if (!g_m1_mmio_ready) return VF_M1_GXF_UNAVAILABLE;
    vf_apple_gxf_set_hcr_el2(&g_m1_gxf, hcr_el2);
    return VF_M1_GXF_OK;
}

int VF_PREOS_ABI vf_m1_guest_gxf_set_vmsa_lock_el1(uint64_t vmsa_lock_el1) {
    if (!g_m1_mmio_ready) return VF_M1_GXF_UNAVAILABLE;
    vf_apple_gxf_set_vmsa_lock_el1(&g_m1_gxf, vmsa_lock_el1);
    return VF_M1_GXF_OK;
}

uint64_t VF_PREOS_ABI vf_m1_guest_gxf_sprr_perm_generation(void) {
    if (!g_m1_mmio_ready) return 0;
    return vf_apple_gxf_sprr_perm_generation(&g_m1_gxf);
}

uint64_t VF_PREOS_ABI vf_m1_guest_gxf_sprr_tlb_flush_count(void) {
    if (!g_m1_mmio_ready) return 0;
    return vf_apple_gxf_sprr_tlb_flush_count(&g_m1_gxf);
}

int VF_PREOS_ABI vf_m1_guest_gxf_bind_guest_tlb(vf_cpu *cpu) {
    g_m1_gxf_tlb_cpu = cpu;
    if (!g_m1_mmio_ready) return VF_M1_GXF_UNAVAILABLE;
    vf_apple_gxf_set_sprr_tlb_flush(&g_m1_gxf, m1_gxf_sprr_guest_tlb_flush, cpu);
    return VF_M1_GXF_OK;
}

uint64_t VF_PREOS_ABI vf_m1_guest_gxf_guest_tlb_generation(void) {
    if (!g_m1_gxf_tlb_cpu) return 0;
    return g_m1_gxf_tlb_cpu->tlb_generation;
}

int VF_PREOS_ABI vf_m1_guest_mmio_read(uint32_t window, uint32_t offset,
                                       unsigned width_bits, uint64_t *value) {
    if (!value || !mmio_width_valid(width_bits)) return VF_M1_MMIO_WIDTH;
    switch (window) {
    case VF_M1_MMIO_WINDOW_AIC: return aic_dispatch_read(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_TIMER: return timer_dispatch_read(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_DART: return dart_dispatch_read(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_STORAGE: return storage_dispatch_read(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_RECOVERY: return recovery_dispatch_read(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_ADP: return adp_dispatch_read(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_UART: return uart_dispatch_read(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_SART: return sart_dispatch_read(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_NVRAM: return nvram_dispatch_read(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_SMC: return smc_dispatch_read(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_SEP: return sep_dispatch_read(offset, width_bits, value);
    default: return VF_M1_MMIO_INVALID;
    }
}

int VF_PREOS_ABI vf_m1_guest_mmio_write(uint32_t window, uint32_t offset,
                                        unsigned width_bits, uint64_t value) {
    if (!mmio_width_valid(width_bits)) return VF_M1_MMIO_WIDTH;
    switch (window) {
    case VF_M1_MMIO_WINDOW_AIC: return aic_dispatch_write(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_TIMER: return timer_dispatch_write(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_DART: return dart_dispatch_write(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_STORAGE: return storage_dispatch_write(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_RECOVERY: return recovery_dispatch_write(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_ADP: return adp_dispatch_write(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_UART: return uart_dispatch_write(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_SART: return sart_dispatch_write(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_NVRAM: return nvram_dispatch_write(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_SMC: return smc_dispatch_write(offset, width_bits, value);
    case VF_M1_MMIO_WINDOW_SEP: return sep_dispatch_write(offset, width_bits, value);
    default: return VF_M1_MMIO_INVALID;
    }
}

/*
 * Rust's Windows x64 ABI emits __chkstk before a large frame and expects RAX
 * (the requested frame size) to survive the call.  The EFI image is linked
 * without the MSVC CRT, so provide the small ABI shim here instead of pulling
 * in a general runtime.  It probes each page and restores the temporary stack
 * position before returning; the caller performs the actual `sub rsp, rax`.
 */
#if defined(__x86_64__)
__attribute__((naked, noinline)) void __chkstk(void) {
    __asm__ volatile(
        "pushq %rcx\n"
        "pushq %r10\n"
        "pushq %r11\n"
        "movq %rsp, %r11\n"
        "movq %rsp, %r10\n"
        "subq %rax, %r10\n"
        "andq $-4096, %r10\n"
        "1:\n"
        "cmpq %r10, %rsp\n"
        "jbe 2f\n"
        "subq $4096, %rsp\n"
        "movq (%rsp), %rcx\n"
        "jmp 1b\n"
        "2:\n"
        "movq %r11, %rsp\n"
        "popq %r11\n"
        "popq %r10\n"
        "popq %rcx\n"
        "retq\n");
}
#endif

#ifndef VF_M1_DISPATCH_TEST

static int zero_words(const uint64_t *words, size_t count) {
    if (!words) return 0;
    for (size_t i = 0; i < count; ++i) if (words[i]) return 0;
    return 1;
}

static int abi_prefix_valid(const void *pointer, size_t expected_size) {
    const uint32_t *words = pointer;
    return pointer && !((uintptr_t)pointer & 7) &&
           words[0] == VF_PREOS_ABI_VERSION && words[1] == expected_size;
}

static int valid_span(const void *pointer, uint64_t bytes, uint64_t minimum,
                      uint64_t maximum, uint64_t alignment) {
    uint64_t address = (uint64_t)(uintptr_t)pointer;
    if (!pointer || bytes < minimum || bytes > maximum || !alignment ||
        (address & (alignment - 1)) || address > UINT64_MAX - bytes) return 0;
    return 1;
}

static uint32_t termination_from_status(int status) {
    switch (status) {
    case VF_HALT: return VF_TERMINATION_HALT;
    case VF_BAD_INSTRUCTION: return VF_TERMINATION_BAD_INSTRUCTION;
    case VF_FETCH_FAULT: return VF_TERMINATION_FETCH_FAULT;
    case VF_DATA_FAULT: return VF_TERMINATION_DATA_FAULT;
    case VF_BUDGET: return VF_TERMINATION_BUDGET_EXHAUSTED;
    case VF_PROTECTION: return VF_TERMINATION_PROTECTION_FAILURE;
    case VF_UNDEFINED_INSTRUCTION: return VF_TERMINATION_UNDEFINED_INSTRUCTION;
    case VF_PRIVILEGE_FAULT: return VF_TERMINATION_PRIVILEGE_FAULT;
    case VF_TRANSLATION_FAULT: return VF_TERMINATION_TRANSLATION_FAULT;
    case VF_PERMISSION_FAULT: return VF_TERMINATION_PERMISSION_FAULT;
    case VF_ALIGNMENT_FAULT: return VF_TERMINATION_ALIGNMENT_FAULT;
    case VF_SP_ALIGNMENT_FAULT: return VF_TERMINATION_ALIGNMENT_FAULT;
    case VF_SYSTEM_REGISTER_TRAP: return VF_TERMINATION_SYSTEM_REGISTER_TRAP;
    case VF_TIMER_INTERRUPT: return VF_TERMINATION_TIMER_INTERRUPT;
    case VF_EXTERNAL_INTERRUPT: return VF_TERMINATION_EXTERNAL_INTERRUPT;
    case VF_FIQ_INTERRUPT: return VF_TERMINATION_FIQ_INTERRUPT;
    case VF_INSTRUCTION_ABORT: return VF_TERMINATION_INSTRUCTION_ABORT;
    case VF_DATA_ABORT: return VF_TERMINATION_DATA_ABORT;
    case VF_CODE_FULL: return VF_TERMINATION_CODE_BUFFER_FULL;
    default: return VF_TERMINATION_INTERNAL;
    }
}

static int status_is_terminal(int status) {
    /* VF_NEXT is an internal continuation status and is not a valid result
     * crossing into Rust.  Returning it means the wrapper/JIT contract was
     * violated, not that the guest halted successfully. */
    return (status >= VF_HALT && status <= VF_DATA_ABORT) ||
           status == VF_FIQ_INTERRUPT || status == VF_SP_ALIGNMENT_FAULT;
}

static int termination_is_valid(uint32_t reason) {
    return (reason >= VF_TERMINATION_HALT && reason <= VF_TERMINATION_DATA_ABORT) ||
           reason == VF_TERMINATION_FIQ_INTERRUPT;
}

static int status_and_termination_match(int status, uint32_t reason) {
    switch (status) {
    case VF_HALT: return reason == VF_TERMINATION_HALT;
    case VF_BAD_INSTRUCTION: return reason == VF_TERMINATION_BAD_INSTRUCTION;
    case VF_FETCH_FAULT: return reason == VF_TERMINATION_FETCH_FAULT;
    case VF_DATA_FAULT: return reason == VF_TERMINATION_DATA_FAULT;
    case VF_BUDGET: return reason == VF_TERMINATION_BUDGET_EXHAUSTED;
    case VF_CODE_FULL: return reason == VF_TERMINATION_CODE_BUFFER_FULL;
    case VF_PROTECTION: return reason == VF_TERMINATION_PROTECTION_FAILURE;
    case VF_UNDEFINED_INSTRUCTION: return reason == VF_TERMINATION_UNDEFINED_INSTRUCTION;
    case VF_PRIVILEGE_FAULT: return reason == VF_TERMINATION_PRIVILEGE_FAULT;
    case VF_TRANSLATION_FAULT: return reason == VF_TERMINATION_TRANSLATION_FAULT;
    case VF_PERMISSION_FAULT: return reason == VF_TERMINATION_PERMISSION_FAULT;
    case VF_ALIGNMENT_FAULT: return reason == VF_TERMINATION_ALIGNMENT_FAULT;
    case VF_SP_ALIGNMENT_FAULT: return reason == VF_TERMINATION_ALIGNMENT_FAULT;
    case VF_SYSTEM_REGISTER_TRAP: return reason == VF_TERMINATION_SYSTEM_REGISTER_TRAP;
    case VF_TIMER_INTERRUPT: return reason == VF_TERMINATION_TIMER_INTERRUPT;
    case VF_EXTERNAL_INTERRUPT: return reason == VF_TERMINATION_EXTERNAL_INTERRUPT;
    case VF_FIQ_INTERRUPT: return reason == VF_TERMINATION_FIQ_INTERRUPT;
    case VF_INSTRUCTION_ABORT: return reason == VF_TERMINATION_INSTRUCTION_ABORT;
    case VF_DATA_ABORT: return reason == VF_TERMINATION_DATA_ABORT;
    default: return 0;
    }
}

static int result_valid(const VF_JIT_RESULT *result) {
    return abi_prefix_valid(result, sizeof(*result)) &&
           result->struct_size == sizeof(*result) && !result->reserved0 &&
           zero_words(result->reserved, 3);
}

static int request_valid(const VF_JIT_REQUEST *request) {
    if (!abi_prefix_valid(request, sizeof(*request)) ||
        request->struct_size != sizeof(*request) ||
        request->machine_profile != VF_MACHINE_PROFILE_M1_DIAGNOSTIC ||
        request->flags || !zero_words(request->reserved, 3) ||
        !request->opaque_execution_handle ||
        ((uintptr_t)request->opaque_execution_handle & 7) || !request->execution_budget ||
        request->execution_budget > VF_PREOS_MAX_EXECUTION_BUDGET ||
        !valid_span(request->guest_bytes, request->guest_size, 4,
                    VF_PREOS_MAX_GUEST_BYTES, 4) || (request->guest_size & 3) ||
        !valid_span(request->guest_ram, request->guest_ram_size,
                    VF_PREOS_FIXED_GUEST_RAM_BYTES,
                    VF_PREOS_FIXED_GUEST_RAM_BYTES, 8)) return 0;
    return 1;
}

int VF_PREOS_ABI vf_preos_jit_execute(const VF_JIT_REQUEST *request, VF_JIT_RESULT *result) {
    vf_efi_execution *execution;
    int status;
    if (!result_valid(result)) return VF_PREOS_E_ABI;
    if (!request_valid(request)) {
        result->jit_status = VF_DATA_FAULT;
        result->termination_reason = VF_TERMINATION_WRAPPER_REJECTED;
        return VF_PREOS_E_CONTEXT;
    }
    execution = request->opaque_execution_handle;
    if (execution->magic != VF_EFI_EXECUTION_MAGIC || !execution->code ||
        !execution->protect || !execution->cpu || execution->reserved ||
        execution->machine_profile != request->machine_profile ||
        execution->guest_bytes != request->guest_bytes ||
        execution->guest_size != request->guest_size ||
        execution->guest_ram != request->guest_ram ||
        execution->guest_ram_size != request->guest_ram_size) {
        result->jit_status = VF_DATA_FAULT;
        result->termination_reason = VF_TERMINATION_WRAPPER_REJECTED;
        return VF_PREOS_E_CONTEXT;
    }
    vf_cpu_reset(execution->cpu, VF_EL0);
    execution->cpu->x[2] = execution->initial_x2;
    status = vf_run(execution->cpu, request->guest_bytes, (size_t)request->guest_size,
                    request->guest_ram, (size_t)request->guest_ram_size,
                    execution->code, request->execution_budget,
                    execution->protect, execution->protection_opaque);
    result->jit_status = status;
    result->termination_reason = termination_from_status(status);
    if (!status_is_terminal(status) ||
        !termination_is_valid(result->termination_reason) ||
        !status_and_termination_match(status, result->termination_reason)) {
        result->jit_status = VF_DATA_FAULT;
        result->termination_reason = VF_TERMINATION_WRAPPER_REJECTED;
        return VF_PREOS_E_INTERNAL;
    }
    result->retired_instruction_count = execution->cpu->retired;
    result->guest_pc = execution->cpu->pc;
    result->fault_instruction = execution->cpu->instruction;
    result->result_x0 = execution->cpu->x[0];
    result->result_x1 = execution->cpu->x[1];
    result->result_x3 = execution->cpu->x[3];
    return VF_PREOS_OK;
}

#endif /* VF_M1_DISPATCH_TEST */

/* A Rust panic is a programming defect, not an EFI return path.  The runtime
 * has no input-triggered panic paths; this noreturn trap prevents unwinding
 * across the C/EFI boundary if an invariant is nevertheless violated. */
void VF_PREOS_ABI vf_preos_abort(void) {
#if defined(_WIN32)
    for (;;) __asm__ volatile("hlt");
#else
    __builtin_trap();
#endif
}
