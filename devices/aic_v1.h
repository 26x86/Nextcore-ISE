/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_AIC_V1_H
#define VENFIRE_AIC_V1_H
#include <stdint.h>
#define VF_AIC_MAX_IRQ 1024u
#define VF_AIC_MAX_CPU 32u
/* MMIO offsets aligned with Asahi AIC v1 and qemu-t8030 hw/intc/apple_aic.c banks. */
#define VF_AIC_REG_REV     0x0000u
#define VF_AIC_REG_INFO    0x0004u
#define VF_AIC_REG_GLB_CFG 0x0010u
#define VF_AIC_REG_RST     0x000cu
#define VF_AIC_REG_WHOAMI  0x2000u
#define VF_AIC_REG_EVENT   0x2004u
#define VF_AIC_REG_IPI_SET 0x2008u
#define VF_AIC_REG_TARGET  0x3000u
#define VF_AIC_REG_SW_BASE 0x4000u
#define VF_AIC_REG_CPU_BASE 0x5000u
#define VF_AIC_EVENT_EXT   0x10000u
typedef struct {
    uint32_t irq_count, cpu_count, global_cfg;
    uint32_t target[VF_AIC_MAX_IRQ];
    uint32_t level[32], software[32], masked[32];
} vf_aic_v1;
/* Single scheduler owner: serialize all MMIO and device line changes. */
int vf_aic_init(vf_aic_v1 *, unsigned irq_count, unsigned cpu_count);
int vf_aic_set_line(vf_aic_v1 *, unsigned irq, int high);
int vf_aic_pending(const vf_aic_v1 *, unsigned cpu);
/* Accesses are exactly aligned little-endian u32. Unknown registers fail. */
int vf_aic_read(vf_aic_v1 *, unsigned cpu, uint32_t offset, unsigned width, uint32_t *value);
int vf_aic_write(vf_aic_v1 *, unsigned cpu, uint32_t offset, unsigned width, uint32_t value);
#endif
