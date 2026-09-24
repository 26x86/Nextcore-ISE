#include "apple_gxf_v1.h"
#include <assert.h>
#include <stdio.h>

/* Test-only SPRR TLB flush hook: counts invocations + last generation. */
static uint64_t g_sprr_flush_hits;
static uint64_t g_sprr_flush_last_gen;

static void test_sprr_tlb_flush_hook(void *ctx, uint64_t perm_generation) {
    (void)ctx;
    g_sprr_flush_hits++;
    g_sprr_flush_last_gen = perm_generation;
}

int main(void) {
    vf_apple_gxf_v1 g;
    uint64_t v = 0;

    assert(!vf_apple_gxf_init(&g));
    assert(!vf_apple_gxf_read(&g, VF_GXF_REG_GXF_CONFIG_EL1, &v) && v == 0);
    assert(!vf_apple_gxf_read(&g, VF_GXF_REG_GXF_STATUS_EL1, &v) && v == 0);
    assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_CONFIG_EL1, 0x11u));
    assert(!vf_apple_gxf_read(&g, VF_GXF_REG_GXF_CONFIG_EL1, &v) && v == 0x11u);
    assert(vf_apple_gxf_write(&g, VF_GXF_REG_GXF_STATUS_EL1, 1) == -1);

    assert(vf_apple_gxf_read(&g, VF_GXF_REG_TPIDR_GL11, &v) == -1);
    assert(vf_apple_gxf_write(&g, VF_GXF_REG_TPIDR_GL11, 0x1000) == -1);
    vf_apple_gxf_set_guarded(&g, 1);
    assert(!vf_apple_gxf_write(&g, VF_GXF_REG_TPIDR_GL11, 0x1000));
    assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TPIDR_GL11, &v) && v == 0x1000);
    assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VBAR_GL11, 0x8000u | 0x1fu));
    assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_GL11, &v) && v == 0x8000u);

    assert(!vf_apple_gxf_write(&g, VF_GXF_REG_SPRR_CONFIG_EL1, 0x42u));
    assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPRR_CONFIG_EL1, &v) && v == 0x42u);
    assert(!vf_apple_gxf_write(&g, VF_GXF_REG_MPRR_EL0BR0_EL1, 0xf0f0u));
    assert(!vf_apple_gxf_read(&g, VF_GXF_REG_MPRR_EL0BR0_EL1, &v) && v == 0xf0f0u);

    /* EL1 SPRR_EL0BR0 write is unmasked; no perm generation / flush. */
    assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_SPRR_EL0BR0_EL1, 0x77777777u, 1u));
    assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPRR_EL0BR0_EL1, &v) && v == 0x77777777u);
    assert(g.sprr_perm_generation == 0);
    assert(g.sprr_tlb_flush_count == 0);
    assert(vf_apple_gxf_sprr_perm_generation(&g) == 0);
    assert(vf_apple_gxf_sprr_tlb_flush_count(&g) == 0);

    /* MPRR allow: slot0=0b11 (WX), slot1=0b00 (none), rest zero.
     * EL0 may flip only W/X in slot0; R (bit2) and other slots stay.
     * Merge bumps generation + stub TLB flush counter + optional hook. */
    g_sprr_flush_hits = 0;
    g_sprr_flush_last_gen = 0;
    vf_apple_gxf_set_sprr_tlb_flush(&g, test_sprr_tlb_flush_hook, NULL);
    assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_MPRR_EL0BR0_EL1, 0x3u, 1u));
    assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_SPRR_EL0BR0_EL1, 0x0u, 1u));
    assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_SPRR_EL0BR0_EL1,
                                  /* slot0 req=0b111 (RWX), slot1 req=0b111 */
                                  0x77u, 0u));
    assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPRR_EL0BR0_EL1, &v));
    /* slot0: allow 0b11 → W/X become 1; R stays 0 → 0b011. slot1 unchanged 0. */
    assert(v == 0x3u);
    assert(g.sprr_perm_generation == 1);
    assert(g.sprr_tlb_flush_count == 1);
    assert(vf_apple_gxf_sprr_perm_generation(&g) == 1);
    assert(vf_apple_gxf_sprr_tlb_flush_count(&g) == 1);
    assert(g_sprr_flush_hits == 1);
    assert(g_sprr_flush_last_gen == 1);
    /* Clear hook: second EL0 merge still bumps counters, no callback. */
    vf_apple_gxf_set_sprr_tlb_flush(&g, NULL, NULL);
    assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_SPRR_EL0BR0_EL1, 0x1u, 0u));
    assert(g.sprr_perm_generation == 2);
    assert(g.sprr_tlb_flush_count == 2);
    assert(g_sprr_flush_hits == 1);

    assert(vf_apple_gxf_read(&g, 0u, &v) == -1);
    assert(vf_apple_gxf_write(&g, 0u, 0) == -1);

    /* GENTER/GEXIT guarded path: CONFIG_EN required; STATUS bit0 + GL11 bank. */
    {
        uint64_t enter_pc = 0;
        uint64_t ret_pc = 0;

        vf_apple_gxf_set_guarded(&g, 0);
        assert(!vf_apple_gxf_is_guarded(&g));
        /* Clear prior CONFIG (0x11) so GENTER fails closed without CONFIG_EN. */
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_CONFIG_EL1, 0u));
        assert(vf_apple_gxf_genter(&g, 0x1000u, 0x5u, &enter_pc) == -1);

        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_CONFIG_EL1, VF_GXF_CONFIG_EN));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_ENTER_EL1, 0xABCD0000u));
        assert(!vf_apple_gxf_genter(&g, 0x2000u, 0x3C5u, &enter_pc));
        assert(enter_pc == 0xABCD0000u);
        assert(vf_apple_gxf_is_guarded(&g));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_GXF_STATUS_EL1, &v) &&
               (v & VF_GXF_STATUS_GUARDED));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ELR_GL11, &v) && v == 0x2000u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPSR_GL11, &v) && v == 0x3C5u);
        assert(g.gxf_mode_generation == 1);
        /* Nested GENTER fails closed. */
        assert(vf_apple_gxf_genter(&g, 0x3000u, 0u, &enter_pc) == -1);

        assert(!vf_apple_gxf_gexit(&g, &ret_pc));
        assert(ret_pc == 0x2000u);
        assert(!vf_apple_gxf_is_guarded(&g));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_GXF_STATUS_EL1, &v) &&
               !(v & VF_GXF_STATUS_GUARDED));
        assert(g.gxf_mode_generation == 2);
        assert(vf_apple_gxf_gexit(&g, &ret_pc) == -1);
        /* GL11 bank trapped again after GEXIT. */
        assert(vf_apple_gxf_read(&g, VF_GXF_REG_TPIDR_GL11, &v) == -1);
    }

    /* EL1 architectural aliases: dual normal / guarded view. */
    {
        uint64_t enter_pc = 0;
        uint64_t ret_pc = 0;

        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_TPIDR_EL1, 0xA111u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VBAR_EL1, 0x9000u | 0x1fu));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_SPSR_EL1, 0x11u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_ELR_EL1, 0x22u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_ESR_EL1, 0x33u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_FAR_EL1, 0x44u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TPIDR_EL1, &v) && v == 0xA111u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_EL1, &v) && v == 0x9000u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPSR_EL1, &v) && v == 0x11u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ELR_EL1, &v) && v == 0x22u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0x33u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_EL1, &v) && v == 0x44u);
        /* Normal bank isolated from GL11 encodings (still trapped). */
        assert(vf_apple_gxf_read(&g, VF_GXF_REG_TPIDR_GL11, &v) == -1);

        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_CONFIG_EL1, VF_GXF_CONFIG_EN));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_ENTER_EL1, 0xBEEF0000u));
        assert(!vf_apple_gxf_genter(&g, 0x5000u, 0x55u, &enter_pc));
        assert(enter_pc == 0xBEEF0000u);
        /* Guarded: EL1 aliases route to GL11; normal bank preserved. */
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_TPIDR_EL1, 0xB222u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VBAR_EL1, 0xA000u | 0x7u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_ESR_EL1, 0x77u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_FAR_EL1, 0x88u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TPIDR_EL1, &v) && v == 0xB222u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TPIDR_GL11, &v) && v == 0xB222u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_EL1, &v) && v == 0xA000u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_GL11, &v) && v == 0xA000u);
        /* GENTER saved return visible via ELR_EL1 alias while guarded. */
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ELR_EL1, &v) && v == 0x5000u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPSR_EL1, &v) && v == 0x55u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_GL11, &v) && v == 0x77u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_GL11, &v) && v == 0x88u);

        assert(!vf_apple_gxf_gexit(&g, &ret_pc));
        assert(ret_pc == 0x5000u);
        /* After GEXIT: aliases return to normal bank; GL11 trapped again. */
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TPIDR_EL1, &v) && v == 0xA111u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_EL1, &v) && v == 0x9000u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ELR_EL1, &v) && v == 0x22u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPSR_EL1, &v) && v == 0x11u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0x33u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_EL1, &v) && v == 0x44u);
        assert(vf_apple_gxf_read(&g, VF_GXF_REG_TPIDR_GL11, &v) == -1);
    }

    /* GXF_ABORT vector delivery + ASPSR nest on GEXIT. */
    {
        uint64_t abort_pc = 0;
        uint64_t ret_pc = 0;
        uint64_t mode_gen = 0;

        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_CONFIG_EL1, VF_GXF_CONFIG_EN));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_ABORT_EL1, 0xDEAD0000u));
        mode_gen = g.gxf_mode_generation;
        assert(!vf_apple_gxf_abort(&g, 0x7000u, 0x77u, 0xE5u, 0xFAu, &abort_pc));
        assert(abort_pc == 0xDEAD0000u);
        assert(vf_apple_gxf_is_guarded(&g));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ELR_GL11, &v) && v == 0x7000u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPSR_GL11, &v) && v == 0x77u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_GL11, &v) && v == 0xE5u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_GL11, &v) && v == 0xFAu);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ASPSR_GL11, &v) &&
               !(v & VF_GXF_ASPSR_NEST));
        assert(g.gxf_mode_generation == mode_gen + 1);
        /* GENTER still fail-closed while guarded. */
        assert(vf_apple_gxf_genter(&g, 0u, 0u, &abort_pc) == -1);

        /* Hardware ASPSR nest-on-GL-exception: abort while guarded sets nest. */
        mode_gen = g.gxf_mode_generation;
        assert(!vf_apple_gxf_abort(&g, 0x7100u, 0x71u, 0xE6u, 0xFBu, &abort_pc));
        assert(abort_pc == 0xDEAD0000u);
        assert(vf_apple_gxf_is_guarded(&g));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ELR_GL11, &v) && v == 0x7100u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPSR_GL11, &v) && v == 0x71u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_GL11, &v) && v == 0xE6u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_GL11, &v) && v == 0xFBu);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ASPSR_GL11, &v) &&
               (v & VF_GXF_ASPSR_NEST));
        assert(g.gxf_mode_generation == mode_gen + 1);

        /* Nest consume: first GEXIT stays in GL; second leaves to EL. */
        assert(!vf_apple_gxf_gexit(&g, &ret_pc));
        assert(ret_pc == 0x7100u);
        assert(vf_apple_gxf_is_guarded(&g));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ASPSR_GL11, &v) &&
               !(v & VF_GXF_ASPSR_NEST));
        assert(!vf_apple_gxf_gexit(&g, &ret_pc));
        assert(ret_pc == 0x7100u);
        assert(!vf_apple_gxf_is_guarded(&g));
        assert(vf_apple_gxf_read(&g, VF_GXF_REG_ASPSR_GL11, &v) == -1);
    }

    /* TVM/TRVM gates on ESR_EL1 / FAR_EL1 aliases (HCR_EL2 host shadow). */
    {
        uint64_t enter_pc = 0;
        uint64_t ret_pc = 0;

        vf_apple_gxf_set_hcr_el2(&g, 0);
        assert(vf_apple_gxf_get_hcr_el2(&g) == 0);
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_ESR_EL1, 0x111u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_FAR_EL1, 0x222u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0x111u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_EL1, &v) && v == 0x222u);

        /* TRVM traps EL1 reads; TVM traps EL1 writes; bank unchanged on trap. */
        vf_apple_gxf_set_hcr_el2(&g, VF_GXF_HCR_TRVM);
        assert(vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) == -1);
        assert(vf_apple_gxf_read(&g, VF_GXF_REG_FAR_EL1, &v) == -1);
        /* Ungated aliases still readable; writes allowed when only TRVM set. */
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TPIDR_EL1, &v));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_ESR_EL1, 0x333u));
        vf_apple_gxf_set_hcr_el2(&g, 0);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0x333u);

        vf_apple_gxf_set_hcr_el2(&g, VF_GXF_HCR_TVM);
        assert(vf_apple_gxf_write(&g, VF_GXF_REG_ESR_EL1, 0x444u) == -1);
        assert(vf_apple_gxf_write(&g, VF_GXF_REG_FAR_EL1, 0x555u) == -1);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0x333u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_EL1, &v) && v == 0x222u);

        /* el>=2 bypasses TVM (hypervisor poke of guest VMSA bank). */
        assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_ESR_EL1, 0x666u, 2u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0x666u);

        /* Guarded: EL1 ESR/FAR aliases still honor TVM/TRVM; GL11 direct does not. */
        vf_apple_gxf_set_hcr_el2(&g, VF_GXF_HCR_TVM | VF_GXF_HCR_TRVM);
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_CONFIG_EL1, VF_GXF_CONFIG_EN));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_ENTER_EL1, 0xC0DE0000u));
        assert(!vf_apple_gxf_genter(&g, 0x9000u, 0x99u, &enter_pc));
        assert(vf_apple_gxf_write(&g, VF_GXF_REG_ESR_EL1, 0x777u) == -1);
        assert(vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) == -1);
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_ESR_GL11, 0x888u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_GL11, &v) && v == 0x888u);
        assert(!vf_apple_gxf_gexit(&g, &ret_pc));
        vf_apple_gxf_set_hcr_el2(&g, 0);
    }

    /* VMSA_LOCK_EL1: unguarded VBAR_EL1 write silent no-op when VBAR bit set. */
    {
        uint64_t enter_pc = 0;
        uint64_t ret_pc = 0;

        vf_apple_gxf_set_vmsa_lock_el1(&g, 0);
        assert(vf_apple_gxf_get_vmsa_lock_el1(&g) == 0);
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VBAR_EL1, 0xB000u | 0x1fu));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_EL1, &v) && v == 0xB000u);

        /* Sticky-OR via sysreg write; unlock attempt does not clear bits. */
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VMSA_LOCK_EL1,
                                   VF_GXF_VMSA_LOCK_VBAR_EL1));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VMSA_LOCK_EL1, &v) &&
               (v & VF_GXF_VMSA_LOCK_VBAR_EL1));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VMSA_LOCK_EL1, 0u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VMSA_LOCK_EL1, &v) &&
               (v & VF_GXF_VMSA_LOCK_VBAR_EL1));

        /* Locked unguarded write: success (no trap) but bank unchanged. */
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VBAR_EL1, 0xC000u | 0x7u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_EL1, &v) && v == 0xB000u);

        /* el>=2 bypasses VMSA VBAR lock (host/hypervisor poke). */
        assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_VBAR_EL1, 0xD000u, 2u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_EL1, &v) && v == 0xD000u);

        /* Guarded: VBAR alias still writable into GL11 despite lock. */
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_CONFIG_EL1, VF_GXF_CONFIG_EN));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_ENTER_EL1, 0xF00D0000u));
        assert(!vf_apple_gxf_genter(&g, 0xA000u, 0xAAu, &enter_pc));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VBAR_EL1, 0xE000u | 0x3u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_EL1, &v) && v == 0xE000u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_GL11, &v) && v == 0xE000u);
        assert(!vf_apple_gxf_gexit(&g, &ret_pc));
        /* After GEXIT: normal bank still at pre-guard / el2 poke value. */
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_EL1, &v) && v == 0xD000u);
        vf_apple_gxf_set_vmsa_lock_el1(&g, 0);
    }

    /* VMSA_LOCK_EL1: SCTLR/TCR/TTBR consumers (unguarded EL1 silent no-op / M-bit). */
    {
        uint64_t enter_pc = 0;
        uint64_t ret_pc = 0;

        vf_apple_gxf_set_vmsa_lock_el1(&g, 0);
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_SCTLR_EL1, 0x30d00801u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_TCR_EL1, 0x1111u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_TTBR0_EL1, 0x2222u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_TTBR1_EL1, 0x3333u));

        /* Full SCTLR lock: write ignored. */
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VMSA_LOCK_EL1,
                                   VF_GXF_VMSA_LOCK_SCTLR_EL1));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_SCTLR_EL1, 0xDEADBEEFu));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SCTLR_EL1, &v) &&
               v == 0x30d00801u);

        /* Host clear lock; SCTLR_M_BIT preserves M while other bits change. */
        vf_apple_gxf_set_vmsa_lock_el1(&g, VF_GXF_VMSA_LOCK_SCTLR_M_BIT);
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_SCTLR_EL1, 0xFFFFFFFEu));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SCTLR_EL1, &v) &&
               v == (0xFFFFFFFEU | VF_GXF_SCTLR_M));
        assert(v & VF_GXF_SCTLR_M);

        /* TCR / TTBR0 / TTBR1 locks. */
        vf_apple_gxf_set_vmsa_lock_el1(
            &g, VF_GXF_VMSA_LOCK_TCR_EL1 | VF_GXF_VMSA_LOCK_TTBR0_EL1 |
                    VF_GXF_VMSA_LOCK_TTBR1_EL1);
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_TCR_EL1, 0xAAAAu));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_TTBR0_EL1, 0xBBBBu));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_TTBR1_EL1, 0xCCCCu));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TCR_EL1, &v) && v == 0x1111u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TTBR0_EL1, &v) && v == 0x2222u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TTBR1_EL1, &v) && v == 0x3333u);

        /* el>=2 bypass. */
        assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_TCR_EL1, 0x4444u, 2u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TCR_EL1, &v) && v == 0x4444u);

        /* Guarded bypass: same single bank remains writable. */
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_CONFIG_EL1, VF_GXF_CONFIG_EN));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_ENTER_EL1, 0xBEEF0000u));
        assert(!vf_apple_gxf_genter(&g, 0xB000u, 0xBBu, &enter_pc));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_TTBR0_EL1, 0x5555u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_TTBR0_EL1, &v) && v == 0x5555u);
        assert(!vf_apple_gxf_gexit(&g, &ret_pc));
        vf_apple_gxf_set_vmsa_lock_el1(&g, 0);
    }

    /* Live HCR_EL2 MSR → hcr_el2 shadow (EL2+ write_el couples TVM/TRVM). */
    {
        vf_apple_gxf_set_hcr_el2(&g, 0);
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_ESR_EL1, 0xA1u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_FAR_EL1, 0xA2u));

        /* EL1 MSR of architectural HCR_EL2 fails closed (privilege). */
        assert(vf_apple_gxf_write(&g, VF_GXF_REG_HCR_EL2, VF_GXF_HCR_TRVM) ==
               -1);
        assert(vf_apple_gxf_write_el(&g, VF_GXF_REG_HCR_EL2, VF_GXF_HCR_TRVM,
                                     1u) == -1);
        assert(vf_apple_gxf_get_hcr_el2(&g) == 0);
        assert(vf_apple_gxf_read(&g, VF_GXF_REG_HCR_EL2, &v) == -1);

        /* EL2+ MSR updates the same shadow used by TVM/TRVM gates. */
        assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_HCR_EL2, VF_GXF_HCR_TRVM,
                                      2u));
        assert(vf_apple_gxf_get_hcr_el2(&g) == VF_GXF_HCR_TRVM);
        assert(vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) == -1);
        assert(vf_apple_gxf_read(&g, VF_GXF_REG_FAR_EL1, &v) == -1);

        assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_HCR_EL2, VF_GXF_HCR_TVM,
                                      2u));
        assert(vf_apple_gxf_get_hcr_el2(&g) == VF_GXF_HCR_TVM);
        assert(vf_apple_gxf_write(&g, VF_GXF_REG_ESR_EL1, 0xB1u) == -1);
        assert(vf_apple_gxf_write(&g, VF_GXF_REG_FAR_EL1, 0xB2u) == -1);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0xA1u);

        /* Clear via EL2 MSR; host set_hcr_el2 remains equivalent inject. */
        assert(!vf_apple_gxf_write_el(&g, VF_GXF_REG_HCR_EL2, 0u, 3u));
        assert(vf_apple_gxf_get_hcr_el2(&g) == 0);
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_ESR_EL1, 0xC1u));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0xC1u);
        vf_apple_gxf_set_hcr_el2(&g, 0);
    }

    /* VBAR_GL exception offset vectoring (IRQ/FIQ/same-EL / lower-EL tables). */
    {
        uint64_t vector_pc = 0;
        uint64_t enter_pc = 0;
        uint64_t ret_pc = 0;

        /* Unguarded: vectors from VBAR_EL1 + table + kind. */
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VBAR_EL1, 0x10000u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_ESR_EL1, 0x111u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_FAR_EL1, 0x222u));
        assert(vf_apple_gxf_take_exception(&g, 99u, VF_GXF_VBAR_TABLE_CURRENT_SP0,
                                           0u, 0u, 0u, 0u, &vector_pc) == -1);
        assert(vf_apple_gxf_take_exception(&g, VF_GXF_EXCP_IRQ, 99u,
                                           0u, 0u, 0u, 0u, &vector_pc) == -1);

        assert(!vf_apple_gxf_take_exception(
            &g, VF_GXF_EXCP_IRQ, VF_GXF_VBAR_TABLE_CURRENT_SPX,
            0x3000u, 0x31u, 0xDEADU, 0xBEEFU, &vector_pc));
        assert(vector_pc == 0x10000u + 0x200u + 0x80u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ELR_EL1, &v) && v == 0x3000u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPSR_EL1, &v) && v == 0x31u);
        /* IRQ does not clobber ESR/FAR. */
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0x111u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_EL1, &v) && v == 0x222u);

        assert(!vf_apple_gxf_take_exception(
            &g, VF_GXF_EXCP_SYNC, VF_GXF_VBAR_TABLE_LOWER_A64,
            0x3100u, 0x32u, 0xE5u, 0xFAu, &vector_pc));
        assert(vector_pc == 0x10000u + 0x400u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0xE5u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_EL1, &v) && v == 0xFAu);

        /* Guarded: vectors from VBAR_GL11; normal EL1 bank preserved. */
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_CONFIG_EL1, VF_GXF_CONFIG_EN));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_ENTER_EL1, 0xBEC00000u));
        assert(!vf_apple_gxf_genter(&g, 0xD000u, 0xDDu, &enter_pc));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_VBAR_GL11, 0x20000u | 0x1fu));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_ESR_GL11, 0x55u));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_FAR_GL11, 0x66u));

        assert(!vf_apple_gxf_take_exception(
            &g, VF_GXF_EXCP_FIQ, VF_GXF_VBAR_TABLE_CURRENT_SP0,
            0x4000u, 0x41u, 0u, 0u, &vector_pc));
        assert(vector_pc == 0x20000u + 0x100u);
        assert(vf_apple_gxf_is_guarded(&g));
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ELR_GL11, &v) && v == 0x4000u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_SPSR_GL11, &v) && v == 0x41u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_GL11, &v) && v == 0x55u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_GL11, &v) && v == 0x66u);

        assert(!vf_apple_gxf_take_exception(
            &g, VF_GXF_EXCP_SERR, VF_GXF_VBAR_TABLE_LOWER_A32,
            0x4100u, 0x42u, 0xABu, 0xBADU, &vector_pc));
        assert(vector_pc == 0x20000u + 0x600u + 0x180u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_GL11, &v) && v == 0xABu);
        /* SERR updates ESR but not FAR. */
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_FAR_GL11, &v) && v == 0x66u);

        assert(!vf_apple_gxf_gexit(&g, &ret_pc));
        assert(ret_pc == 0x4100u);
        /* After GEXIT: normal bank still holds last unguarded take_exception. */
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_VBAR_EL1, &v) && v == 0x10000u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ELR_EL1, &v) && v == 0x3100u);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ESR_EL1, &v) && v == 0xE5u);
    }

    /* TCG opcode stub: classify + exec → genter/gexit (no QEMU translate). */
    {
        uint64_t next_pc = 0;
        uint64_t mode0 = 0;

        assert(vf_apple_gxf_tcg_classify(VF_GXF_TCG_INSN_GENTER) ==
               VF_GXF_TCG_KIND_GENTER);
        assert(vf_apple_gxf_tcg_classify(VF_GXF_TCG_INSN_GEXIT) ==
               VF_GXF_TCG_KIND_GEXIT);
        /* rn=2 under opcode 5 is not GENTER/GEXIT. */
        assert(vf_apple_gxf_tcg_classify(0x00201440u) == VF_GXF_TCG_KIND_NONE);
        /* bit31 set / bits[28:25]!=0 → not apple GXF path. */
        assert(vf_apple_gxf_tcg_classify(0x80201420u) == VF_GXF_TCG_KIND_NONE);
        assert(vf_apple_gxf_tcg_classify(0x02001420u) == VF_GXF_TCG_KIND_NONE);

        vf_apple_gxf_set_guarded(&g, 0);
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_CONFIG_EL1, VF_GXF_CONFIG_EN));
        assert(!vf_apple_gxf_write(&g, VF_GXF_REG_GXF_ENTER_EL1, 0xC7000000u));
        mode0 = g.gxf_mode_generation;
        /* EL0 fail-closed. */
        assert(vf_apple_gxf_tcg_exec(&g, VF_GXF_TCG_INSN_GENTER, 0u, 0x1000u, 0x5u,
                                     &next_pc) == -1);
        assert(!vf_apple_gxf_is_guarded(&g));
        assert(g.gxf_mode_generation == mode0);

        assert(!vf_apple_gxf_tcg_exec(&g, VF_GXF_TCG_INSN_GENTER, 1u, 0x7000u, 0x77u,
                                      &next_pc));
        assert(next_pc == 0xC7000000u);
        assert(vf_apple_gxf_is_guarded(&g));
        assert(g.gxf_mode_generation == mode0 + 1);
        assert(!vf_apple_gxf_read(&g, VF_GXF_REG_ELR_GL11, &v) && v == 0x7000u);
        /* Nested GENTER via opcode fails closed (same as helper). */
        assert(vf_apple_gxf_tcg_exec(&g, VF_GXF_TCG_INSN_GENTER, 1u, 0x7100u, 0u,
                                     &next_pc) == -1);

        assert(!vf_apple_gxf_tcg_exec(&g, VF_GXF_TCG_INSN_GEXIT, 1u, 0u, 0u,
                                      &next_pc));
        assert(next_pc == 0x7000u);
        assert(!vf_apple_gxf_is_guarded(&g));
        assert(g.gxf_mode_generation == mode0 + 2);
        /* GEXIT while unguarded fails closed. */
        assert(vf_apple_gxf_tcg_exec(&g, VF_GXF_TCG_INSN_GEXIT, 1u, 0u, 0u,
                                     &next_pc) == -1);
        assert(vf_apple_gxf_tcg_exec(&g, 0xdeadbeefu, 1u, 0u, 0u, &next_pc) == -1);
    }

    puts("PASS Apple GXF v1 GXF/SPRR fail-closed");
    return 0;
}