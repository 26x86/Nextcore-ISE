/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_APPLE_SMC_V1_H
#define VENFIRE_APPLE_SMC_V1_H
#include <stdint.h>

/* Apple SMC IOP key protocol — adapted from qemu-t8030 hw/misc/apple_smc.c. */
#define VF_SMC_SRAM_SIZE         0x4000u
#define VF_SMC_ENDPOINT_KEY      1u

#define VF_SMC_KEY_NKEY          0x234B4559u /* '#KEY' */
#define VF_SMC_KEY_CLKH          0x434C4B48u
#define VF_SMC_KEY_RGEN          0x5247454Eu
#define VF_SMC_KEY_ADC           0x61444323u /* aDC# */
#define VF_SMC_KEY_MBSE          0x4D425345u
#define VF_SMC_KEY_NESN          0x4E45534Eu

#define VF_SMC_CMD_READ_KEY          0x10u
#define VF_SMC_CMD_WRITE_KEY         0x11u
#define VF_SMC_CMD_GET_KEY_BY_INDEX  0x12u
#define VF_SMC_CMD_GET_KEY_INFO      0x13u
#define VF_SMC_CMD_GET_SRAM_ADDR     0x17u
#define VF_SMC_CMD_READ_KEY_PAYLOAD  0x20u

#define VF_SMC_SUCCESS               0u
#define VF_SMC_BAD_COMMAND           0x82u
#define VF_SMC_KEY_NOT_FOUND         0x84u
#define VF_SMC_KEY_NOT_READABLE      0x85u
#define VF_SMC_KEY_NOT_WRITABLE      0x86u
#define VF_SMC_KEY_INDEX_RANGE       0xb8u

#define VF_SMC_MAX_KEYS              8u

typedef struct {
    uint8_t cmd;
    uint8_t tag;
    uint8_t length;
    uint8_t payload_length;
    uint32_t key;
} vf_smc_key_msg;

typedef struct {
    uint8_t status;
    uint8_t tag;
    uint8_t length;
    uint8_t unk3;
    uint8_t response[4];
} vf_smc_key_resp;

typedef struct {
    uint32_t key;
    uint8_t size;
    uint8_t readable;
    uint8_t writable;
    uint8_t data[8];
} vf_smc_key_entry;

typedef struct {
    vf_smc_key_entry keys[VF_SMC_MAX_KEYS];
    unsigned key_count;
    uint64_t sram_addr;
    uint8_t sram[VF_SMC_SRAM_SIZE];
} vf_smc_v1;

int vf_smc_init(vf_smc_v1 *s);
/* Process one 64-bit mailbox word; response written to *out when non-null. */
int vf_smc_handle_msg(vf_smc_v1 *s, uint64_t msg_in, uint64_t *out);
/* Direct key read for tests — copies up to buf_len, sets *out_len. */
int vf_smc_read_key(vf_smc_v1 *s, uint32_t key, void *buf, unsigned buf_len,
                    uint8_t *out_len);

#endif
