/* 26x86 first-party SMC v1 key-read stub. See SMC.md for register evidence.
 * Behaviour adapted from qemu-t8030 hw/misc/apple_smc.c (GPL reference only).
 */
#include "apple_smc_v1.h"

#ifdef VF_EFI_BUILD
static void *smc_memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
#define memset smc_memset
#else
#include <string.h>
#endif

static vf_smc_key_entry *smc_find_key(vf_smc_v1 *s, uint32_t key) {
    unsigned i;
    if (!s) return 0;
    for (i = 0; i < s->key_count; i++) {
        if (s->keys[i].key == key) return &s->keys[i];
    }
    return 0;
}

static vf_smc_key_entry *smc_add_key(vf_smc_v1 *s, uint32_t key) {
    vf_smc_key_entry *k;
    if (!s || s->key_count >= VF_SMC_MAX_KEYS) return 0;
    k = &s->keys[s->key_count++];
    memset(k, 0, sizeof(*k));
    k->key = key;
    return k;
}

static void smc_seed_boot_keys(vf_smc_v1 *s) {
    vf_smc_key_entry *k;
    static const uint8_t clkh[8] = {0x00, 0x00, 0x70, 0x80, 0x00, 0x01, 0x19, 0x40};

    k = smc_add_key(s, VF_SMC_KEY_NKEY);
    k->size = 4;
    k->readable = 1;
    k->writable = 0;

    k = smc_add_key(s, VF_SMC_KEY_CLKH);
    k->size = 8;
    k->readable = 1;
    k->writable = 0;
    memcpy(k->data, clkh, 8);

    k = smc_add_key(s, VF_SMC_KEY_RGEN);
    k->size = 1;
    k->readable = 1;
    k->writable = 0;
    k->data[0] = 3;

    k = smc_add_key(s, VF_SMC_KEY_ADC);
    k->size = 4;
    k->readable = 1;
    k->writable = 0;

    k = smc_add_key(s, VF_SMC_KEY_MBSE);
    k->size = 4;
    k->readable = 0;
    k->writable = 1;

    k = smc_add_key(s, VF_SMC_KEY_NESN);
    k->size = 4;
    k->readable = 0;
    k->writable = 1;
}

static uint8_t smc_read_key_data(vf_smc_v1 *s, vf_smc_key_entry *k,
                                 vf_smc_key_resp *r) {
    uint32_t count;

    if (!k->readable) return VF_SMC_KEY_NOT_READABLE;

    if (k->key == VF_SMC_KEY_NKEY) {
        count = (uint32_t)s->key_count;
        memcpy(k->data, &count, 4);
    }

    r->length = k->size;
    if (k->size <= 4) {
        memcpy(r->response, k->data, k->size);
    } else if (k->size <= sizeof(k->data)) {
        memcpy(s->sram, k->data, k->size);
    } else {
        return VF_SMC_KEY_NOT_FOUND;
    }
    return VF_SMC_SUCCESS;
}

int vf_smc_init(vf_smc_v1 *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    s->sram_addr = 0x100000000ull; /* placeholder until DT pin lands */
    smc_seed_boot_keys(s);
    return 0;
}

int vf_smc_read_key(vf_smc_v1 *s, uint32_t key, void *buf, unsigned buf_len,
                    uint8_t *out_len) {
    vf_smc_key_entry *k;
    vf_smc_key_resp r = {0};

    if (!s || !buf || !out_len) return -1;
    k = smc_find_key(s, key);
    if (!k) return -1;
    if (smc_read_key_data(s, k, &r) != VF_SMC_SUCCESS) return -1;
    if (k->size > buf_len) return -1;
    if (k->size <= 4) {
        memcpy(buf, r.response, k->size);
    } else {
        memcpy(buf, s->sram, k->size);
    }
    *out_len = k->size;
    return 0;
}

int vf_smc_handle_msg(vf_smc_v1 *s, uint64_t msg_in, uint64_t *out) {
    vf_smc_key_msg m;
    vf_smc_key_resp r = {0};
    vf_smc_key_entry *k;
    unsigned idx;

    if (!s) return -1;
    memcpy(&m, &msg_in, sizeof(m));

    switch (m.cmd) {
    case VF_SMC_CMD_GET_SRAM_ADDR:
        if (out) *out = s->sram_addr;
        return 0;

    case VF_SMC_CMD_READ_KEY:
    case VF_SMC_CMD_READ_KEY_PAYLOAD:
        k = smc_find_key(s, m.key);
        r.tag = m.tag;
        if (!k) {
            r.status = VF_SMC_KEY_NOT_FOUND;
        } else {
            r.status = smc_read_key_data(s, k, &r);
        }
        break;

    case VF_SMC_CMD_GET_KEY_INFO:
        k = smc_find_key(s, m.key);
        r.tag = m.tag;
        if (!k) {
            r.status = VF_SMC_KEY_NOT_FOUND;
        } else {
            if (k->size + 5u <= VF_SMC_SRAM_SIZE) {
                s->sram[0] = k->size;
                s->sram[1] = (uint8_t)(k->key >> 24);
                s->sram[2] = (uint8_t)(k->key >> 16);
                s->sram[3] = (uint8_t)(k->key >> 8);
                s->sram[4] = (uint8_t)k->key;
                s->sram[5] = 0x04; /* SMC_ATTR_LITTLE_ENDIAN */
            }
            r.status = VF_SMC_SUCCESS;
        }
        break;

    case VF_SMC_CMD_GET_KEY_BY_INDEX:
        r.tag = m.tag;
        idx = m.key;
        if (idx >= s->key_count) {
            r.status = VF_SMC_KEY_INDEX_RANGE;
        } else {
            uint32_t be_key = s->keys[idx].key;
            r.status = VF_SMC_SUCCESS;
            r.response[0] = (uint8_t)(be_key >> 24);
            r.response[1] = (uint8_t)(be_key >> 16);
            r.response[2] = (uint8_t)(be_key >> 8);
            r.response[3] = (uint8_t)be_key;
        }
        break;

    case VF_SMC_CMD_WRITE_KEY:
        k = smc_find_key(s, m.key);
        r.tag = m.tag;
        r.length = m.length;
        if (!k) {
            if (m.length > 8 || s->key_count >= VF_SMC_MAX_KEYS) {
                r.status = VF_SMC_KEY_NOT_FOUND;
            } else {
                k = smc_add_key(s, m.key);
                k->size = m.length;
                k->writable = 1;
                k->readable = 1;
                if (m.length <= sizeof(k->data)) {
                    memcpy(k->data, s->sram, m.length);
                }
                r.status = VF_SMC_SUCCESS;
            }
        } else if (!k->writable) {
            r.status = VF_SMC_KEY_NOT_WRITABLE;
        } else if (m.length <= 8) {
            k->size = m.length;
            memcpy(k->data, s->sram, m.length);
            r.status = VF_SMC_SUCCESS;
        } else {
            r.status = VF_SMC_KEY_NOT_WRITABLE;
        }
        break;

    default:
        r.status = VF_SMC_BAD_COMMAND;
        r.tag = m.tag;
        break;
    }

    if (out) memcpy(out, &r, sizeof(r));
    return 0;
}
