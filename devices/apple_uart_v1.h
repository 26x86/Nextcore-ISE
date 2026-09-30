/* 26x86 first-party code; repository LICENSE.txt applies. */
#ifndef VENFIRE_APPLE_UART_V1_H
#define VENFIRE_APPLE_UART_V1_H
#include <stdint.h>

#define VF_APPLE_UART_MMIO_SIZE 0x3cu

#define VF_UART_ULCON    0x0000u
#define VF_UART_UCON     0x0004u
#define VF_UART_UFCON    0x0008u
#define VF_UART_UMCON    0x000cu
#define VF_UART_UTRSTAT  0x0010u
#define VF_UART_UERSTAT  0x0014u
#define VF_UART_UFSTAT   0x0018u
#define VF_UART_UMSTAT   0x001cu
#define VF_UART_UTXH     0x0020u
#define VF_UART_URXH     0x0024u
#define VF_UART_UBRDIV   0x0028u
#define VF_UART_UFRACVAL 0x002cu

#define VF_UART_UTRSTAT_TX_EMPTY         (1u << 2)
#define VF_UART_UTRSTAT_TX_BUFFER_EMPTY (1u << 1)
#define VF_UART_UTRSTAT_RX_READY         (1u << 0)
#define VF_UART_UTRSTAT_RESET            (VF_UART_UTRSTAT_TX_EMPTY | VF_UART_UTRSTAT_TX_BUFFER_EMPTY)

#define VF_UART_UFCON_FIFO_ENABLE (1u << 0)
#define VF_UART_UFCON_RX_RESET    (1u << 1)
#define VF_UART_UFCON_TX_RESET    (1u << 2)

typedef void (*vf_uart_tx_fn)(void *ctx, uint8_t byte);

typedef struct {
    uint32_t reg[VF_APPLE_UART_MMIO_SIZE / 4u];
    uint8_t rx_fifo[16];
    unsigned rx_count;
    vf_uart_tx_fn tx;
    void *tx_ctx;
    int irq_pending;
} vf_apple_uart_v1;

int vf_apple_uart_init(vf_apple_uart_v1 *, vf_uart_tx_fn tx, void *tx_ctx);
int vf_apple_uart_push_rx(vf_apple_uart_v1 *, uint8_t byte);
int vf_apple_uart_irq_pending(const vf_apple_uart_v1 *);
int vf_apple_uart_read(vf_apple_uart_v1 *, uint32_t offset, unsigned width, uint32_t *value);
int vf_apple_uart_write(vf_apple_uart_v1 *, uint32_t offset, unsigned width, uint32_t value);

#endif
