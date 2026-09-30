/* 26x86 first-party Apple GPIO v1 MMIO stub. See sandbox/devices/GPIO.md.
 * Behaviour adapted from qemu-t8030 hw/gpio/apple_gpio.c (GPL reference only).
 */
#include "apple_gpio_v1.h"

static int gpio_width_ok(uint32_t off, unsigned width) {
    return width == 4 && !(off & 3u);
}

static int gpio_test_bit(const uint32_t *words, unsigned pin) {
    return (int)((words[pin >> 5] >> (pin & 31u)) & 1u);
}

static void gpio_set_bit(uint32_t *words, unsigned pin) {
    words[pin >> 5] |= UINT32_C(1) << (pin & 31u);
}

static void gpio_clear_bit(uint32_t *words, unsigned pin) {
    words[pin >> 5] &= ~(UINT32_C(1) << (pin & 31u));
}

static unsigned gpio_int_word(unsigned pin) { return (pin + 31u) >> 5; }

static void gpio_int_set_bit(uint32_t *words, unsigned pin) {
    words[gpio_int_word(pin)] |= UINT32_C(1) << (pin & 31u);
}

static void gpio_int_clear_bit(uint32_t *words, unsigned pin) {
    words[gpio_int_word(pin)] &= ~(UINT32_C(1) << (pin & 31u));
}

static unsigned gpio_int_words(unsigned npins) {
    if (!npins) return 0;
    return gpio_int_word(npins - 1u) + 1u;
}

static int gpio_group_active(const vf_apple_gpio_v1 *g, unsigned group) {
    unsigned words = gpio_int_words(g->npins);
    unsigned w;
    if (!g || group >= g->nirqgrps) return 0;
    for (w = 0; w < words; w++) {
        if (g->int_cfg[group][w]) return 1;
    }
    return 0;
}

static void gpio_raise_irq(vf_apple_gpio_v1 *g, unsigned group) {
    if (g && g->irq) g->irq(g->irq_ctx, group, gpio_group_active(g, group));
}

static void gpio_drive_out(vf_apple_gpio_v1 *g, unsigned pin, uint32_t cfg) {
    int level = 1;
    if (!g || !g->out || pin >= g->npins) return;
    if (cfg & VF_GPIO_FUNC_MASK) {
        if ((cfg & VF_GPIO_FUNC_MASK) == VF_GPIO_FUNC_ALT0) level = 1;
    } else if ((cfg & VF_GPIO_CFG_MASK) == VF_GPIO_CFG_GP_OUT) {
        level = (cfg & VF_GPIO_DATA_1) ? 1 : 0;
    }
    g->out(g->out_ctx, pin, level);
}

static void gpio_update_level_pending(vf_apple_gpio_v1 *g, unsigned pin, uint32_t cfg) {
    unsigned irqgrp;
    if ((cfg & VF_GPIO_INT_MASKED) == VF_GPIO_INT_MASKED) return;
    irqgrp = (cfg & VF_GPIO_INT_MASKED) >> VF_GPIO_INTR_GRP_SHIFT;
    if (irqgrp >= g->nirqgrps) return;
    gpio_int_clear_bit(g->int_cfg[irqgrp], pin);
    switch (cfg & VF_GPIO_CFG_MASK) {
    case VF_GPIO_CFG_INT_LVL_HI:
        if (gpio_test_bit(g->in, pin)) gpio_int_set_bit(g->int_cfg[irqgrp], pin);
        break;
    case VF_GPIO_CFG_INT_LVL_LO:
        if (!gpio_test_bit(g->in, pin)) gpio_int_set_bit(g->int_cfg[irqgrp], pin);
        break;
    default:
        break;
    }
    gpio_raise_irq(g, irqgrp);
}

static void gpio_update_pincfg(vf_apple_gpio_v1 *g, unsigned pin, uint32_t value) {
    if (pin >= g->npins) return;
    gpio_update_level_pending(g, pin, value);
    g->gpio_cfg[pin] = value;
    gpio_drive_out(g, pin, value);
}

static void gpio_apply_input_irq(vf_apple_gpio_v1 *g, unsigned pin, int level) {
    uint32_t cfg;
    unsigned irqgrp;
    int irqgrp_set = -1;

    if (pin >= g->npins) return;
    if (level) gpio_set_bit(g->in, pin);
    else gpio_clear_bit(g->in, pin);

    cfg = g->gpio_cfg[pin];
    if ((cfg & VF_GPIO_INT_MASKED) != VF_GPIO_INT_MASKED) {
        irqgrp = (cfg & VF_GPIO_INT_MASKED) >> VF_GPIO_INTR_GRP_SHIFT;
        if (irqgrp < g->nirqgrps) {
            irqgrp_set = (int)irqgrp;
            switch (cfg & VF_GPIO_CFG_MASK) {
            case VF_GPIO_CFG_GP_IN:
            case VF_GPIO_CFG_GP_OUT:
                break;
            case VF_GPIO_CFG_INT_LVL_HI:
                if (level) gpio_int_set_bit(g->int_cfg[irqgrp], pin);
                break;
            case VF_GPIO_CFG_INT_LVL_LO:
                if (!level) gpio_int_set_bit(g->int_cfg[irqgrp], pin);
                break;
            case VF_GPIO_CFG_INT_EDG_RIS:
                if (!gpio_test_bit(g->old_in, pin) && level)
                    gpio_int_set_bit(g->int_cfg[irqgrp], pin);
                break;
            case VF_GPIO_CFG_INT_EDG_FAL:
                if (gpio_test_bit(g->old_in, pin) && !level)
                    gpio_int_set_bit(g->int_cfg[irqgrp], pin);
                break;
            case VF_GPIO_CFG_INT_EDG_ANY:
                if (gpio_test_bit(g->old_in, pin) != level)
                    gpio_int_set_bit(g->int_cfg[irqgrp], pin);
                break;
            default:
                break;
            }
        }
    }

    g->old_in[pin >> 5] = g->in[pin >> 5];
    if (irqgrp_set >= 0) gpio_raise_irq(g, (unsigned)irqgrp_set);
}

static int gpio_cfg_index(uint32_t off, unsigned *pin) {
    if (off < VF_GPIO_REG_CFG(0) || off > VF_GPIO_REG_CFG(VF_GPIO_MAX_PIN_NR - 1u))
        return -1;
    *pin = (off - VF_GPIO_REG_CFG(0)) >> 2;
    return 0;
}

static int gpio_int_index(uint32_t off, unsigned *group, unsigned *word) {
    uint32_t rel;
    if (off < VF_GPIO_REG_INT(0, 0) || off > VF_GPIO_REG_INT(VF_GPIO_MAX_INT_GRP_NR, VF_GPIO_MAX_PIN_NR - 1u))
        return -1;
    rel = off - VF_GPIO_REG_INT(0, 0);
    *group = rel >> 6;
    *word = (rel & 0x3fu) >> 2;
    return 0;
}

int vf_apple_gpio_init(vf_apple_gpio_v1 *g, unsigned npins, unsigned nirqgrps,
                       uint32_t phandle, vf_gpio_out_fn out, void *out_ctx,
                       vf_gpio_irq_fn irq, void *irq_ctx) {
    unsigned i;
    unsigned grp;
    unsigned words;
    if (!g || !npins || npins >= VF_GPIO_MAX_PIN_NR || !nirqgrps ||
        nirqgrps > VF_GPIO_MAX_INT_GRP_NR)
        return -1;
    g->npins = npins;
    g->nirqgrps = nirqgrps;
    g->phandle = phandle;
    g->out = out;
    g->out_ctx = out_ctx;
    g->irq = irq;
    g->irq_ctx = irq_ctx;
    g->npl = 0;
    for (i = 0; i < npins; i++) g->gpio_cfg[i] = VF_GPIO_CFG_DISABLED;
    words = gpio_int_words(npins);
    for (grp = 0; grp < nirqgrps; grp++) {
        unsigned w;
        for (w = 0; w < words && w < VF_GPIO_INT_WORDS; w++) g->int_cfg[grp][w] = 0;
    }
    for (i = 0; i < VF_GPIO_INT_WORDS; i++) {
        g->in[i] = 0;
        g->old_in[i] = 0;
    }
    return 0;
}

int vf_apple_gpio_set_in(vf_apple_gpio_v1 *g, unsigned pin, int level) {
    if (!g || pin >= g->npins) return -1;
    gpio_apply_input_irq(g, pin, level != 0);
    return 0;
}

int vf_apple_gpio_irq_asserted(const vf_apple_gpio_v1 *g, unsigned group) {
    return g && group < g->nirqgrps && gpio_group_active(g, group);
}

int vf_apple_gpio_read(vf_apple_gpio_v1 *g, uint32_t off, unsigned width, uint32_t *value) {
    unsigned pin;
    unsigned group;
    unsigned word;
    uint32_t cfg;

    if (!g || !value || !gpio_width_ok(off, width)) return -1;

    if (!gpio_cfg_index(off, &pin)) {
        if (pin >= g->npins) {
            *value = 0;
            return 0;
        }
        cfg = g->gpio_cfg[pin];
        if (((cfg & VF_GPIO_FUNC_MASK) == VF_GPIO_FUNC_GPIO) &&
            ((cfg & VF_GPIO_CFG_MASK) == VF_GPIO_CFG_GP_IN)) {
            cfg &= ~VF_GPIO_DATA_1;
            if (gpio_test_bit(g->in, pin)) cfg |= VF_GPIO_DATA_1;
        }
        *value = cfg;
        return 0;
    }

    if (!gpio_int_index(off, &group, &word)) {
        if (group >= g->nirqgrps || word >= VF_GPIO_INT_WORDS) {
            *value = 0;
            return 0;
        }
        *value = g->int_cfg[group][word];
        return 0;
    }

    if (off == VF_GPIO_REG_NPL_IN_EN) {
        *value = g->npl;
        return 0;
    }

    *value = 0;
    return 0;
}

int vf_apple_gpio_write(vf_apple_gpio_v1 *g, uint32_t off, unsigned width, uint32_t value) {
    unsigned pin;
    unsigned group;
    unsigned word;

    if (!g || !gpio_width_ok(off, width)) return -1;

    if (!gpio_cfg_index(off, &pin)) {
        if (pin >= g->npins) return 0;
        gpio_update_pincfg(g, pin, value);
        return 0;
    }

    if (!gpio_int_index(off, &group, &word)) {
        if (group >= g->nirqgrps || word >= VF_GPIO_INT_WORDS) return 0;
        g->int_cfg[group][word] &= ~value;
        gpio_raise_irq(g, group);
        return 0;
    }

    if (off == VF_GPIO_REG_NPL_IN_EN) return -1; /* NPL write path deferred */

    return -1;
}
