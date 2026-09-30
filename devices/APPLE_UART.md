# Apple UART device layer

First-party stub covers Samsung-style Apple UART MMIO used on t8030 and
related SoCs. Register facts are extracted from qemu-t8030
`hw/char/apple_uart.c` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference, behaviour-only).

## Register map (0x3C bytes, u32 aligned)

| Offset | Name | Reset | Access | Stub |
| --- | --- | --- | --- | --- |
| 0x0000 | ULCON | 0 | RW | store |
| 0x0004 | UCON | 0 | RW | store |
| 0x0008 | UFCON | 0 | RW | store; RX reset clears fifo |
| 0x000C | UMCON | 0 | RW | store |
| 0x0010 | UTRSTAT | **0x6** | RW ack | TX empty/buffer-empty at reset |
| 0x0014 | UERSTAT | 0 | RW clear | read clears |
| 0x0018 | UFSTAT | 0 | RO | RX count low nibble |
| 0x001C | UMSTAT | 0 | RO | fail write |
| 0x0020 | UTXH | — | WO | **tx callback** |
| 0x0024 | URXH | 0 | RO | fifo or holding reg |
| 0x0028 | UBRDIV | 0 | RW | store |
| 0x002C | UFRACVAL | 0 | RW | store |

### UTRSTAT bits (from reference)

| Bit | Name |
| --- | --- |
| 0 | Rx buffer data ready |
| 1 | Tx buffer empty |
| 2 | Tx empty |
| 3 | Rx timeout |
| 4 | Rx threshold |
| 5 | Tx threshold |

### UCON / UFCON (documented, partial stub)

- `UCON_TXMODE` / `UCON_RXMODE` at bits [3:2] / [1:0]; DMA and IRQ modes deferred.
- `UFCON_FIFO_ENABLE` bit0; RX/TX fifo reset bits 1/2.
- Trigger levels and `wordtime` baud derivation (24 MHz uclk) deferred.

## Boot-unblock path

Early firmware typically polls **UTRSTAT** for TX ready and writes **UTXH**.
The stub returns reset `UTRSTAT=0x6`, emits bytes via an optional host callback
on **UTXH** write, and reasserts TX-empty status. RX uses a bounded 16-byte
software fifo when **UFCON.FIFO_ENABLE** is set.

## IRQ (graph-local AIC wire)

`irq_pending` asserts on **UTXH** TX and RX `push_rx`, and clears on **UTRSTAT**
ack or **UFCON** RX reset. `preos_bridge.c` syncs that flag to AIC line
`VF_M1_UART_IRQ_LINE` (**5**) via `vf_m1_guest_aic_set_line` /
`vf_m1_guest_uart_push_rx`. Public Asahi `t8103.dtsi` serial0 uses AIC_IRQ
**605** as a behaviour reference only — this stub does not claim that physical
IRQ number.

## Deferred

Char backend, DMA, fifo timeout timer, break handling, channel-specific
trigger tables, and M1 physical base / `apple,uart` DT binding. Not sufficient
for full iBoot console or macOS serial. 16550/pl011 must not substitute without
differential tests. Guest UART bytes remain empty until AUX/root is provisioned.
