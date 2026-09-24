/* SPDX-License-Identifier: BSD-4-Clause; C-owned bridge behind preos_abi.h. */
#ifndef VENFIRE_PREOS_BRIDGE_H
#define VENFIRE_PREOS_BRIDGE_H

#include "jit.h"
#include "preos_abi.h"

#define VF_EFI_EXECUTION_MAGIC UINT64_C(0x5646455845433031) /* "VFEXEC01" */

/* This structure never crosses into Rust by value.  Rust sees it only as an
 * opaque pointer and C validates it before touching the internal JIT state. */
typedef struct {
    uint64_t magic;
    vf_code *code;
    vf_protect protect;
    void *protection_opaque;
    const uint8_t *guest_bytes;
    uint64_t guest_size;
    uint8_t *guest_ram;
    uint64_t guest_ram_size;
    vf_cpu *cpu;
    uint64_t initial_x2;
    uint32_t machine_profile;
    uint32_t reserved;
} vf_efi_execution;

/* Graph-local M1 guest MMIO windows routed through sandbox device v1 stubs.
 * Window ids are logical_base >> 12; they are not Apple physical MMIO bases. */
#define VF_M1_LOGICAL_WINDOW_BYTES UINT32_C(0x1000)
#define VF_M1_MMIO_WINDOW_AIC      UINT32_C(0x1)
#define VF_M1_MMIO_WINDOW_TIMER    UINT32_C(0x2) /* M1_LOGICAL_TIMER_BASE >> 12 */
#define VF_M1_MMIO_WINDOW_DART     UINT32_C(0x3)
#define VF_M1_MMIO_WINDOW_STORAGE  UINT32_C(0x4) /* M1_LOGICAL_STORAGE_BASE >> 12 */
#define VF_M1_MMIO_WINDOW_RECOVERY UINT32_C(0x5) /* M1_LOGICAL_RECOVERY_BASE >> 12 */
#define VF_M1_MMIO_WINDOW_ADP      UINT32_C(0x6) /* M1_LOGICAL_DISPLAY_BASE >> 12 */
#define VF_M1_MMIO_WINDOW_UART     UINT32_C(0x7)
#define VF_M1_MMIO_WINDOW_SART     UINT32_C(0x8)
#define VF_M1_MMIO_WINDOW_NVRAM    UINT32_C(0x9)
#define VF_M1_MMIO_WINDOW_SMC      UINT32_C(0xa)
#define VF_M1_MMIO_WINDOW_SEP      UINT32_C(0xb)

/* Graph-local SMC mailbox shim (not Apple mbox MMIO). */
#define VF_M1_SMC_MMIO_MSG_OUT     0x000u
#define VF_M1_SMC_MMIO_KEY_COUNT   0x008u
#define VF_M1_SMC_MMIO_SRAM_ADDR   0x010u
#define VF_M1_SMC_MMIO_MSG_IN      0x018u

/* Bounded T8103 graph contract; not an Apple DT irq/cpu claim. */
#define VF_M1_AIC_IRQ_COUNT        UINT32_C(896)
#define VF_M1_AIC_CPU_COUNT        UINT32_C(8)
/* Graph-local timer pending line — matches Rust TIMER_SOURCE (0).
 * Sources 0–3: timer/storage/display/recovery. */
#define VF_M1_TIMER_IRQ_LINE       UINT32_C(0)
/* Graph-local storage attach pending line — matches Rust STORAGE_SOURCE (1). */
#define VF_M1_STORAGE_IRQ_LINE     UINT32_C(1)
/* Graph-local display/vblank line — matches Rust DISPLAY_SOURCE (2). */
#define VF_M1_ADP_IRQ_LINE         UINT32_C(2)
/* Graph-local recovery envelope ready line — matches Rust RECOVERY_SOURCE (3). */
#define VF_M1_RECOVERY_IRQ_LINE    UINT32_C(3)
/* Graph-local DART fault line (sources 0–7 overlay). */
#define VF_M1_DART_IRQ_LINE        UINT32_C(4)
/* Graph-local UART RX/TX pending line (within sources 0–7 overlay).
 * Public Asahi t8103.dtsi serial0 uses AIC_IRQ 605 — reference only; not
 * claimed as a physical SoC binding for this stub. */
#define VF_M1_UART_IRQ_LINE        UINT32_C(5)
/* Graph-local SEP L1 mailbox reply-pending line (within sources 0–7 overlay).
 * Inferno DT SEP interrupts use large AIC line numbers — reference only; not
 * claimed as a physical SoC binding for this L1 stub. */
#define VF_M1_SEP_IRQ_LINE         UINT32_C(6)

/* Boot-seeded SMC key table size (read-only probe; no apple_smc_v1 in EFI image). */
#define VF_SMC_BOOT_KEY_COUNT      UINT32_C(6)

#define VF_M1_MMIO_OK              0
#define VF_M1_MMIO_INVALID         1
#define VF_M1_MMIO_WIDTH           2
#define VF_M1_MMIO_READONLY        3
#define VF_M1_MMIO_UNAVAILABLE     4

void VF_PREOS_ABI vf_m1_guest_mmio_reset(void);
int VF_PREOS_ABI vf_m1_guest_mmio_read(uint32_t window, uint32_t offset,
                                       unsigned width_bits, uint64_t *value);
int VF_PREOS_ABI vf_m1_guest_mmio_write(uint32_t window, uint32_t offset,
                                        unsigned width_bits, uint64_t value);
/* Mirror graph-local interrupt source N into aic_v1 level line N (sources 0–7 today). */
int VF_PREOS_ABI vf_m1_guest_aic_set_line(unsigned irq, int high);
/* Latch DART ERROR_STATUS fault and assert VF_M1_DART_IRQ_LINE on AIC. */
int VF_PREOS_ABI vf_m1_guest_dart_simulate_fault(unsigned sid, uint32_t code);
/* Host/test-visible TLB invalidate generation from the bridge DART stub. */
uint32_t VF_PREOS_ABI vf_m1_guest_dart_tlb_generation(void);
/* Host/test-visible SART region-table generation (IOMMU notifier stand-in). */
uint32_t VF_PREOS_ABI vf_m1_guest_sart_generation(void);
/* Identity SART translate against the bridge region table; see vf_sart_translate. */
int VF_PREOS_ABI vf_m1_guest_sart_translate(uint64_t iova, uint64_t *pa_out,
                                            unsigned *region_out);
/* Push one RX byte into the UART fifo and sync VF_M1_UART_IRQ_LINE. */
int VF_PREOS_ABI vf_m1_guest_uart_push_rx(uint8_t byte);
/* Advance graph-local timer counter and sync VF_M1_TIMER_IRQ_LINE. */
int VF_PREOS_ABI vf_m1_guest_timer_advance(uint64_t ticks);
/* Attach graph-local storage metadata and sync VF_M1_STORAGE_IRQ_LINE. */
int VF_PREOS_ABI vf_m1_guest_storage_attach(uint64_t block_count, int read_only);
/* Detach graph-local storage and deassert VF_M1_STORAGE_IRQ_LINE. */
int VF_PREOS_ABI vf_m1_guest_storage_detach(void);
/*
 * Bridge ans_pci_v1 auto-wires STORAGE AIC line 1 on mmio_reset via
 * vf_ans_pci_bind_aic (INTx|MSI delivery_pending mirror). Re-bind after
 * unbind; fail-closed if MMIO/AIC not ready. ≠ window 0x4 / Apple DT IRQ /
 * guest acceptance.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_bind_aic(void);
/* Unbind ans_pci AIC wire (fail-closed; pending may latch without sync). */
int VF_PREOS_ABI vf_m1_guest_ans_pci_unbind_aic(void);
/* Host/test: assert/deassert ans_pci INTx/MSI pending → bound AIC mirror. */
int VF_PREOS_ABI vf_m1_guest_ans_pci_set_irq(int level);
int VF_PREOS_ABI vf_m1_guest_ans_pci_delivery_pending(void);
int VF_PREOS_ABI vf_m1_guest_ans_pci_aic_sync_seen(void);
int VF_PREOS_ABI vf_m1_guest_ans_pci_aic_line_high(void);
/*
 * Bridge ans_pci auto-binds a research/test MSI GPA window on mmio_reset via
 * vf_ans_pci_bind_msi_gpa (base 0xfee00000, 16-byte host stand-in). Re-bind
 * after unbind; fail-closed if MMIO not ready. ≠ live guest RAM / MSI-X /
 * Apple DT / guest acceptance.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_bind_msi_gpa(void);
/* Unbind MSI GPA wire (fail-closed; MSI pending may latch without write). */
int VF_PREOS_ABI vf_m1_guest_ans_pci_unbind_msi_gpa(void);
/* 1 bound / 0 unbound / -1 if MMIO not ready. */
int VF_PREOS_ABI vf_m1_guest_ans_pci_msi_gpa_bound(void);
/*
 * Host/test: MSI Enable + prepare Message Address/Data. addr==0 uses
 * research GPA base. enable==0 clears MSI Enable. Fail-closed if MMIO not
 * ready.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_prepare_msi(int enable, uint64_t addr,
                                                 uint16_t data);
int VF_PREOS_ABI vf_m1_guest_ans_pci_msi_message_written(void);
/* Peek research MSI GPA stand-in byte; -1 if MMIO not ready or off OOB. */
int VF_PREOS_ABI vf_m1_guest_ans_pci_msi_gpa_byte(uint32_t off);
/*
 * Host/test: MSI-X Enable + Table BIR prepare on bridge ans_pci. enable==0
 * clears MSI-X Enable (table_bir ignored). Fail-closed if MMIO not ready.
 * ≠ PBA/table MMIO / MSI-X message write / Apple DT.
 */
int VF_PREOS_ABI vf_m1_guest_ans_pci_prepare_msix(int enable,
                                                   uint8_t table_bir);
/* 1 when msix_pending latched on bridge ans_pci; 0 otherwise / MMIO not ready. */
int VF_PREOS_ABI vf_m1_guest_ans_pci_msix_pending(void);
/*
 * Bridge ans_v1 auto-binds to bridge ans_pci on mmio_reset so Identify CQ
 * irq_check drives PCI delivery_pending → AIC STORAGE (when AIC bound).
 * Re-bind after unbind; fail-closed if MMIO not ready. ≠ Apple DT / guest
 * acceptance. MSI-X Enable routes pending via prepare_msix (no table/PBA).
 */
int VF_PREOS_ABI vf_m1_guest_ans_bind_pci(void);
/* Unbind ans→pci (fail-closed; irq_check may latch without PCI/AIC sync). */
int VF_PREOS_ABI vf_m1_guest_ans_unbind_pci(void);
/* Host/test: BusMaster + CC.EN for Identify DMA/CQ path. */
int VF_PREOS_ABI vf_m1_guest_ans_enable_identify_path(void);
/* Host/test: arm Identify CNS=CTRL + admin SQ doorbell one-shot. */
int VF_PREOS_ABI vf_m1_guest_ans_submit_admin_identify(void);
int VF_PREOS_ABI vf_m1_guest_ans_irq_check(void);
/* Host/test: INTMS (mask!=0) or INTMC (mask==0) on admin CQ bit0. */
int VF_PREOS_ABI vf_m1_guest_ans_mask_admin_irq(int mask);
/* Host/test: CQHDBL drain to current admin CQ Tail. */
int VF_PREOS_ABI vf_m1_guest_ans_drain_admin_cq(void);
/* Ingest graph-local recovery envelope and sync VF_M1_RECOVERY_IRQ_LINE. */
int VF_PREOS_ABI vf_m1_guest_recovery_ingest(const uint8_t *frame, size_t frame_len);
/* Clear recovery ready bit and deassert VF_M1_RECOVERY_IRQ_LINE. */
int VF_PREOS_ABI vf_m1_guest_recovery_clear(void);
/*
 * Ingest a caller-owned on-disk NVRAM bank image into the graph-local stub
 * (CHRP + adler32 layout). Synthetic/fixture banks only unless the operator
 * supplies a private bank outside git.
 */
int VF_PREOS_ABI vf_m1_guest_nvram_ingest_bank(const uint8_t *buf, size_t len);
/*
 * Load graph-local NVRAM from a clean-room NVMe-ns blk backend
 * (getlength + pread @0, cap 0x2000). Fail-closed if blk absent. Not macOS
 * UART proof.
 */
typedef struct vf_nvram_blk_backend vf_nvram_blk_backend;
int VF_PREOS_ABI vf_m1_guest_nvram_load_from_blk(const vf_nvram_blk_backend *blk);
/*
 * Attach a research NVRAM bank image path (BlockBackend-style open), load into
 * graph-local stub, and keep the backend for save. Fail-closed if path missing
 * or open/load fails. Host/test only — EFI builds return -1.
 */
int VF_PREOS_ABI vf_m1_guest_nvram_attach_research_image(const char *path);
/* Detach research image backend (closes fd). Idempotent. */
int VF_PREOS_ABI vf_m1_guest_nvram_detach_blk(void);
/*
 * Serialize graph-local NVRAM and pwrite to the attached research image.
 * Fail-closed if nothing attached or pwrite fails.
 */
int VF_PREOS_ABI vf_m1_guest_nvram_save_to_attached(void);
/*
 * Copy ingested/committed boot-args into out. Returns length excluding NUL,
 * 0 if unset, or -1. Host/test handoff surface only — not macOS UART proof.
 */
int VF_PREOS_ABI vf_m1_guest_nvram_boot_args_handoff(char *out, size_t out_len);
/* Configure ADP L1 framebuffer mode (graph-local; XRGB8888 stride bounds). */
int VF_PREOS_ABI vf_m1_guest_adp_configure(uint64_t guest_address,
                                           uint64_t backing_bytes,
                                           uint32_t width, uint32_t height,
                                           uint32_t stride);

/* Apple A13 GXF/SPRR AArch64 sysreg bank (Golden Gate rank 5 boundary). */
#define VF_M1_GXF_OK          0
#define VF_M1_GXF_INVALID     1
#define VF_M1_GXF_UNAVAILABLE 2

int VF_PREOS_ABI vf_m1_guest_gxf_read(uint32_t reg, uint64_t *value);
int VF_PREOS_ABI vf_m1_guest_gxf_write(uint32_t reg, uint64_t value);
/* el==0 applies MPRR-masked SPRR_EL0BR0 merge; el>=1 is unmasked (EL1 default). */
int VF_PREOS_ABI vf_m1_guest_gxf_write_el(uint32_t reg, uint64_t value, unsigned el);
/* Clean-room GENTER/GEXIT/ABORT via apple_gxf_v1 (no live QEMU translate / PAC). */
int VF_PREOS_ABI vf_m1_guest_gxf_genter(uint64_t return_pc, uint64_t spsr,
                                        uint64_t *enter_pc);
int VF_PREOS_ABI vf_m1_guest_gxf_abort(uint64_t return_pc, uint64_t spsr,
                                       uint64_t esr, uint64_t far,
                                       uint64_t *abort_pc);
/* VBAR_GL/EL1 exception offset vectoring (no TCG; not GXF_ABORT_EL1). */
int VF_PREOS_ABI vf_m1_guest_gxf_take_exception(unsigned kind, unsigned table,
                                                uint64_t return_pc, uint64_t spsr,
                                                uint64_t esr, uint64_t far,
                                                uint64_t *vector_pc);
int VF_PREOS_ABI vf_m1_guest_gxf_gexit(uint64_t *return_pc);
/* Clean-room TCG opcode stub → genter/gexit (not a live QEMU translate hook). */
int VF_PREOS_ABI vf_m1_guest_gxf_tcg_classify(uint32_t insn);
int VF_PREOS_ABI vf_m1_guest_gxf_tcg_exec(uint32_t insn, unsigned el,
                                          uint64_t return_pc, uint64_t spsr,
                                          uint64_t *next_pc);
/* Host inject of HCR_EL2 shadow for ESR_EL1/FAR_EL1 TVM/TRVM gates. */
int VF_PREOS_ABI vf_m1_guest_gxf_set_hcr_el2(uint64_t hcr_el2);
/* Host inject of VMSA_LOCK_EL1 (direct assign; guest write is sticky-OR). */
int VF_PREOS_ABI vf_m1_guest_gxf_set_vmsa_lock_el1(uint64_t vmsa_lock_el1);
/* Host/test-visible SPRR perm generation + flush counter (callback may TLB). */
uint64_t VF_PREOS_ABI vf_m1_guest_gxf_sprr_perm_generation(void);
uint64_t VF_PREOS_ABI vf_m1_guest_gxf_sprr_tlb_flush_count(void);
/* Bind optional guest JIT CPU so EL0 SPRR merges call vf_cpu_invalidate_tlb.
 * NULL clears bind (fail-closed: count still bumps, no CPU TLB touch).
 * Not a QEMU SoftMMU TLB. */
int VF_PREOS_ABI vf_m1_guest_gxf_bind_guest_tlb(vf_cpu *cpu);
/* Bound CPU tlb_generation, or 0 when unbound. */
uint64_t VF_PREOS_ABI vf_m1_guest_gxf_guest_tlb_generation(void);

#endif
