#include "apple_uart_v1.h"
#include <assert.h>
#include <stdio.h>

static unsigned tx_log;
static void capture_tx(void *ctx, uint8_t byte) {
    (void)ctx;
    tx_log = (tx_log << 8) | byte;
}

int main(void) {
    vf_apple_uart_v1 u;
    uint32_t v = 0;

    assert(!vf_apple_uart_init(&u, capture_tx, NULL));
    assert(!vf_apple_uart_read(&u, VF_UART_UTRSTAT, 4, &v) && v == VF_UART_UTRSTAT_RESET);
    assert(!vf_apple_uart_write(&u, VF_UART_UTXH, 4, 'A'));
    assert(tx_log == 'A');
    assert(!vf_apple_uart_read(&u, VF_UART_UTRSTAT, 4, &v) && (v & VF_UART_UTRSTAT_TX_EMPTY));
    assert(!vf_apple_uart_write(&u, VF_UART_UFCON, 4, VF_UART_UFCON_FIFO_ENABLE));
    assert(!vf_apple_uart_push_rx(&u, 'Z'));
    assert(vf_apple_uart_irq_pending(&u));
    assert(!vf_apple_uart_read(&u, VF_UART_URXH, 4, &v) && v == 'Z');
    assert(!vf_apple_uart_read(&u, VF_UART_UFSTAT, 4, &v) && v == 0);
    assert(!vf_apple_uart_write(&u, VF_UART_UTRSTAT, 4, VF_UART_UTRSTAT_TX_EMPTY));
    assert(!vf_apple_uart_irq_pending(&u));
    assert(!vf_apple_uart_push_rx(&u, 'Y'));
    assert(vf_apple_uart_irq_pending(&u));
    assert(!vf_apple_uart_write(&u, VF_UART_UFCON, 4, VF_UART_UFCON_FIFO_ENABLE | VF_UART_UFCON_RX_RESET));
    assert(!vf_apple_uart_irq_pending(&u));
    assert(vf_apple_uart_read(&u, VF_UART_UTXH, 4, &v) == -1);
    assert(vf_apple_uart_write(&u, VF_UART_UFSTAT, 4, 1) == -1);
    assert(vf_apple_uart_read(&u, VF_APPLE_UART_MMIO_SIZE, 4, &v) == -1);
    puts("PASS Apple UART v1: reset UTRSTAT, UTXH emit, RX fifo, RO/WO gates, bounds");
    return 0;
}
