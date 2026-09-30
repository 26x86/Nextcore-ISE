/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_APPLE_GPIO_V1_H
#define VENFIRE_APPLE_GPIO_V1_H
#include <stdint.h>

/* Apple GPIO MMIO (T8030 topology reference). See GPIO.md. */
#define VF_GPIO_MAX_PIN_NR     512u
#define VF_GPIO_MAX_INT_GRP_NR 7u
#define VF_GPIO_INT_WORDS      16u

#define VF_GPIO_REG_CFG(n)     (0x000u + (uint32_t)(n) * 4u)
#define VF_GPIO_REG_INT(g, n)  (0x800u + (uint32_t)(g) * 0x40u + (((n) + 31u) >> 5u) * 4u)
#define VF_GPIO_REG_NPL_IN_EN  0xC48u

#define VF_GPIO2PIN(gpio)        ((unsigned)(gpio) & 7u)
#define VF_GPIO2PAD(gpio)        (((unsigned)(gpio) >> 8) & 0xFFu)
#define VF_GPIO2CONTROLLER(gpio) (((unsigned)(gpio) >> 24) & 0xFFu)

#define VF_GPIO_DATA_0 (0u << 0)
#define VF_GPIO_DATA_1 (1u << 0)

#define VF_GPIO_CFG_GP_IN      (0u << 1)
#define VF_GPIO_CFG_GP_OUT     (1u << 1)
#define VF_GPIO_CFG_INT_LVL_HI (2u << 1)
#define VF_GPIO_CFG_INT_LVL_LO (3u << 1)
#define VF_GPIO_CFG_INT_EDG_RIS (4u << 1)
#define VF_GPIO_CFG_INT_EDG_FAL (5u << 1)
#define VF_GPIO_CFG_INT_EDG_ANY (6u << 1)
#define VF_GPIO_CFG_DISABLE    (7u << 1)
#define VF_GPIO_CFG_MASK       (7u << 1)

#define VF_GPIO_FUNC_SHIFT 5u
#define VF_GPIO_FUNC_GPIO  (0u << VF_GPIO_FUNC_SHIFT)
#define VF_GPIO_FUNC_ALT0  (1u << VF_GPIO_FUNC_SHIFT)
#define VF_GPIO_FUNC_ALT1  (2u << VF_GPIO_FUNC_SHIFT)
#define VF_GPIO_FUNC_ALT2  (3u << VF_GPIO_FUNC_SHIFT)
#define VF_GPIO_FUNC_MASK  (3u << VF_GPIO_FUNC_SHIFT)

#define VF_GPIO_INPUT_ENABLE (1u << 9)

#define VF_GPIO_INTR_GRP_SHIFT 16u
#define VF_GPIO_INT_MASKED     (7u << VF_GPIO_INTR_GRP_SHIFT)

#define VF_GPIO_CFG_DISABLED \
    (VF_GPIO_FUNC_GPIO | VF_GPIO_CFG_DISABLE | VF_GPIO_INT_MASKED)
#define VF_GPIO_CFG_IN \
    (VF_GPIO_INPUT_ENABLE | VF_GPIO_FUNC_GPIO | VF_GPIO_CFG_GP_IN | VF_GPIO_INT_MASKED)
#define VF_GPIO_CFG_OUT \
    (VF_GPIO_INPUT_ENABLE | VF_GPIO_FUNC_GPIO | VF_GPIO_CFG_GP_OUT | VF_GPIO_INT_MASKED)
#define VF_GPIO_CFG_OUT_0 \
    (VF_GPIO_INPUT_ENABLE | VF_GPIO_FUNC_GPIO | VF_GPIO_CFG_GP_OUT | VF_GPIO_DATA_0 | \
     VF_GPIO_INT_MASKED)
#define VF_GPIO_CFG_OUT_1 \
    (VF_GPIO_INPUT_ENABLE | VF_GPIO_FUNC_GPIO | VF_GPIO_CFG_GP_OUT | VF_GPIO_DATA_1 | \
     VF_GPIO_INT_MASKED)

typedef void (*vf_gpio_out_fn)(void *ctx, unsigned pin, int level);
typedef void (*vf_gpio_irq_fn)(void *ctx, unsigned group, int asserted);

typedef struct {
    unsigned npins;
    unsigned nirqgrps;
    uint32_t phandle;
    uint32_t gpio_cfg[VF_GPIO_MAX_PIN_NR];
    uint32_t int_cfg[VF_GPIO_MAX_INT_GRP_NR][VF_GPIO_INT_WORDS];
    uint32_t in[VF_GPIO_INT_WORDS];
    uint32_t old_in[VF_GPIO_INT_WORDS];
    uint32_t npl;
    vf_gpio_out_fn out;
    void *out_ctx;
    vf_gpio_irq_fn irq;
    void *irq_ctx;
} vf_apple_gpio_v1;

int vf_apple_gpio_init(vf_apple_gpio_v1 *, unsigned npins, unsigned nirqgrps,
                       uint32_t phandle, vf_gpio_out_fn out, void *out_ctx,
                       vf_gpio_irq_fn irq, void *irq_ctx);
int vf_apple_gpio_set_in(vf_apple_gpio_v1 *, unsigned pin, int level);
int vf_apple_gpio_irq_asserted(const vf_apple_gpio_v1 *, unsigned group);
/* Exactly aligned little-endian u32. Unknown MMIO reads return zero. */
int vf_apple_gpio_read(vf_apple_gpio_v1 *, uint32_t offset, unsigned width, uint32_t *value);
int vf_apple_gpio_write(vf_apple_gpio_v1 *, uint32_t offset, unsigned width, uint32_t value);

#endif
