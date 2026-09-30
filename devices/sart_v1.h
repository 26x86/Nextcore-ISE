/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_SART_V1_H
#define VENFIRE_SART_V1_H
#include <stdint.h>

#define VF_SART_REG_SIZE        0x8000u
#define VF_SART_NUM_REGIONS     16u
#define VF_SART_VERSION_1       1u
#define VF_SART_VERSION_2       2u
#define VF_SART_VERSION_3       3u

/* Region table bank offsets (bytes). See SART.md for version-specific decode. */
#define VF_SART_BANK_SIZE       0x000u
#define VF_SART_BANK_ADDR       0x040u
#define VF_SART_BANK_SIZE_V3    0x080u

#define VF_SART_REG_SIZE_OFF(i)     (VF_SART_BANK_SIZE + 4u * (i))
#define VF_SART_REG_ADDR_OFF(i)     (VF_SART_BANK_ADDR + 4u * (i))
#define VF_SART_REG_FLAGS_OFF(i)    (VF_SART_BANK_SIZE + 4u * (i))
#define VF_SART_REG_SIZE_V3_OFF(i)  (VF_SART_BANK_SIZE_V3 + 4u * (i))

#define VF_SART_REG_WORDS       (VF_SART_REG_SIZE / 4u)
#define VF_SART_PAGE_SHIFT      12u
#define VF_SART_PAGE_MASK       (~(uint64_t)0xFFFu)
#define VF_SART_MAX_VA_BITS     42u

#define VF_SART_SIZE_MASK_V1    0x0007FFFFu
#define VF_SART_SIZE_MASK_V2    0x00FFFFFFu
#define VF_SART_ADDR_MASK_V12   0x00FFFFFFu
#define VF_SART_ADDR_MASK_V3    0x3FFFFFFFu
#define VF_SART_SIZE_MASK_V3    0x3FFFFFFFu

typedef struct {
    uint32_t version;
    uint32_t generation;
    uint32_t regs[VF_SART_REG_WORDS];
} vf_sart_v1;

int vf_sart_init(vf_sart_v1 *, uint32_t version);
uint32_t vf_sart_generation(const vf_sart_v1 *);
/* Exactly aligned little-endian u32 within [0, VF_SART_REG_SIZE). */
int vf_sart_read(vf_sart_v1 *, uint32_t offset, unsigned width, uint32_t *value);
int vf_sart_write(vf_sart_v1 *, uint32_t offset, unsigned width, uint32_t value);

/* Decode region i into page-granular addr/size and flags (SART.md). */
int vf_sart_region_decode(const vf_sart_v1 *, unsigned region,
                          uint32_t *addr_pages, uint32_t *size_pages,
                          uint32_t *flags);

/* Identity remap stand-in: *pa_out = iova & ~0xFFF.
 * Returns 1 if IOVA page hits a flagged region, 0 on miss (still identity),
 * -1 on invalid args. Optional *region_out receives first matching index. */
int vf_sart_translate(const vf_sart_v1 *, uint64_t iova, uint64_t *pa_out,
                      unsigned *region_out);

#endif
