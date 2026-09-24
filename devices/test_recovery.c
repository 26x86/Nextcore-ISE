#include "recovery_v1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    vf_recovery_v1 r;
    uint8_t frame[VF_RECOVERY_HEADER_BYTES + 64];
    size_t frame_len = 0;
    uint64_t v = 0;
    const uint8_t *payload;
    size_t payload_len = 0;
    static const uint8_t sample[] = "opaque-27-recovery-input";

    assert(!vf_recovery_init(&r));
    assert(!vf_recovery_read(&r, VF_RECOVERY_MMIO_STATUS, 32, &v) && v == 0);
    assert(!vf_recovery_read(&r, VF_RECOVERY_MMIO_LAST_SEQUENCE, 32, &v) && v == 0);
    assert(!vf_recovery_read(&r, VF_RECOVERY_MMIO_PAYLOAD_LEN, 32, &v) && v == 0);
    assert(!vf_recovery_irq_pending(&r));

    assert(vf_recovery_encode(0, 3, sample, sizeof(sample) - 1u, frame,
                              sizeof(frame), &frame_len) == VF_RECOVERY_OK);
    assert(frame_len == VF_RECOVERY_HEADER_BYTES + (sizeof(sample) - 1u));
    assert(vf_recovery_ingest(&r, frame, frame_len) == VF_RECOVERY_OK);
    assert(vf_recovery_irq_pending(&r));
    assert(!vf_recovery_read(&r, VF_RECOVERY_MMIO_STATUS, 32, &v) &&
           v == VF_RECOVERY_STATUS_READY);
    assert(!vf_recovery_read(&r, VF_RECOVERY_MMIO_LAST_SEQUENCE, 32, &v) && v == 0);
    assert(!vf_recovery_read(&r, VF_RECOVERY_MMIO_PAYLOAD_LEN, 32, &v) &&
           v == (sizeof(sample) - 1u));
    payload = vf_recovery_payload(&r, &payload_len);
    assert(payload && payload_len == sizeof(sample) - 1u);
    assert(memcmp(payload, sample, payload_len) == 0);
    assert(r.signature_verified == 0);

    assert(vf_recovery_ingest(&r, frame, frame_len) == VF_RECOVERY_ERR_SEQUENCE);

    frame[16] ^= 1u;
    assert(vf_recovery_encode(1, 3, sample, sizeof(sample) - 1u, frame,
                              sizeof(frame), &frame_len) == VF_RECOVERY_OK);
    frame[16] ^= 1u;
    assert(vf_recovery_ingest(&r, frame, frame_len) == VF_RECOVERY_ERR_BAD_CHECKSUM);

    assert(vf_recovery_encode(1, 1, sample, sizeof(sample) - 1u, frame,
                              sizeof(frame), &frame_len) == VF_RECOVERY_OK);
    assert(vf_recovery_ingest(&r, frame, frame_len) == VF_RECOVERY_OK);
    assert(vf_recovery_irq_pending(&r));
    assert(!vf_recovery_read(&r, VF_RECOVERY_MMIO_LAST_SEQUENCE, 32, &v) && v == 1);

    assert(!vf_recovery_clear_ready(&r));
    assert(!vf_recovery_irq_pending(&r));
    assert(!vf_recovery_read(&r, VF_RECOVERY_MMIO_STATUS, 32, &v) && v == 0);
    assert(!vf_recovery_read(&r, VF_RECOVERY_MMIO_LAST_SEQUENCE, 32, &v) && v == 1);
    assert(!vf_recovery_read(&r, VF_RECOVERY_MMIO_PAYLOAD_LEN, 32, &v) &&
           v == (sizeof(sample) - 1u));

    assert(vf_recovery_write(&r, VF_RECOVERY_MMIO_STATUS, 32, 1) == -1);
    assert(vf_recovery_read(&r, VF_RECOVERY_MMIO_STATUS, 64, &v) == -1);
    assert(vf_recovery_read(&r, 0x00cu, 32, &v) == -1);
    assert(vf_recovery_ingest(&r, frame, 4) == VF_RECOVERY_ERR_SHORT_FRAME);

    puts("PASS RECOVERY v1 envelope ingest/sequence pending + clear");
    return 0;
}
