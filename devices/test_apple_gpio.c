#include "apple_gpio_v1.h"
#include <assert.h>
#include <stdio.h>

static unsigned out_pin;
static int out_level;
static unsigned irq_group;
static int irq_level;

static void capture_out(void *ctx, unsigned pin, int level) {
    (void)ctx;
    out_pin = pin;
    out_level = level;
}

static void capture_irq(void *ctx, unsigned group, int asserted) {
    (void)ctx;
    irq_group = group;
    irq_level = asserted;
}

int main(void) {
    vf_apple_gpio_v1 g;
    uint32_t v = 0;
    uint32_t lvl_hi =
        VF_GPIO_INPUT_ENABLE | VF_GPIO_FUNC_GPIO | VF_GPIO_CFG_INT_LVL_HI | (0u << VF_GPIO_INTR_GRP_SHIFT);

    assert(!vf_apple_gpio_init(&g, 32, 2, 0x42, capture_out, NULL, capture_irq, NULL));

    assert(!vf_apple_gpio_read(&g, VF_GPIO_REG_CFG(0), 4, &v));
    assert(v == VF_GPIO_CFG_DISABLED);
    assert(!vf_apple_gpio_read(&g, VF_GPIO_REG_NPL_IN_EN, 4, &v) && v == 0);
    assert(!vf_apple_gpio_read(&g, 0x1000u, 4, &v) && v == 0);

    assert(!vf_apple_gpio_write(&g, VF_GPIO_REG_CFG(3), 4, VF_GPIO_CFG_OUT_1));
    assert(out_pin == 3 && out_level == 1);
    assert(!vf_apple_gpio_read(&g, VF_GPIO_REG_CFG(3), 4, &v));
    assert(v == VF_GPIO_CFG_OUT_1);

    assert(!vf_apple_gpio_write(&g, VF_GPIO_REG_CFG(5), 4, VF_GPIO_CFG_IN));
    assert(!vf_apple_gpio_set_in(&g, 5, 1));
    assert(!vf_apple_gpio_read(&g, VF_GPIO_REG_CFG(5), 4, &v));
    assert(v & VF_GPIO_DATA_1);

    assert(!vf_apple_gpio_write(&g, VF_GPIO_REG_CFG(7), 4, lvl_hi));
    assert(!vf_apple_gpio_set_in(&g, 7, 0));
    assert(!vf_apple_gpio_irq_asserted(&g, 0));
    assert(!vf_apple_gpio_set_in(&g, 7, 1));
    assert(vf_apple_gpio_irq_asserted(&g, 0));
    assert(!vf_apple_gpio_read(&g, VF_GPIO_REG_INT(0, 7), 4, &v));
    assert(v & (1u << 7));
    assert(!vf_apple_gpio_write(&g, VF_GPIO_REG_INT(0, 7), 4, 1u << 7));
    assert(!vf_apple_gpio_irq_asserted(&g, 0));

    assert(vf_apple_gpio_read(&g, VF_GPIO_REG_CFG(0), 2, &v) == -1);
    assert(vf_apple_gpio_read(&g, VF_GPIO_REG_CFG(0) + 2u, 4, &v) == -1);
    assert(vf_apple_gpio_write(&g, VF_GPIO_REG_NPL_IN_EN, 4, 1) == -1);

    assert(VF_GPIO2PIN(0x01020304u) == 4);
    assert(VF_GPIO2PAD(0x00001234u) == 0x12);
    assert(VF_GPIO2CONTROLLER(0x03000000u) == 3);

    puts("PASS Apple GPIO v1: reset CFG_DISABLED, GP_OUT/GP_IN, level IRQ W1C, bounds");
    return 0;
}
