/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_RECOVERY_V1_H
#define VENFIRE_RECOVERY_V1_H
#include <stddef.h>
#include <stdint.h>

/*
 * Graph-local M1 recovery envelope stub.
 * Aligns with Rust M1RecoveryTransport / RECOVERY_SOURCE (3) MMIO at
 * M1_LOGICAL_RECOVERY_BASE. Opaque CRC/sequence transport only — not Apple
 * signature verification, not RecoveryOS media, not a DT claim.
 */

#define VF_RECOVERY_MAX_PAYLOAD        1024u
#define VF_RECOVERY_HEADER_BYTES       20u
#define VF_RECOVERY_MAGIC              UINT32_C(0x31524656) /* "VFR1" LE */

/* Graph-local MMIO offsets (match Rust M1RecoveryTransport read path). */
#define VF_RECOVERY_MMIO_STATUS        0x000u
#define VF_RECOVERY_MMIO_LAST_SEQUENCE 0x004u
#define VF_RECOVERY_MMIO_PAYLOAD_LEN   0x008u

#define VF_RECOVERY_STATUS_READY              (1u << 0)
#define VF_RECOVERY_STATUS_SIGNATURE_VERIFIED (1u << 1)

#define VF_RECOVERY_OK                 0
#define VF_RECOVERY_ERR_SHORT_FRAME   (-2)
#define VF_RECOVERY_ERR_BAD_MAGIC     (-3)
#define VF_RECOVERY_ERR_BAD_LENGTH    (-4)
#define VF_RECOVERY_ERR_BAD_CHECKSUM  (-5)
#define VF_RECOVERY_ERR_SEQUENCE      (-6)
#define VF_RECOVERY_ERR_PAYLOAD_LARGE (-7)
#define VF_RECOVERY_ERR_OUTPUT_SMALL  (-8)

typedef struct {
    uint32_t expected_sequence;
    uint32_t last_sequence;
    uint32_t flags;
    uint8_t payload[VF_RECOVERY_MAX_PAYLOAD];
    uint32_t payload_len;
    int ready;
    int signature_verified;
} vf_recovery_v1;

int vf_recovery_init(vf_recovery_v1 *r);
int vf_recovery_encode(uint32_t sequence, uint32_t flags,
                       const uint8_t *payload, size_t payload_len,
                       uint8_t *output, size_t output_cap, size_t *out_len);
int vf_recovery_ingest(vf_recovery_v1 *r, const uint8_t *frame, size_t frame_len);
int vf_recovery_clear_ready(vf_recovery_v1 *r);
int vf_recovery_irq_pending(const vf_recovery_v1 *r);
const uint8_t *vf_recovery_payload(const vf_recovery_v1 *r, size_t *len_out);
int vf_recovery_read(vf_recovery_v1 *r, uint32_t offset, unsigned width_bits,
                     uint64_t *value);
int vf_recovery_write(vf_recovery_v1 *r, uint32_t offset, unsigned width_bits,
                      uint64_t value);

#endif
