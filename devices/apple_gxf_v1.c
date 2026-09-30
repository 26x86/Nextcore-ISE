/* 26x86 first-party GXF/SPRR v1 sysreg stub. See sandbox/devices/GXF.md for register evidence.
 * Behaviour adapted from qemu-t8030 / Inferno a13_gxf (GPL reference only).
 */
#include "apple_gxf_v1.h"

/* SPRR region attrs: 16 × 4-bit slots at bit index*4.
 * EL0-writable subset is RWX (low 3 bits). MPRR allow bits are 2-bit packs at index*2. */
#define VF_GXF_SPRR_SLOT_SHIFT(i) ((unsigned)(i) * 4u)
#define VF_GXF_SPRR_RWX_MASK      UINT64_C(0x7)
#define VF_GXF_MPRR_ALLOW_SHIFT(i) ((unsigned)(i) * 2u)
#define VF_GXF_MPRR_ALLOW_MASK    UINT64_C(0x3)

static void gxf_sync_guarded_from_status(vf_apple_gxf_v1 *g) {
    if (!g) return;
    g->guarded = (g->gxf_status_el1 & VF_GXF_STATUS_GUARDED) ? 1 : 0;
}

static int gxf_guarded_only(const vf_apple_gxf_v1 *g, uint32_t reg) {
    switch (reg) {
    case VF_GXF_REG_ASPSR_GL11:
    case VF_GXF_REG_SP_GL11:
    case VF_GXF_REG_TPIDR_GL11:
    case VF_GXF_REG_VBAR_GL11:
    case VF_GXF_REG_SPSR_GL11:
    case VF_GXF_REG_ESR_GL11:
    case VF_GXF_REG_ELR_GL11:
    case VF_GXF_REG_FAR_GL11:
        return !g || !g->guarded;
    default:
        return 0;
    }
}

/* Dual-view EL1 architectural aliases: guarded → GL11 bank, else normal EL1.
 * ESR_EL1/FAR_EL1 honor HCR_EL2 shadow TVM (write) / TRVM (read) at EL1 only. */
static int gxf_tvm_trvm_blocks(const vf_apple_gxf_v1 *g, uint32_t reg,
                               int isread, unsigned el) {
    if (!g || el != 1u) return 0;
    switch (reg) {
    case VF_GXF_REG_ESR_EL1:
    case VF_GXF_REG_FAR_EL1:
        if (isread) {
            return (g->hcr_el2 & VF_GXF_HCR_TRVM) ? 1 : 0;
        }
        return (g->hcr_el2 & VF_GXF_HCR_TVM) ? 1 : 0;
    default:
        return 0;
    }
}

/* Unguarded EL1 VMSA lock bit active (T8030: !arm_is_guarded && lock bit).
 * Guarded / el>=2 bypass all VMSA lock consumers. */
static int gxf_vmsa_locked(const vf_apple_gxf_v1 *g, int guarded, unsigned el,
                           uint64_t lock_bit) {
    if (!g || guarded || el != 1u) return 0;
    return (g->vmsa_lock_el1 & lock_bit) ? 1 : 0;
}

static int gxf_el1_alias_read(const vf_apple_gxf_v1 *g, uint32_t reg,
                              uint64_t *value, unsigned el) {
    int guarded = g->guarded;

    if (gxf_tvm_trvm_blocks(g, reg, 1, el)) return -1;

    switch (reg) {
    case VF_GXF_REG_TPIDR_EL1:
        *value = guarded ? g->tpidr_gl11 : g->tpidr_el1;
        return 0;
    case VF_GXF_REG_VBAR_EL1:
        *value = guarded ? g->vbar_gl11 : g->vbar_el1;
        return 0;
    case VF_GXF_REG_SPSR_EL1:
        *value = guarded ? g->spsr_gl11 : g->spsr_el1;
        return 0;
    case VF_GXF_REG_ELR_EL1:
        *value = guarded ? g->elr_gl11 : g->elr_el1;
        return 0;
    case VF_GXF_REG_ESR_EL1:
        *value = guarded ? g->esr_gl11 : g->esr_el1;
        return 0;
    case VF_GXF_REG_FAR_EL1:
        *value = guarded ? g->far_gl11 : g->far_el1;
        return 0;
    default:
        return -1;
    }
}

static int gxf_el1_alias_write(vf_apple_gxf_v1 *g, uint32_t reg,
                               uint64_t value, unsigned el) {
    int guarded = g->guarded;
    uint64_t vbar = value & ~UINT64_C(0x1f);

    if (gxf_tvm_trvm_blocks(g, reg, 0, el)) return -1;

    switch (reg) {
    case VF_GXF_REG_TPIDR_EL1:
        if (guarded) {
            g->tpidr_gl11 = value;
        } else {
            g->tpidr_el1 = value;
        }
        return 0;
    case VF_GXF_REG_VBAR_EL1:
        if (gxf_vmsa_locked(g, guarded, el, VF_GXF_VMSA_LOCK_VBAR_EL1)) {
            /* Silent ignore (reference writefn returns without raw_write). */
            return 0;
        }
        if (guarded) {
            g->vbar_gl11 = vbar;
        } else {
            g->vbar_el1 = vbar;
        }
        return 0;
    case VF_GXF_REG_SPSR_EL1:
        if (guarded) {
            g->spsr_gl11 = value;
        } else {
            g->spsr_el1 = value;
        }
        return 0;
    case VF_GXF_REG_ELR_EL1:
        if (guarded) {
            g->elr_gl11 = value;
        } else {
            g->elr_el1 = value;
        }
        return 0;
    case VF_GXF_REG_ESR_EL1:
        if (guarded) {
            g->esr_gl11 = value;
        } else {
            g->esr_el1 = value;
        }
        return 0;
    case VF_GXF_REG_FAR_EL1:
        if (guarded) {
            g->far_gl11 = value;
        } else {
            g->far_el1 = value;
        }
        return 0;
    default:
        return -1;
    }
}

/* Merge EL0 SPRR_EL0BR0 write under MPRR_EL0BR0 allow masks. */
static uint64_t gxf_merge_sprr_el0br0(uint64_t orig, uint64_t requested,
                                      uint64_t mprr_el0br0) {
    uint64_t perm = orig;
    int i;

    for (i = 0; i < 16; i++) {
        unsigned shift = VF_GXF_SPRR_SLOT_SHIFT(i);
        uint64_t allow = (mprr_el0br0 >> VF_GXF_MPRR_ALLOW_SHIFT(i)) &
                         VF_GXF_MPRR_ALLOW_MASK;
        uint64_t req = (requested >> shift) & VF_GXF_SPRR_RWX_MASK;
        uint64_t old = (orig >> shift) & VF_GXF_SPRR_RWX_MASK;
        uint64_t changed = (req ^ old) & allow;
        uint64_t result = (old & ~changed) | (req & changed);

        perm &= ~(VF_GXF_SPRR_RWX_MASK << shift);
        perm |= result << shift;
    }
    return perm;
}

/* Bump SPRR perm generation + flush counter; invoke optional hook.
 * Host may wire sprr_tlb_flush → guest JIT vf_cpu_invalidate_tlb (bridge).
 * NULL hook = count-only; not a QEMU SoftMMU TLB by itself. */
static void gxf_sprr_perm_changed(vf_apple_gxf_v1 *g) {
    g->sprr_perm_generation++;
    g->sprr_tlb_flush_count++;
    if (g->sprr_tlb_flush) {
        g->sprr_tlb_flush(g->sprr_tlb_flush_ctx, g->sprr_perm_generation);
    }
}

int vf_apple_gxf_init(vf_apple_gxf_v1 *g) {
    if (!g) return -1;
    g->guarded = 0;
    g->gxf_config_el1 = 0;
    g->gxf_status_el1 = 0;
    g->gxf_enter_el1 = 0;
    g->gxf_abort_el1 = 0;
    g->aspsr_gl11 = 0;
    g->sp_gl11 = 0;
    g->tpidr_gl11 = 0;
    g->vbar_gl11 = 0;
    g->spsr_gl11 = 0;
    g->esr_gl11 = 0;
    g->elr_gl11 = 0;
    g->far_gl11 = 0;
    g->tpidr_el1 = 0;
    g->vbar_el1 = 0;
    g->spsr_el1 = 0;
    g->elr_el1 = 0;
    g->esr_el1 = 0;
    g->far_el1 = 0;
    g->hcr_el2 = 0;
    g->vmsa_lock_el1 = 0;
    g->sctlr_el1 = 0;
    g->tcr_el1 = 0;
    g->ttbr0_el1 = 0;
    g->ttbr1_el1 = 0;
    g->sprr_config_el1 = 0;
    g->sprr_config_el0 = 0;
    g->sprr_el0br0_el1 = 0;
    g->sprr_el0br1_el1 = 0;
    g->sprr_el1br0_el1 = 0;
    g->sprr_el1br1_el1 = 0;
    g->mprr_el0br0_el1 = 0;
    g->mprr_el0br1_el1 = 0;
    g->mprr_el1br0_el1 = 0;
    g->mprr_el1br1_el1 = 0;
    g->sprr_perm_generation = 0;
    g->sprr_tlb_flush_count = 0;
    g->sprr_tlb_flush = 0;
    g->sprr_tlb_flush_ctx = 0;
    g->gxf_mode_generation = 0;
    return 0;
}

void vf_apple_gxf_set_guarded(vf_apple_gxf_v1 *g, int guarded) {
    if (!g) return;
    if (guarded) {
        g->gxf_status_el1 |= VF_GXF_STATUS_GUARDED;
    } else {
        g->gxf_status_el1 &= ~VF_GXF_STATUS_GUARDED;
    }
    gxf_sync_guarded_from_status(g);
}

int vf_apple_gxf_is_guarded(const vf_apple_gxf_v1 *g) {
    return g && (g->gxf_status_el1 & VF_GXF_STATUS_GUARDED) ? 1 : 0;
}

void vf_apple_gxf_set_hcr_el2(vf_apple_gxf_v1 *g, uint64_t hcr_el2) {
    if (!g) return;
    g->hcr_el2 = hcr_el2;
}

uint64_t vf_apple_gxf_get_hcr_el2(const vf_apple_gxf_v1 *g) {
    return g ? g->hcr_el2 : 0;
}

void vf_apple_gxf_set_vmsa_lock_el1(vf_apple_gxf_v1 *g, uint64_t vmsa_lock_el1) {
    if (!g) return;
    g->vmsa_lock_el1 = vmsa_lock_el1;
}

uint64_t vf_apple_gxf_get_vmsa_lock_el1(const vf_apple_gxf_v1 *g) {
    return g ? g->vmsa_lock_el1 : 0;
}

void vf_apple_gxf_set_sprr_tlb_flush(vf_apple_gxf_v1 *g,
                                    vf_gxf_sprr_tlb_flush_fn fn, void *ctx) {
    if (!g) return;
    g->sprr_tlb_flush = fn;
    g->sprr_tlb_flush_ctx = ctx;
}

uint64_t vf_apple_gxf_sprr_perm_generation(const vf_apple_gxf_v1 *g) {
    return g ? g->sprr_perm_generation : 0;
}

uint64_t vf_apple_gxf_sprr_tlb_flush_count(const vf_apple_gxf_v1 *g) {
    return g ? g->sprr_tlb_flush_count : 0;
}

int vf_apple_gxf_genter(vf_apple_gxf_v1 *g, uint64_t return_pc, uint64_t spsr,
                        uint64_t *enter_pc) {
    if (!g || !enter_pc) return -1;
    if (!(g->gxf_config_el1 & VF_GXF_CONFIG_EN)) return -1;
    if (vf_apple_gxf_is_guarded(g)) return -1;

    g->elr_gl11 = return_pc;
    g->spsr_gl11 = spsr;
    g->aspsr_gl11 &= ~VF_GXF_ASPSR_NEST;
    g->gxf_status_el1 |= VF_GXF_STATUS_GUARDED;
    gxf_sync_guarded_from_status(g);
    g->gxf_mode_generation++;
    *enter_pc = g->gxf_enter_el1;
    return 0;
}

int vf_apple_gxf_abort(vf_apple_gxf_v1 *g, uint64_t return_pc, uint64_t spsr,
                       uint64_t esr, uint64_t far, uint64_t *abort_pc) {
    int already_guarded;

    if (!g || !abort_pc) return -1;
    if (!(g->gxf_config_el1 & VF_GXF_CONFIG_EN)) return -1;

    /* Patterned on qemu-t8030 arm_cpu_do_interrupt_aarch64 EXCP_GXF_ABORT:
     * always vector via GXF_ABORT_EL1 (not VBAR); overwrite GL bank. When
     * already in GL, hardware auto-sets ASPSR_NEST so GEXIT returns to
     * interrupted GL (Asahi/Sven ASPSR nest) instead of EL. */
    already_guarded = vf_apple_gxf_is_guarded(g);

    g->elr_gl11 = return_pc;
    g->spsr_gl11 = spsr;
    g->esr_gl11 = esr;
    g->far_gl11 = far;
    if (already_guarded) {
        g->aspsr_gl11 |= VF_GXF_ASPSR_NEST;
    } else {
        g->aspsr_gl11 &= ~VF_GXF_ASPSR_NEST;
        g->gxf_status_el1 |= VF_GXF_STATUS_GUARDED;
        gxf_sync_guarded_from_status(g);
    }
    g->gxf_mode_generation++;
    *abort_pc = g->gxf_abort_el1;
    return 0;
}

int vf_apple_gxf_take_exception(vf_apple_gxf_v1 *g, unsigned kind, unsigned table,
                                uint64_t return_pc, uint64_t spsr, uint64_t esr,
                                uint64_t far, uint64_t *vector_pc) {
    uint64_t base;
    uint64_t offset;
    int guarded;

    if (!g || !vector_pc) return -1;
    if (kind > VF_GXF_EXCP_SERR || table > VF_GXF_VBAR_TABLE_LOWER_A32) return -1;

    /* Patterned on qemu-t8030 arm_cpu_do_interrupt_aarch64: guarded uses
     * VBAR_GL, else VBAR_EL; then +0x200 (SP_ELx) / +0x400 (lower A64) /
     * +0x600 (lower A32); then +0x80 IRQ / +0x100 FIQ / +0x180 SError.
     * GXF_ABORT / GENTER keep dedicated ENTER/ABORT vectors (not this path). */
    guarded = vf_apple_gxf_is_guarded(g);
    base = guarded ? g->vbar_gl11 : g->vbar_el1;
    offset = VF_GXF_VBAR_TABLE_OFFSET(table) + VF_GXF_VBAR_KIND_OFFSET(kind);
    if (base > UINT64_MAX - offset) return -1;

    if (guarded) {
        g->elr_gl11 = return_pc;
        g->spsr_gl11 = spsr;
        if (kind == VF_GXF_EXCP_SYNC || kind == VF_GXF_EXCP_SERR) {
            g->esr_gl11 = esr;
        }
        if (kind == VF_GXF_EXCP_SYNC) {
            g->far_gl11 = far;
        }
    } else {
        g->elr_el1 = return_pc;
        g->spsr_el1 = spsr;
        if (kind == VF_GXF_EXCP_SYNC || kind == VF_GXF_EXCP_SERR) {
            g->esr_el1 = esr;
        }
        if (kind == VF_GXF_EXCP_SYNC) {
            g->far_el1 = far;
        }
    }

    *vector_pc = base + offset;
    return 0;
}

int vf_apple_gxf_gexit(vf_apple_gxf_v1 *g, uint64_t *return_pc) {
    if (!g || !return_pc) return -1;
    if (!vf_apple_gxf_is_guarded(g)) return -1;

    *return_pc = g->elr_gl11;
    if (g->aspsr_gl11 & VF_GXF_ASPSR_NEST) {
        /* Nest consume: stay in GL; subsequent gexit can still leave to EL. */
        g->aspsr_gl11 &= ~VF_GXF_ASPSR_NEST;
    } else {
        g->gxf_status_el1 &= ~VF_GXF_STATUS_GUARDED;
        gxf_sync_guarded_from_status(g);
    }
    g->gxf_mode_generation++;
    return 0;
}

int vf_apple_gxf_tcg_classify(uint32_t insn) {
    unsigned opcode;
    unsigned rn;

    /* qemu-t8030: switch(bits[28:25]==0) && !bit31 → disas_apple_insn. */
    if ((insn >> 31) & 1u) return VF_GXF_TCG_KIND_NONE;
    if (((insn >> 25) & 0xfu) != 0u) return VF_GXF_TCG_KIND_NONE;
    opcode = (insn >> 10) & 0x3fu;
    if (opcode != VF_GXF_TCG_APPLE_OP_GXF) return VF_GXF_TCG_KIND_NONE;
    rn = (insn >> 5) & 0x1fu;
    if (rn == VF_GXF_TCG_RN_GENTER) return VF_GXF_TCG_KIND_GENTER;
    if (rn == VF_GXF_TCG_RN_GEXIT) return VF_GXF_TCG_KIND_GEXIT;
    return VF_GXF_TCG_KIND_NONE;
}

int vf_apple_gxf_tcg_exec(vf_apple_gxf_v1 *g, uint32_t insn, unsigned el,
                          uint64_t return_pc, uint64_t spsr, uint64_t *next_pc) {
    int kind;

    if (!g || !next_pc) return -1;
    /* qemu-t8030 disas_apple_insn rejects EL0 before opcode switch. */
    if (el == 0u) return -1;

    kind = vf_apple_gxf_tcg_classify(insn);
    if (kind == VF_GXF_TCG_KIND_GENTER) {
        return vf_apple_gxf_genter(g, return_pc, spsr, next_pc);
    }
    if (kind == VF_GXF_TCG_KIND_GEXIT) {
        (void)return_pc;
        (void)spsr;
        return vf_apple_gxf_gexit(g, next_pc);
    }
    return -1;
}

int vf_apple_gxf_read(vf_apple_gxf_v1 *g, uint32_t reg, uint64_t *value) {
    if (!g || !value || gxf_guarded_only(g, reg)) return -1;
    /* Guest MRS of EL1 aliases: treat as EL1 (TVM/TRVM apply). */
    if (gxf_el1_alias_read(g, reg, value, 1u) == 0) return 0;
    switch (reg) {
    case VF_GXF_REG_GXF_CONFIG_EL1: *value = g->gxf_config_el1; return 0;
    case VF_GXF_REG_GXF_STATUS_EL1: *value = g->gxf_status_el1; return 0;
    case VF_GXF_REG_GXF_ENTER_EL1: *value = g->gxf_enter_el1; return 0;
    case VF_GXF_REG_GXF_ABORT_EL1: *value = g->gxf_abort_el1; return 0;
    case VF_GXF_REG_ASPSR_GL11: *value = g->aspsr_gl11; return 0;
    case VF_GXF_REG_SP_GL11: *value = g->sp_gl11; return 0;
    case VF_GXF_REG_TPIDR_GL11: *value = g->tpidr_gl11; return 0;
    case VF_GXF_REG_VBAR_GL11: *value = g->vbar_gl11; return 0;
    case VF_GXF_REG_SPSR_GL11: *value = g->spsr_gl11; return 0;
    case VF_GXF_REG_ESR_GL11: *value = g->esr_gl11; return 0;
    case VF_GXF_REG_ELR_GL11: *value = g->elr_gl11; return 0;
    case VF_GXF_REG_FAR_GL11: *value = g->far_gl11; return 0;
    case VF_GXF_REG_HCR_EL2:
        /* Guest MRS assumes EL1: architectural HCR_EL2 is EL2+ only. */
        return -1;
    case VF_GXF_REG_VMSA_LOCK_EL1: *value = g->vmsa_lock_el1; return 0;
    case VF_GXF_REG_SCTLR_EL1: *value = g->sctlr_el1; return 0;
    case VF_GXF_REG_TCR_EL1: *value = g->tcr_el1; return 0;
    case VF_GXF_REG_TTBR0_EL1: *value = g->ttbr0_el1; return 0;
    case VF_GXF_REG_TTBR1_EL1: *value = g->ttbr1_el1; return 0;
    case VF_GXF_REG_SPRR_CONFIG_EL1: *value = g->sprr_config_el1; return 0;
    case VF_GXF_REG_SPRR_CONFIG_EL0: *value = g->sprr_config_el0; return 0;
    case VF_GXF_REG_SPRR_EL0BR0_EL1: *value = g->sprr_el0br0_el1; return 0;
    case VF_GXF_REG_SPRR_EL0BR1_EL1: *value = g->sprr_el0br1_el1; return 0;
    case VF_GXF_REG_SPRR_EL1BR0_EL1: *value = g->sprr_el1br0_el1; return 0;
    case VF_GXF_REG_SPRR_EL1BR1_EL1: *value = g->sprr_el1br1_el1; return 0;
    case VF_GXF_REG_MPRR_EL0BR0_EL1: *value = g->mprr_el0br0_el1; return 0;
    case VF_GXF_REG_MPRR_EL0BR1_EL1: *value = g->mprr_el0br1_el1; return 0;
    case VF_GXF_REG_MPRR_EL1BR0_EL1: *value = g->mprr_el1br0_el1; return 0;
    case VF_GXF_REG_MPRR_EL1BR1_EL1: *value = g->mprr_el1br1_el1; return 0;
    default:
        return -1;
    }
}

int vf_apple_gxf_write_el(vf_apple_gxf_v1 *g, uint32_t reg, uint64_t value,
                          unsigned el) {
    if (!g || gxf_guarded_only(g, reg)) return -1;
    if (gxf_el1_alias_write(g, reg, value, el) == 0) return 0;
    switch (reg) {
    case VF_GXF_REG_GXF_CONFIG_EL1: g->gxf_config_el1 = value; return 0;
    case VF_GXF_REG_GXF_ENTER_EL1: g->gxf_enter_el1 = value; return 0;
    case VF_GXF_REG_GXF_ABORT_EL1: g->gxf_abort_el1 = value; return 0;
    case VF_GXF_REG_ASPSR_GL11: g->aspsr_gl11 = value; return 0;
    case VF_GXF_REG_SP_GL11: g->sp_gl11 = value; return 0;
    case VF_GXF_REG_TPIDR_GL11: g->tpidr_gl11 = value; return 0;
    case VF_GXF_REG_VBAR_GL11: g->vbar_gl11 = value & ~UINT64_C(0x1f); return 0;
    case VF_GXF_REG_SPSR_GL11: g->spsr_gl11 = value; return 0;
    case VF_GXF_REG_ESR_GL11: g->esr_gl11 = value; return 0;
    case VF_GXF_REG_ELR_GL11: g->elr_gl11 = value; return 0;
    case VF_GXF_REG_FAR_GL11: g->far_gl11 = value; return 0;
    case VF_GXF_REG_HCR_EL2:
        /* Live architectural MSR: EL2+ couples into TVM/TRVM shadow. */
        if (el < 2u) return -1;
        g->hcr_el2 = value;
        return 0;
    case VF_GXF_REG_VMSA_LOCK_EL1:
        /* Sticky-OR: bits once set cannot clear via guest write (T8030). */
        g->vmsa_lock_el1 |= value;
        return 0;
    case VF_GXF_REG_SCTLR_EL1:
        /* Full lock → silent no-op; else optional M-bit preserve (T8030). */
        if (gxf_vmsa_locked(g, g->guarded, el, VF_GXF_VMSA_LOCK_SCTLR_EL1)) {
            return 0;
        }
        if (gxf_vmsa_locked(g, g->guarded, el, VF_GXF_VMSA_LOCK_SCTLR_M_BIT)) {
            value = (value & ~VF_GXF_SCTLR_M) | (g->sctlr_el1 & VF_GXF_SCTLR_M);
        }
        g->sctlr_el1 = value;
        return 0;
    case VF_GXF_REG_TCR_EL1:
        if (gxf_vmsa_locked(g, g->guarded, el, VF_GXF_VMSA_LOCK_TCR_EL1)) {
            return 0;
        }
        g->tcr_el1 = value;
        return 0;
    case VF_GXF_REG_TTBR0_EL1:
        if (gxf_vmsa_locked(g, g->guarded, el, VF_GXF_VMSA_LOCK_TTBR0_EL1)) {
            return 0;
        }
        g->ttbr0_el1 = value;
        return 0;
    case VF_GXF_REG_TTBR1_EL1:
        if (gxf_vmsa_locked(g, g->guarded, el, VF_GXF_VMSA_LOCK_TTBR1_EL1)) {
            return 0;
        }
        g->ttbr1_el1 = value;
        return 0;
    case VF_GXF_REG_SPRR_CONFIG_EL1: g->sprr_config_el1 = value; return 0;
    case VF_GXF_REG_SPRR_CONFIG_EL0: g->sprr_config_el0 = value; return 0;
    case VF_GXF_REG_SPRR_EL0BR0_EL1:
        if (el == 0) {
            g->sprr_el0br0_el1 = gxf_merge_sprr_el0br0(
                g->sprr_el0br0_el1, value, g->mprr_el0br0_el1);
            gxf_sprr_perm_changed(g);
        } else {
            g->sprr_el0br0_el1 = value;
        }
        return 0;
    case VF_GXF_REG_SPRR_EL0BR1_EL1: g->sprr_el0br1_el1 = value; return 0;
    case VF_GXF_REG_SPRR_EL1BR0_EL1: g->sprr_el1br0_el1 = value; return 0;
    case VF_GXF_REG_SPRR_EL1BR1_EL1: g->sprr_el1br1_el1 = value; return 0;
    case VF_GXF_REG_MPRR_EL0BR0_EL1: g->mprr_el0br0_el1 = value; return 0;
    case VF_GXF_REG_MPRR_EL0BR1_EL1: g->mprr_el0br1_el1 = value; return 0;
    case VF_GXF_REG_MPRR_EL1BR0_EL1: g->mprr_el1br0_el1 = value; return 0;
    case VF_GXF_REG_MPRR_EL1BR1_EL1: g->mprr_el1br1_el1 = value; return 0;
    case VF_GXF_REG_GXF_STATUS_EL1:
    default:
        return -1;
    }
}

int vf_apple_gxf_write(vf_apple_gxf_v1 *g, uint32_t reg, uint64_t value) {
    return vf_apple_gxf_write_el(g, reg, value, 1u);
}
