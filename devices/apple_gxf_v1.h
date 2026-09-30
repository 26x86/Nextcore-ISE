/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_APPLE_GXF_V1_H
#define VENFIRE_APPLE_GXF_V1_H
#include <stdint.h>

/* Apple A13 GXF/SPRR AArch64 sysregs (T8030 reference). See GXF.md. */
#define VF_GXF_SYSREG(op0, op1, crn, crm, op2) \
    ((uint32_t)(((op0) << 16) | ((op1) << 12) | ((crn) << 8) | ((crm) << 4) | (op2)))

#define VF_GXF_REG_GXF_CONFIG_EL1  VF_GXF_SYSREG(3, 6, 15, 1, 2)
#define VF_GXF_REG_GXF_STATUS_EL1  VF_GXF_SYSREG(3, 6, 15, 8, 0)
#define VF_GXF_REG_GXF_ENTER_EL1   VF_GXF_SYSREG(3, 6, 15, 8, 1)
#define VF_GXF_REG_GXF_ABORT_EL1   VF_GXF_SYSREG(3, 6, 15, 8, 2)
#define VF_GXF_REG_ASPSR_GL11      VF_GXF_SYSREG(3, 6, 15, 8, 3)
#define VF_GXF_REG_SP_GL11         VF_GXF_SYSREG(3, 6, 15, 9, 0)
#define VF_GXF_REG_TPIDR_GL11      VF_GXF_SYSREG(3, 6, 15, 9, 1)
#define VF_GXF_REG_VBAR_GL11       VF_GXF_SYSREG(3, 6, 15, 9, 2)
#define VF_GXF_REG_SPSR_GL11       VF_GXF_SYSREG(3, 6, 15, 9, 3)
#define VF_GXF_REG_ESR_GL11        VF_GXF_SYSREG(3, 6, 15, 9, 5)
#define VF_GXF_REG_ELR_GL11        VF_GXF_SYSREG(3, 6, 15, 9, 6)
#define VF_GXF_REG_FAR_GL11        VF_GXF_SYSREG(3, 6, 15, 9, 7)
/* Architectural EL1 encodings overridden to dual normal/guarded view. */
#define VF_GXF_REG_TPIDR_EL1       VF_GXF_SYSREG(3, 0, 13, 0, 4)
#define VF_GXF_REG_VBAR_EL1        VF_GXF_SYSREG(3, 0, 12, 0, 0)
#define VF_GXF_REG_SPSR_EL1        VF_GXF_SYSREG(3, 0, 4, 0, 0)
#define VF_GXF_REG_ELR_EL1         VF_GXF_SYSREG(3, 0, 4, 0, 1)
#define VF_GXF_REG_ESR_EL1         VF_GXF_SYSREG(3, 0, 5, 2, 0)
#define VF_GXF_REG_FAR_EL1         VF_GXF_SYSREG(3, 0, 6, 0, 0)
/* Architectural MMU controls with VMSA_LOCK_EL1 consumers (single bank). */
#define VF_GXF_REG_SCTLR_EL1       VF_GXF_SYSREG(3, 0, 1, 0, 0)
#define VF_GXF_REG_TTBR0_EL1       VF_GXF_SYSREG(3, 0, 2, 0, 0)
#define VF_GXF_REG_TTBR1_EL1       VF_GXF_SYSREG(3, 0, 2, 0, 1)
#define VF_GXF_REG_TCR_EL1         VF_GXF_SYSREG(3, 0, 2, 0, 2)
/* Architectural HCR_EL2 (EL2+ MSR); live write couples into TVM/TRVM shadow. */
#define VF_GXF_REG_HCR_EL2         VF_GXF_SYSREG(3, 4, 1, 1, 0)
/* Apple VMSA_LOCK_EL1 (T8030 helper.c); sticky-OR write; bit0 locks VBAR. */
#define VF_GXF_REG_VMSA_LOCK_EL1   VF_GXF_SYSREG(3, 4, 15, 1, 2)
#define VF_GXF_REG_SPRR_CONFIG_EL1 VF_GXF_SYSREG(3, 6, 15, 1, 0)
#define VF_GXF_REG_SPRR_CONFIG_EL0 VF_GXF_SYSREG(3, 6, 15, 1, 1)
#define VF_GXF_REG_SPRR_EL0BR0_EL1 VF_GXF_SYSREG(3, 6, 15, 1, 5)
#define VF_GXF_REG_SPRR_EL0BR1_EL1 VF_GXF_SYSREG(3, 6, 15, 1, 6)
#define VF_GXF_REG_SPRR_EL1BR0_EL1 VF_GXF_SYSREG(3, 6, 15, 1, 7)
#define VF_GXF_REG_SPRR_EL1BR1_EL1 VF_GXF_SYSREG(3, 6, 15, 3, 0)
#define VF_GXF_REG_MPRR_EL0BR0_EL1 VF_GXF_SYSREG(3, 6, 15, 3, 1)
#define VF_GXF_REG_MPRR_EL0BR1_EL1 VF_GXF_SYSREG(3, 6, 15, 3, 2)
#define VF_GXF_REG_MPRR_EL1BR0_EL1 VF_GXF_SYSREG(3, 6, 15, 3, 3)
#define VF_GXF_REG_MPRR_EL1BR1_EL1 VF_GXF_SYSREG(3, 6, 15, 3, 4)

/* GXF_CONFIG_EL1 bit 0 enables guarded enter (public Asahi / m1n1 layout). */
#define VF_GXF_CONFIG_EN         UINT64_C(1)
/* GXF_STATUS_EL1 bit 0 mirrors arm_is_guarded (status & 1). */
#define VF_GXF_STATUS_GUARDED    UINT64_C(1)
/* ASPSR_GL11 bit 0: GEXIT returns to GL (stay guarded) vs EL (clear guarded).
 * Public Asahi/Sven: ASPSR indicates whether gexit returns to GL or EL. */
#define VF_GXF_ASPSR_NEST        UINT64_C(1)
/* HCR_EL2 shadow bits (ARM architectural positions).
 * Updated by EL2+ MSR of VF_GXF_REG_HCR_EL2 or host vf_apple_gxf_set_hcr_el2.
 * Only TVM/TRVM are consumed by ESR_EL1/FAR_EL1 alias gates. */
#define VF_GXF_HCR_TVM           (UINT64_C(1) << 26)
#define VF_GXF_HCR_TRVM          (UINT64_C(1) << 30)
/* VMSA_LOCK_EL1 bits (T8030 cpu.h). Unguarded EL1 write consumers below. */
#define VF_GXF_VMSA_LOCK_VBAR_EL1    (UINT64_C(1) << 0)
#define VF_GXF_VMSA_LOCK_SCTLR_EL1   (UINT64_C(1) << 1)
#define VF_GXF_VMSA_LOCK_TCR_EL1     (UINT64_C(1) << 2)
#define VF_GXF_VMSA_LOCK_TTBR0_EL1   (UINT64_C(1) << 3)
#define VF_GXF_VMSA_LOCK_TTBR1_EL1   (UINT64_C(1) << 4)
/* When set (and full SCTLR lock clear): preserve SCTLR.M on unguarded write. */
#define VF_GXF_VMSA_LOCK_SCTLR_M_BIT (UINT64_C(1) << 63)
#define VF_GXF_SCTLR_M               (UINT64_C(1) << 0)

/* AArch64 VBAR exception-class offsets within a table (ARM DDI / qemu-t8030). */
#define VF_GXF_EXCP_SYNC             0u /* +0x000 */
#define VF_GXF_EXCP_IRQ              1u /* +0x080 */
#define VF_GXF_EXCP_FIQ              2u /* +0x100 */
#define VF_GXF_EXCP_SERR             3u /* +0x180 */
/* VBAR table select: same-EL SP0 / same-EL SPx / lower AArch64 / lower AArch32. */
#define VF_GXF_VBAR_TABLE_CURRENT_SP0 0u /* +0x000 */
#define VF_GXF_VBAR_TABLE_CURRENT_SPX 1u /* +0x200 */
#define VF_GXF_VBAR_TABLE_LOWER_A64   2u /* +0x400 */
#define VF_GXF_VBAR_TABLE_LOWER_A32   3u /* +0x600 */
#define VF_GXF_VBAR_KIND_OFFSET(kind)  ((uint64_t)(kind) * UINT64_C(0x80))
#define VF_GXF_VBAR_TABLE_OFFSET(tbl)  ((uint64_t)(tbl) * UINT64_C(0x200))

/* qemu-t8030 disas_apple_insn GENTER/GEXIT encodings (bit31=0, bits[28:25]=0,
 * opcode bits[15:10]=5). Reference-only; not a live QEMU translate hook. */
#define VF_GXF_TCG_INSN_GENTER       0x00201420u /* rn=1, rd=0 */
#define VF_GXF_TCG_INSN_GEXIT        0x00201400u /* rn=0, rd=0 */
#define VF_GXF_TCG_APPLE_OP_GXF      5u
#define VF_GXF_TCG_RN_GEXIT          0u
#define VF_GXF_TCG_RN_GENTER         1u
/* Classify result for vf_apple_gxf_tcg_classify (not ABI status codes). */
#define VF_GXF_TCG_KIND_NONE         0
#define VF_GXF_TCG_KIND_GENTER       1
#define VF_GXF_TCG_KIND_GEXIT        2

/* Optional SPRR permission-change TLB flush hook (generation after bump).
 * Not a guest MMU/TLB; host/TCG may wire a real invalidate later. */
typedef void (*vf_gxf_sprr_tlb_flush_fn)(void *ctx, uint64_t perm_generation);

typedef struct {
    int guarded;
    uint64_t gxf_config_el1;
    uint64_t gxf_status_el1;
    uint64_t gxf_enter_el1;
    uint64_t gxf_abort_el1;
    uint64_t aspsr_gl11;
    uint64_t sp_gl11;
    uint64_t tpidr_gl11;
    uint64_t vbar_gl11;
    uint64_t spsr_gl11;
    uint64_t esr_gl11;
    uint64_t elr_gl11;
    uint64_t far_gl11;
    /* Normal (unguarded) EL1 bank shadowed by architectural alias overrides. */
    uint64_t tpidr_el1;
    uint64_t vbar_el1;
    uint64_t spsr_el1;
    uint64_t elr_el1;
    uint64_t esr_el1;
    uint64_t far_el1;
    /* HCR_EL2 shadow for TVM/TRVM (EL2+ MSR or host inject). */
    uint64_t hcr_el2;
    /* VMSA_LOCK_EL1 shadow; guest writes sticky-OR; VBAR/SCTLR/TCR/TTBR bits. */
    uint64_t vmsa_lock_el1;
    /* Single-bank MMU controls (not dual-view); lock applies when unguarded EL1. */
    uint64_t sctlr_el1;
    uint64_t tcr_el1;
    uint64_t ttbr0_el1;
    uint64_t ttbr1_el1;
    uint64_t sprr_config_el1;
    uint64_t sprr_config_el0;
    uint64_t sprr_el0br0_el1;
    uint64_t sprr_el0br1_el1;
    uint64_t sprr_el1br0_el1;
    uint64_t sprr_el1br1_el1;
    uint64_t mprr_el0br0_el1;
    uint64_t mprr_el0br1_el1;
    uint64_t mprr_el1br0_el1;
    uint64_t mprr_el1br1_el1;
    /* Monotonic counter bumped on EL0-masked SPRR_EL0BR0 merges; not a TLB. */
    uint64_t sprr_perm_generation;
    /* Stub flush counter bumped with sprr_perm_generation; not a guest TLB. */
    uint64_t sprr_tlb_flush_count;
    /* Optional host/TCG hook on SPRR perm change (NULL = count-only stub). */
    vf_gxf_sprr_tlb_flush_fn sprr_tlb_flush;
    void *sprr_tlb_flush_ctx;
    /* Monotonic counter bumped on GENTER/GEXIT mode transitions; not a TLB. */
    uint64_t gxf_mode_generation;
} vf_apple_gxf_v1;

int vf_apple_gxf_init(vf_apple_gxf_v1 *);
void vf_apple_gxf_set_guarded(vf_apple_gxf_v1 *, int guarded);
int vf_apple_gxf_is_guarded(const vf_apple_gxf_v1 *);
/* Host inject of HCR_EL2 shadow (same store as EL2+ MSR of VF_GXF_REG_HCR_EL2). */
void vf_apple_gxf_set_hcr_el2(vf_apple_gxf_v1 *, uint64_t hcr_el2);
uint64_t vf_apple_gxf_get_hcr_el2(const vf_apple_gxf_v1 *);
/* Host inject of VMSA_LOCK_EL1 (direct assign; guest write is sticky-OR). */
void vf_apple_gxf_set_vmsa_lock_el1(vf_apple_gxf_v1 *, uint64_t vmsa_lock_el1);
uint64_t vf_apple_gxf_get_vmsa_lock_el1(const vf_apple_gxf_v1 *);
/* Install/clear optional SPRR TLB flush callback (NULL clears). */
void vf_apple_gxf_set_sprr_tlb_flush(vf_apple_gxf_v1 *,
                                    vf_gxf_sprr_tlb_flush_fn fn, void *ctx);
uint64_t vf_apple_gxf_sprr_perm_generation(const vf_apple_gxf_v1 *);
uint64_t vf_apple_gxf_sprr_tlb_flush_count(const vf_apple_gxf_v1 *);
/* Unknown or trapped registers fail closed with -1.
 * vf_apple_gxf_write assumes EL1 (full SPRR_EL0BR0 write).
 * vf_apple_gxf_read assumes EL1 (HCR_EL2 MRS fails closed; use get_hcr_el2). */
int vf_apple_gxf_read(vf_apple_gxf_v1 *, uint32_t reg, uint64_t *value);
int vf_apple_gxf_write(vf_apple_gxf_v1 *, uint32_t reg, uint64_t value);
/* el==0 applies MPRR_EL0BR0 allow-mask merge to SPRR_EL0BR0; el>=1 is unmasked.
 * EL0 SPRR_EL0BR0 merge bumps sprr_perm_generation + sprr_tlb_flush_count and
 * invokes the optional flush hook (bridge may wire → vf_cpu_invalidate_tlb).
 * el==1 applies TVM/TRVM gates to ESR_EL1/FAR_EL1 and VMSA locks;
 * el>=2 bypasses those gates and may MSR HCR_EL2 into the TVM/TRVM shadow. */
int vf_apple_gxf_write_el(vf_apple_gxf_v1 *, uint32_t reg, uint64_t value,
                          unsigned el);
/* Clean-room GENTER/GEXIT/ABORT (host helpers + optional TCG opcode stub).
 * GENTER: requires CONFIG_EN; fails if already guarded; saves return_pc/spsr into
 * ELR_GL11/SPSR_GL11; clears ASPSR_NEST; sets STATUS_GUARDED; *enter_pc=ENTER.
 * GXF_ABORT: requires CONFIG_EN; saves ELR/SPSR/ESR/FAR; *abort_pc=GXF_ABORT_EL1.
 *   EL→GL (unguarded): clear ASPSR_NEST; set STATUS_GUARDED.
 *   GL→GL nest (already guarded): set ASPSR_NEST (hardware nest); stay guarded.
 * GEXIT: fails if not guarded; *return_pc=ELR_GL11. If ASPSR_NEST set: clear
 * nest and stay guarded (return to GL); else clear STATUS_GUARDED (return to EL).
 *
 * Architectural EL1 encodings (TPIDR/VBAR/SPSR/ELR/ESR/FAR_EL1) dual-route:
 * guarded → *_GL11 bank; unguarded → normal *_el1 bank. VBAR keeps 32-byte
 * align. EL1 ESR_EL1/FAR_EL1 honor hcr_el2 TVM (write) / TRVM (read).
 * Live HCR_EL2: EL2+ write_el(VF_GXF_REG_HCR_EL2) or set_hcr_el2 updates shadow;
 * EL1 MSR/MRS of HCR_EL2 fails closed. Unguarded EL1 VMSA locks (silent no-op
 * unless noted): VBAR bit0; SCTLR bit1; TCR bit2; TTBR0 bit3; TTBR1 bit4.
 * SCTLR bit63 preserves SCTLR.M when full SCTLR lock clear. Guarded / el>=2
 * bypass VMSA locks; VBAR_GL11 unlocked.
 *
 * VBAR exception vectoring (vf_apple_gxf_take_exception): when guarded, base is
 * VBAR_GL11; else VBAR_EL1. vector = base + table_offset + kind_offset. Saves
 * ELR/SPSR always; ESR for SYNC/SERR; FAR for SYNC. Does not enter/leave
 * guarded mode and does not use GXF_ABORT_EL1 (that remains vf_apple_gxf_abort).
 *
 * TCG opcode stub (vf_apple_gxf_tcg_*): clean-room classify/exec patterned on
 * qemu-t8030 disas_apple_insn GENTER/GEXIT. Does not patch QEMU translate-a64;
 * live VMApple TCG decode remains a VenFire scaffold / OPEN_QUESTION. */
int vf_apple_gxf_genter(vf_apple_gxf_v1 *, uint64_t return_pc, uint64_t spsr,
                        uint64_t *enter_pc);
int vf_apple_gxf_abort(vf_apple_gxf_v1 *, uint64_t return_pc, uint64_t spsr,
                       uint64_t esr, uint64_t far, uint64_t *abort_pc);
int vf_apple_gxf_gexit(vf_apple_gxf_v1 *, uint64_t *return_pc);
int vf_apple_gxf_take_exception(vf_apple_gxf_v1 *, unsigned kind, unsigned table,
                                uint64_t return_pc, uint64_t spsr, uint64_t esr,
                                uint64_t far, uint64_t *vector_pc);
/* Returns VF_GXF_TCG_KIND_* for recognized GENTER/GEXIT encodings; else NONE. */
int vf_apple_gxf_tcg_classify(uint32_t insn);
/* EL0 / unrecognized → -1. Recognized GENTER/GEXIT → vf_apple_gxf_genter/gexit.
 * *next_pc is ENTER vector (GENTER) or ELR_GL11 (GEXIT). No MMU/exception inject. */
int vf_apple_gxf_tcg_exec(vf_apple_gxf_v1 *, uint32_t insn, unsigned el,
                          uint64_t return_pc, uint64_t spsr, uint64_t *next_pc);

#endif
