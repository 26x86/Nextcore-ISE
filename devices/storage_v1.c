/* 26x86 first-party graph-local storage stub. See STORAGE.md.
 * Matches Rust M1Storage attach/generation; MMIO is read-only from guest.
 * IRQ pending while attached (level) → bridge STORAGE_SOURCE / AIC line 1.
 * Not ANS/NVMe vendor MMIO or a real block backend.
 */
#include "storage_v1.h"

#ifdef VF_EFI_BUILD
static void *storage_memset(void *dst, int c, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
#define memset(d, c, n) storage_memset((d), (c), (unsigned)(n))
#else
#include <string.h>
#endif

int vf_storage_init(vf_storage_v1 *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    return 0;
}

int vf_storage_attach(vf_storage_v1 *s, uint64_t block_count, int read_only) {
    uint64_t bytes;

    if (!s) return -1;
    if (block_count == 0) return -1;
    bytes = block_count * (uint64_t)VF_STORAGE_BLOCK_BYTES;
    if (bytes / (uint64_t)VF_STORAGE_BLOCK_BYTES != block_count) return -1;

    s->attached = 1;
    s->read_only = read_only ? 1 : 0;
    s->block_count = block_count;
    if (s->generation < UINT32_MAX)
        s->generation = s->generation + 1u;
    return 0;
}

int vf_storage_detach(vf_storage_v1 *s) {
    if (!s) return -1;
    s->attached = 0;
    s->read_only = 0;
    s->block_count = 0;
    /* generation is sticky across detach (observable attach epoch). */
    return 0;
}

int vf_storage_irq_pending(const vf_storage_v1 *s) {
    return s && s->attached ? 1 : 0;
}

int vf_storage_read(vf_storage_v1 *s, uint32_t offset, unsigned width_bits,
                    uint64_t *value) {
    if (!s || !value) return -1;
    if (width_bits != 64) return -1;
    switch (offset) {
    case VF_STORAGE_MMIO_STATUS:
        *value = (s->attached ? VF_STORAGE_STATUS_ATTACHED : 0u) |
                 (s->read_only ? VF_STORAGE_STATUS_READ_ONLY : 0u);
        return 0;
    case VF_STORAGE_MMIO_BLOCK_COUNT:
        *value = s->block_count;
        return 0;
    case VF_STORAGE_MMIO_GENERATION:
        *value = s->generation;
        return 0;
    default:
        return -1;
    }
}

int vf_storage_write(vf_storage_v1 *s, uint32_t offset, unsigned width_bits,
                     uint64_t value) {
    (void)s;
    (void)offset;
    (void)width_bits;
    (void)value;
    /* Match Rust M1LogicalWindow::Storage — guest MMIO is read-only. */
    return -1;
}
