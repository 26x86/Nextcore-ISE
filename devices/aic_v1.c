/* 26x86 first-party AIC v1 wired IRQ model. See AIC.md for register evidence.
 * Bank layout adapted from public Asahi/linux and qemu-t8030 hw/intc/apple_aic.c
 * (GPL reference, semi-clean room — behaviour only, no copied source).
 */
#include "aic_v1.h"

static unsigned aic_eir_words(unsigned irqs) { return (irqs + 31u) / 32u; }

static uint32_t aic_word_valid(const vf_aic_v1 *a, unsigned word) {
    unsigned first = word * 32u;
    if (first >= a->irq_count) return 0;
    if (a->irq_count - first >= 32u) return UINT32_MAX;
    return (UINT32_C(1) << (a->irq_count - first)) - 1u;
}

static int aic_resolve_access(const vf_aic_v1 *a, unsigned cpu, uint32_t off,
                              unsigned *access_cpu, uint32_t *local_off) {
    if (off >= VF_AIC_REG_CPU_BASE
        && off < VF_AIC_REG_CPU_BASE + a->cpu_count * 0x80u) {
        unsigned slot = (off - VF_AIC_REG_CPU_BASE) / 0x80u;
        uint32_t delta = (off - VF_AIC_REG_CPU_BASE) % 0x80u;
        if (delta > 0x7cu) return -1;
        *access_cpu = slot;
        *local_off = VF_AIC_REG_WHOAMI + delta;
        return 0;
    }
    if (cpu >= a->cpu_count) return -1;
    *access_cpu = cpu;
    *local_off = off;
    return 0;
}

static void aic_reset_state(vf_aic_v1 *a) {
    for (unsigned i = 0; i < VF_AIC_MAX_IRQ; i++) a->target[i] = 1;
    for (unsigned i = 0; i < 32; i++) {
        a->level[i] = 0;
        a->software[i] = 0;
        a->masked[i] = UINT32_MAX;
    }
    a->global_cfg = 0;
}

int vf_aic_init(vf_aic_v1 *a, unsigned irqs, unsigned cpus) {
    if (!a || !irqs || irqs > VF_AIC_MAX_IRQ || !cpus || cpus > VF_AIC_MAX_CPU) return -1;
    a->irq_count = irqs;
    a->cpu_count = cpus;
    aic_reset_state(a);
    return 0;
}

int vf_aic_set_line(vf_aic_v1 *a, unsigned irq, int high) {
    if (!a || irq >= a->irq_count) return -1;
    uint32_t bit = UINT32_C(1) << (irq & 31u);
    if (high) a->level[irq >> 5] |= bit;
    else a->level[irq >> 5] &= ~bit;
    return 0;
}

static int next_irq(const vf_aic_v1 *a, unsigned cpu) {
    if (!a || cpu >= a->cpu_count) return -1;
    for (unsigned word = 0; word < aic_eir_words(a->irq_count); word++) {
        uint32_t pending = (a->level[word] | a->software[word]) & ~a->masked[word];
        while (pending) {
            unsigned bit = (unsigned)__builtin_ctz(pending), irq = word * 32u + bit;
            if (irq < a->irq_count && (a->target[irq] & (UINT32_C(1) << cpu))) return (int)irq;
            pending &= pending - 1u;
        }
    }
    return -1;
}

int vf_aic_pending(const vf_aic_v1 *a, unsigned cpu) { return next_irq(a, cpu) >= 0; }

static int aic_read_bank(const vf_aic_v1 *a, uint32_t off, const uint32_t *bank,
                         uint32_t *value) {
    if (off < VF_AIC_REG_SW_BASE || off >= 0x4200u || (off & 3u)) return -1;
    unsigned word = (off & 0x7fu) / 4u;
    if (word >= aic_eir_words(a->irq_count)) return -1;
    *value = bank[word];
    return 0;
}

static int aic_write_bank(vf_aic_v1 *a, uint32_t off, uint32_t value, unsigned op) {
    if (off < VF_AIC_REG_SW_BASE || off >= 0x4200u || (off & 3u)) return -1;
    unsigned word = (off & 0x7fu) / 4u, group = (off - VF_AIC_REG_SW_BASE) / 0x80u;
    if (word >= aic_eir_words(a->irq_count) || group > 3u) return -1;
    if (value & ~aic_word_valid(a, word)) return -1;
    switch (op) {
    case 0: a->software[word] |= value; break;
    case 1: a->software[word] &= ~value; break;
    case 2: a->masked[word] |= value; break;
    case 3: a->masked[word] &= ~value; break;
    default: return -1;
    }
    return 0;
}

int vf_aic_read(vf_aic_v1 *a, unsigned cpu, uint32_t off, unsigned width, uint32_t *value) {
    if (!a || !value || width != 4 || (off & 3u)) return -1;
    unsigned access_cpu;
    uint32_t local_off;
    if (aic_resolve_access(a, cpu, off, &access_cpu, &local_off)) return -1;
    off = local_off;
    cpu = access_cpu;

    if (off == VF_AIC_REG_REV) {
        *value = 2;
        return 0;
    }
    if (off == VF_AIC_REG_INFO) {
        *value = ((a->cpu_count - 1u) << 16) | a->irq_count;
        return 0;
    }
    if (off == VF_AIC_REG_GLB_CFG) {
        *value = a->global_cfg;
        return 0;
    }
    if (off == VF_AIC_REG_WHOAMI) {
        *value = cpu;
        return 0;
    }
    if (off == VF_AIC_REG_EVENT) {
        int irq = next_irq(a, cpu);
        *value = 0;
        if (irq >= 0) {
            a->masked[(unsigned)irq >> 5] |= UINT32_C(1) << ((unsigned)irq & 31u);
            *value = VF_AIC_EVENT_EXT | (unsigned)irq;
        }
        return 0;
    }
    if (off >= VF_AIC_REG_TARGET && off < 0x4000u) {
        unsigned irq = (off - VF_AIC_REG_TARGET) / 4u;
        if (irq >= a->irq_count) return -1;
        *value = a->target[irq];
        return 0;
    }
    if (off >= 0x4100u && off < 0x4200u) return aic_read_bank(a, off, a->masked, value);
    if (off >= 0x4200u && off < 0x4280u) {
        unsigned word = (off - 0x4200u) / 4u;
        if (word >= aic_eir_words(a->irq_count)) return -1;
        *value = a->level[word] | a->software[word];
        return 0;
    }
    return -1;
}

int vf_aic_write(vf_aic_v1 *a, unsigned cpu, uint32_t off, unsigned width, uint32_t value) {
    if (!a || width != 4 || (off & 3u)) return -1;
    unsigned access_cpu;
    uint32_t local_off;
    if (aic_resolve_access(a, cpu, off, &access_cpu, &local_off)) return -1;
    off = local_off;
    cpu = access_cpu;
    (void)cpu;

    if (off == VF_AIC_REG_RST) {
        aic_reset_state(a);
        return 0;
    }
    if (off == VF_AIC_REG_GLB_CFG) {
        a->global_cfg = value;
        return 0;
    }
    if (off >= VF_AIC_REG_TARGET && off < 0x4000u) {
        unsigned irq = (off - VF_AIC_REG_TARGET) / 4u;
        if (irq >= a->irq_count || (a->cpu_count < 32u && (value >> a->cpu_count))) return -1;
        a->target[irq] = value;
        return 0;
    }
    if (off >= VF_AIC_REG_SW_BASE && off < 0x4200u) {
        unsigned group = (off - VF_AIC_REG_SW_BASE) / 0x80u;
        return aic_write_bank(a, off, value, group);
    }
    return -1;
}
