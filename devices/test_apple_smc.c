#include "apple_smc_v1.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t pack_msg(uint8_t cmd, uint8_t tag, uint8_t len, uint8_t plen,
                         uint32_t key) {
    vf_smc_key_msg m = {cmd, tag, len, plen, key};
    uint64_t raw = 0;
    memcpy(&raw, &m, sizeof(m));
    return raw;
}

int main(void) {
    vf_smc_v1 s;
    uint64_t out = 0;
    uint8_t buf[8];
    uint8_t len = 0;
    uint32_t count = 0;

    assert(!vf_smc_init(&s));
    assert(s.key_count == 6);

    /* #KEY returns live key count */
    assert(!vf_smc_read_key(&s, VF_SMC_KEY_NKEY, &count, sizeof(count), &len));
    assert(len == 4 && count == 6);

    /* CLKH boot default */
    assert(!vf_smc_read_key(&s, VF_SMC_KEY_CLKH, buf, sizeof(buf), &len));
    assert(len == 8);
    assert(buf[2] == 0x70 && buf[3] == 0x80 && buf[6] == 0x19 && buf[7] == 0x40);

    /* RGEN = 3 */
    assert(!vf_smc_read_key(&s, VF_SMC_KEY_RGEN, buf, sizeof(buf), &len));
    assert(len == 1 && buf[0] == 3);

    /* MBSE write-only */
    assert(vf_smc_read_key(&s, VF_SMC_KEY_MBSE, buf, sizeof(buf), &len) == -1);

    /* READ_KEY via mailbox framing */
    out = 0;
    assert(!vf_smc_handle_msg(&s, pack_msg(VF_SMC_CMD_READ_KEY, 0x42, 0, 4,
                                           VF_SMC_KEY_ADC), &out));
    {
        vf_smc_key_resp r;
        memcpy(&r, &out, sizeof(r));
        assert(r.status == VF_SMC_SUCCESS);
        assert(r.tag == 0x42);
        assert(r.length == 4);
        assert(r.response[0] == 0 && r.response[1] == 0 &&
               r.response[2] == 0 && r.response[3] == 0);
    }

    /* GET_KEY_BY_INDEX */
    out = 0;
    assert(!vf_smc_handle_msg(&s, pack_msg(VF_SMC_CMD_GET_KEY_BY_INDEX, 1, 0, 0,
                                           1), &out));
    {
        vf_smc_key_resp r;
        memcpy(&r, &out, sizeof(r));
        assert(r.status == VF_SMC_SUCCESS);
        assert(r.response[0] == 'C' && r.response[1] == 'L' &&
               r.response[2] == 'K' && r.response[3] == 'H');
    }

    /* GET_SRAM_ADDR */
    out = 0;
    assert(!vf_smc_handle_msg(&s, pack_msg(VF_SMC_CMD_GET_SRAM_ADDR, 0, 0, 0, 0),
                               &out));
    assert(out == s.sram_addr);

    /* unknown key */
    out = 0;
    assert(!vf_smc_handle_msg(&s, pack_msg(VF_SMC_CMD_READ_KEY, 0, 0, 4,
                                           0xdeadbeefu), &out));
    {
        vf_smc_key_resp r;
        memcpy(&r, &out, sizeof(r));
        assert(r.status == VF_SMC_KEY_NOT_FOUND);
    }

    puts("PASS Apple SMC v1 key reads: NKEY, CLKH, RGEN, mailbox READ/INDEX/SRAM");
    return 0;
}
