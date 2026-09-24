/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_STORAGE_V1_H
#define VENFIRE_STORAGE_V1_H
#include <stdint.h>

/*
 * Graph-local M1 storage window stub.
 * Aligns with Rust M1Storage / STORAGE_SOURCE (1) MMIO at M1_LOGICAL_STORAGE_BASE.
 * Not Apple ANS/NVMe vendor MMIO, not a block backend, not a DT storage claim.
 */

#define VF_STORAGE_BLOCK_BYTES     4096u

/* Graph-local MMIO offsets (match Rust M1Storage read path). */
#define VF_STORAGE_MMIO_STATUS     0x000u
#define VF_STORAGE_MMIO_BLOCK_COUNT 0x008u
#define VF_STORAGE_MMIO_GENERATION 0x010u

#define VF_STORAGE_STATUS_ATTACHED (1u << 0)
#define VF_STORAGE_STATUS_READ_ONLY (1u << 1)

typedef struct {
    int attached;
    int read_only;
    uint64_t block_count;
    uint32_t generation;
} vf_storage_v1;

int vf_storage_init(vf_storage_v1 *s);
int vf_storage_attach(vf_storage_v1 *s, uint64_t block_count, int read_only);
int vf_storage_detach(vf_storage_v1 *s);
int vf_storage_irq_pending(const vf_storage_v1 *s);
int vf_storage_read(vf_storage_v1 *s, uint32_t offset, unsigned width_bits,
                    uint64_t *value);
int vf_storage_write(vf_storage_v1 *s, uint32_t offset, unsigned width_bits,
                     uint64_t value);

#endif
