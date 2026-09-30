/* 26x86 first-party Apple UART v1 stub. See APPLE_UART.md for register evidence.
 * Behaviour adapted from qemu-t8030 hw/char/apple_uart.c (GPL reference only).
 */
#include "apple_uart_v1.h"

static int uart_bounds(uint32_t off, unsigned width) {
    if (width != 4 || (off & 3u)) return -1;
    if (off >= VF_APPLE_UART_MMIO_SIZE) return -1;
    return 0;
}

static unsigned uart_idx(uint32_t off) { return off / 4u; }

static void uart_update_status(vf_apple_uart_v1 *u) {
    if (u->rx_count)
        u->reg[uart_idx(VF_UART_UTRSTAT)] |= VF_UART_UTRSTAT_RX_READY;
    else
        u->reg[uart_idx(VF_UART_UTRSTAT)] &= ~VF_UART_UTRSTAT_RX_READY;
}

int vf_apple_uart_init(vf_apple_uart_v1 *u, vf_uart_tx_fn tx, void *tx_ctx) {
    if (!u) return -1;
    for (unsigned i = 0; i < (unsigned)(sizeof u->reg / sizeof u->reg[0]); i++)
        u->reg[i] = 0;
    u->reg[uart_idx(VF_UART_UTRSTAT)] = VF_UART_UTRSTAT_RESET;
    u->rx_count = 0;
    u->tx = tx;
    u->tx_ctx = tx_ctx;
    u->irq_pending = 0;
    return 0;
}

int vf_apple_uart_push_rx(vf_apple_uart_v1 *u, uint8_t byte) {
    if (!u || u->rx_count >= sizeof u->rx_fifo) return -1;
    u->rx_fifo[u->rx_count++] = byte;
    uart_update_status(u);
    u->irq_pending = 1;
    return 0;
}

int vf_apple_uart_irq_pending(const vf_apple_uart_v1 *u) {
    return u && u->irq_pending;
}

int vf_apple_uart_read(vf_apple_uart_v1 *u, uint32_t off, unsigned width, uint32_t *value) {
    if (!u || !value || uart_bounds(off, width)) return -1;

    switch (off) {
    case VF_UART_UERSTAT:
        *value = u->reg[uart_idx(off)];
        u->reg[uart_idx(off)] = 0;
        return 0;
    case VF_UART_UFSTAT:
        *value = u->rx_count & 0xfu;
        if (u->rx_count >= sizeof u->rx_fifo)
            *value |= (1u << 8);
        return 0;
    case VF_UART_URXH:
        if (u->reg[uart_idx(VF_UART_UFCON)] & VF_UART_UFCON_FIFO_ENABLE) {
            if (!u->rx_count) {
                *value = 0;
                return 0;
            }
            *value = u->rx_fifo[0];
            for (unsigned i = 1; i < u->rx_count; i++)
                u->rx_fifo[i - 1] = u->rx_fifo[i];
            u->rx_count--;
        } else {
            *value = u->reg[uart_idx(VF_UART_URXH)];
        }
        uart_update_status(u);
        return 0;
    case VF_UART_UTXH:
        return -1; /* write-only */
    default:
        *value = u->reg[uart_idx(off)];
        return 0;
    }
}

int vf_apple_uart_write(vf_apple_uart_v1 *u, uint32_t off, unsigned width, uint32_t value) {
    if (!u || uart_bounds(off, width)) return -1;

    switch (off) {
    case VF_UART_UFCON:
        u->reg[uart_idx(off)] = value;
        if (value & VF_UART_UFCON_RX_RESET) {
            u->rx_count = 0;
            u->reg[uart_idx(off)] &= ~VF_UART_UFCON_RX_RESET;
            uart_update_status(u);
            u->irq_pending = 0;
        }
        return 0;
    case VF_UART_UTXH:
        if (u->tx)
            u->tx(u->tx_ctx, (uint8_t)value);
        u->reg[uart_idx(VF_UART_UTRSTAT)] |= VF_UART_UTRSTAT_TX_EMPTY | VF_UART_UTRSTAT_TX_BUFFER_EMPTY;
        u->irq_pending = 1;
        return 0;
    case VF_UART_UTRSTAT:
        u->reg[uart_idx(off)] &= ~(value & VF_UART_UTRSTAT_TX_EMPTY);
        u->irq_pending = 0;
        return 0;
    case VF_UART_UERSTAT:
        u->reg[uart_idx(off)] &= ~value;
        return 0;
    case VF_UART_UFSTAT:
    case VF_UART_UMSTAT:
    case VF_UART_URXH:
        return -1; /* read-only */
    default:
        u->reg[uart_idx(off)] = value;
        return 0;
    }
}
