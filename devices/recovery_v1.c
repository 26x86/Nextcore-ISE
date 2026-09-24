/* 26x86 first-party graph-local recovery envelope stub. See RECOVERY.md.
 * Matches Rust M1RecoveryTransport CRC/sequence ingest; MMIO is read-only.
 * IRQ pending while ready (level) → bridge RECOVERY_SOURCE / AIC line 3.
 * signature_verified stays false until an external trust verifier supplies
 * evidence. Not Apple RecoveryOS media or signature validation.
 */
#include "recovery_v1.h"

#ifdef VF_EFI_BUILD
static void *recovery_memset(void *dst, int c, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
static void *recovery_memcpy(void *dst, const void *src, unsigned n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
#define memset(d, c, n) recovery_memset((d), (c), (unsigned)(n))
#define memcpy(d, s, n) recovery_memcpy((d), (s), (unsigned)(n))
#else
#include <string.h>
#endif

static uint32_t recovery_read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void recovery_put_u32_le(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint32_t recovery_crc32(const uint8_t *bytes, size_t len) {
    uint32_t crc = 0xffffffffu;
    size_t i;
    unsigned bit;

    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)bytes[i];
        for (bit = 0; bit < 8u; bit++) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

int vf_recovery_init(vf_recovery_v1 *r) {
    if (!r) return -1;
    memset(r, 0, sizeof(*r));
    return 0;
}

int vf_recovery_encode(uint32_t sequence, uint32_t flags,
                       const uint8_t *payload, size_t payload_len,
                       uint8_t *output, size_t output_cap, size_t *out_len) {
    size_t total;

    if (!output || !out_len) return VF_RECOVERY_ERR_OUTPUT_SMALL;
    if (payload_len > VF_RECOVERY_MAX_PAYLOAD) return VF_RECOVERY_ERR_PAYLOAD_LARGE;
    if (payload_len > 0 && !payload) return VF_RECOVERY_ERR_SHORT_FRAME;
    total = VF_RECOVERY_HEADER_BYTES + payload_len;
    if (output_cap < total) return VF_RECOVERY_ERR_OUTPUT_SMALL;

    recovery_put_u32_le(output + 0, VF_RECOVERY_MAGIC);
    recovery_put_u32_le(output + 4, sequence);
    recovery_put_u32_le(output + 8, flags);
    recovery_put_u32_le(output + 12, (uint32_t)payload_len);
    recovery_put_u32_le(output + 16,
                        recovery_crc32(payload_len ? payload : (const uint8_t *)"",
                                       payload_len));
    if (payload_len)
        memcpy(output + VF_RECOVERY_HEADER_BYTES, payload, (unsigned)payload_len);
    *out_len = total;
    return VF_RECOVERY_OK;
}

int vf_recovery_ingest(vf_recovery_v1 *r, const uint8_t *frame, size_t frame_len) {
    uint32_t sequence;
    uint32_t flags;
    uint32_t payload_len_u32;
    size_t payload_len;
    size_t total;
    const uint8_t *payload;

    if (!r || !frame) return VF_RECOVERY_ERR_SHORT_FRAME;
    if (frame_len < VF_RECOVERY_HEADER_BYTES) return VF_RECOVERY_ERR_SHORT_FRAME;
    if (recovery_read_u32_le(frame + 0) != VF_RECOVERY_MAGIC)
        return VF_RECOVERY_ERR_BAD_MAGIC;

    sequence = recovery_read_u32_le(frame + 4);
    flags = recovery_read_u32_le(frame + 8);
    payload_len_u32 = recovery_read_u32_le(frame + 12);
    if (payload_len_u32 > VF_RECOVERY_MAX_PAYLOAD)
        return VF_RECOVERY_ERR_PAYLOAD_LARGE;
    payload_len = (size_t)payload_len_u32;
    total = VF_RECOVERY_HEADER_BYTES + payload_len;
    if (frame_len != total) return VF_RECOVERY_ERR_BAD_LENGTH;

    payload = frame + VF_RECOVERY_HEADER_BYTES;
    if (recovery_read_u32_le(frame + 16) != recovery_crc32(payload, payload_len))
        return VF_RECOVERY_ERR_BAD_CHECKSUM;
    if (sequence != r->expected_sequence) return VF_RECOVERY_ERR_SEQUENCE;

    if (payload_len)
        memcpy(r->payload, payload, (unsigned)payload_len);
    r->payload_len = (uint32_t)payload_len;
    r->flags = flags;
    r->last_sequence = sequence;
    r->expected_sequence = r->expected_sequence + 1u;
    r->ready = 1;
    r->signature_verified = 0;
    return VF_RECOVERY_OK;
}

int vf_recovery_clear_ready(vf_recovery_v1 *r) {
    if (!r) return -1;
    r->ready = 0;
    /* last_sequence / payload sticky (observable ingest epoch), like storage
     * generation across detach. expected_sequence retained for next ingest. */
    return 0;
}

int vf_recovery_irq_pending(const vf_recovery_v1 *r) {
    return r && r->ready ? 1 : 0;
}

const uint8_t *vf_recovery_payload(const vf_recovery_v1 *r, size_t *len_out) {
    if (!r) {
        if (len_out) *len_out = 0;
        return 0;
    }
    if (len_out) *len_out = r->payload_len;
    return r->payload;
}

int vf_recovery_read(vf_recovery_v1 *r, uint32_t offset, unsigned width_bits,
                     uint64_t *value) {
    if (!r || !value) return -1;
    if (width_bits != 32) return -1;
    switch (offset) {
    case VF_RECOVERY_MMIO_STATUS:
        *value = (r->ready ? VF_RECOVERY_STATUS_READY : 0u)
               | (r->signature_verified ? VF_RECOVERY_STATUS_SIGNATURE_VERIFIED : 0u);
        return 0;
    case VF_RECOVERY_MMIO_LAST_SEQUENCE:
        *value = r->last_sequence;
        return 0;
    case VF_RECOVERY_MMIO_PAYLOAD_LEN:
        *value = r->payload_len;
        return 0;
    default:
        return -1;
    }
}

int vf_recovery_write(vf_recovery_v1 *r, uint32_t offset, unsigned width_bits,
                      uint64_t value) {
    (void)r;
    (void)offset;
    (void)width_bits;
    (void)value;
    /* Match Rust M1LogicalWindow::Recovery — guest MMIO is read-only. */
    return -1;
}
