# ANS mailbox / akfRegMap (ANS composite region stub)

First-party stub for the **AppleA7IOP akfRegMap** mailbox inside the qemu-t8030
ANS2 composite block (DT `reg` index 0). Layout facts are taken from qemu-t8030
`hw/block/apple_ans.c` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference, behaviour-only).
Queue-empty / not-empty bit semantics follow public Asahi ASC mailbox
documentation (behaviour-only): empty = bit17, not-empty = bit16. Inferno ANS
composite layout is surveyed in `docs/research/INFERNO_DEVICE_MODELS.md`.
Message packing for the graph-local bootstrap endpoint follows the same SEP L1
bit layout used by `sep_mailbox_v1` (behaviour-only; not Apple ANS EP numbers).

This stub is **not** the ANS vendor MMIO window (`ans_v1`) and **not** the
ASCWrapV2 core map (`ascwrap_v1`).

## Composite layout (reference-only)

| Index | Region | qemu name | Stub |
| --- | --- | --- | --- |
| 0 | `reg[0]`..`reg[1]` | AppleA7IOP akfRegMap (mbox) | **`ans_mbox_v1`** |
| 1 | `reg[2]`..`reg[3]` | AppleASCWrapV2 coreRegisterMap | `ascwrap_v1` |
| 2 | `reg[4]`..`reg[5]` | AppleA7IOP autoBootRegMap | `ans_autoboot_v1` |
| 3 | PCI host | ans_pci_mmio / ans_pci_ioport | `ans_pci_v1` |
| 4 | `reg[6]`..`reg[7]` | vendor MMIO + NVMe alias | `ans_v1` |

## Guest-visible contract

1. After `vf_ans_mbox_init`, status is **`BOOTSTRAP`** and flags include
   **`PRESENT`**. Status never advances to `ACTIVE` (would imply a real IOP /
   ASC firmware path with full endpoint graph / DMA).
2. Host/test **`vf_ans_mbox_start`** / **`vf_ans_mbox_wakeup`** (same path;
   qemu-t8030 `AppleMboxOps.start`/`wakeup` both call `apple_ans_start`) set
   **`FLAG_STARTED`** while status stays **`BOOTSTRAP`**, and post a bootstrap
   **`ANNOUNCE`** reply into I2A (`FLAG_REPLY_PENDING`; I2A qstat **NOT_EMPTY**
   until drained). This is the honest present→started transition without
   promoting `ACTIVE`.
3. Optional **`vf_ans_mbox_bind_pci`**: when bound, start/wakeup also latches
   PCI `COMMAND` Memory|BusMaster on the linked `ans_pci_v1` (qemu
   `apple_ans_start` OR of `0x0002|0x0004`). Clearing BusMaster on the linked
   PCI fails closed: `FLAG_STARTED` is dropped on the next mbox flags/started
   observe (started without BusMaster is dishonest when linked).
4. Setting BusMaster alone on PCI does **not** invent start/wakeup
   (`FLAG_STARTED` stays clear until an explicit start/wakeup).
5. Graph-local **A2I** queue-status always reads **`EMPTY`** (bit17): A2I writes
   are consumed immediately by the stub (never not-empty).
6. Graph-local **I2A** queue-status reads **`NOT_EMPTY`** (bit16) while a
   bootstrap reply is pending, else **`EMPTY`**.
7. Graph-local A2I message slot at offset `0x018` is width **64** only:
   - **Read** always returns `0`.
   - **Write** is accepted for host/test observability. Before STARTED, write
     does **not** invent a reply. After STARTED, the write is parsed as a
     graph-local endpoint message (SEP L1 packing) and may post an I2A reply.
8. Graph-local I2A message slot at offset `0x020` is width **64** RO:
   - **Read** returns the last reply word and drains `REPLY_PENDING` (I2A
     returns to EMPTY). **No** MSI / AIC / DT IRQ on reply.
9. Bootstrap endpoint **`EP=255`** (graph-local; not Apple ANS EP IDs):
   - `PING` (1) → `PING_ACK` (101)
   - `GET_STATUS` (2) → `STATUS_REPLY` (102) with status data
   - Unknown EP / unknown bootstrap op → `L1_REJECT` (0xFE)
10. Host/test may observe `vf_ans_mbox_started`, `a2i_write_seen` /
    `last_a2i_write`, `reply_pending` / `i2a_msg`; those do **not** imply MSI
    or command DMA.
11. When `ans_v1` binds this mbox via `vf_ans_bind_mbox`, Identify CNS=CTRL|NS
    DMA fail-closes unless `vf_ans_mbox_started` (mirror of the PCI BusMaster
    gate). Unbound skips the gate.
12. Optional **`vf_ascwrap_bind_mbox`**: ASCWrap `FLAG_READY` mirrors this
    stub's STARTED (`ASCWRAP.md`). `vf_ans_bind_ascwrap` gates Identify DMA
    on that READY. Autoboot may bind ASCWrap so ARMED mirrors READY; Identify
    may also gate on ARMED (`vf_ans_bind_autoboot`).
13. **No** MSI, DT IRQ numbers, AIC, full AppleMbox endpoint graph, command
    DMA, or `STATUS_ACTIVE`.

## Graph-local MMIO

Not Apple A7IOP/AKF physical base addresses and not the Asahi `+0x8000`
physical mailbox layout. Shim style matches ASCWrap / SEP L1:

| Offset | Width | Access | Semantics |
| --- | --- | --- | --- |
| `0x000` | 32 | RO | Stage (`ABSENT`/`BOOTSTRAP`/`ACTIVE`) |
| `0x004` | 32 | RO | Flags (`PRESENT` [| `STARTED`] [| `REPLY_PENDING`]) |
| `0x008` | 32 | RO | Tier = `VF_ANS_MBOX_TIER_AKF_REGMAP` (0) |
| `0x010` | 32 | RO | A2I queue status: always `EMPTY` |
| `0x014` | 32 | RO | I2A queue status: `NOT_EMPTY` while reply pending, else `EMPTY` |
| `0x018` | 64 | RO/WO | A2I msg: read 0; write accept (+ endpoint when STARTED) |
| `0x020` | 64 | RO | I2A msg: reply drain |

Bounds: offsets ≥ `0x028`, wrong widths, and writes to status/flags/tier/qstat/I2A
fail closed.

## Deferred

- Apple physical AKF / Asahi `+0x8000` register map and 128-bit dual-word MSG
- Full AppleMbox endpoint graph beyond graph-local bootstrap EP 255
- AIC / MSI / DT IRQ routing for send/recv empty/not-empty lines
- Real PCIe ECAM / command DMA (COMMAND Memory|BusMaster store/read + start
  latch is in `ans_pci_v1` / this stub; no DMA/MSI)
- ANS command DMA / block backend
- Any claim that mailbox start/BusMaster / bootstrap EP honesty unlocks
  `macos_boot_verified`

## Status honesty (2026-09-18)

`ans_mbox_v1` announces present/bootstrap, keeps A2I EMPTY, posts I2A bootstrap
replies (ANNOUNCE on start; PING/GET_STATUS when STARTED), and exposes
start/wakeup → `FLAG_STARTED` (+ optional PCI Memory|BusMaster latch) without
`ACTIVE` / MSI / DMA / AIC. Identify CNS=CTRL|NS DMA may bind this mbox and
require STARTED (`vf_ans_bind_mbox`). ASCWrap may bind this mbox so READY
mirrors STARTED (`vf_ascwrap_bind_mbox`); Identify may also gate on READY
(`vf_ans_bind_ascwrap`). Autoboot may bind ASCWrap so ARMED mirrors READY;
Identify may also gate on ARMED (`vf_ans_bind_autoboot`). Identify CNS=CTRL|NS
host/test DMA fill + admin SQ
doorbell Identify submit + simplified admin CQ completion / CQH drain +
`vf_ans_irq_check` / INTMS mask + bound PCI INTx pending sync are in `ans_v1`
(`ANS.md`). IOP autoBootRegMap present/bootstrap + ASCWrap READY→ARMED bind
is in `ans_autoboot_v1`
(`ANS_AUTOBOOT.md`). PCIe host present/bootstrap + COMMAND Memory|BusMaster +
INTx pending latch is in `ans_pci_v1` (`ANS_PCI.md`). Next auto step:
MSI message path (still not claimed), or AIC graph hook if pattern-matched.
Avoid GXF / NVRAM / SoftMMU (owned elsewhere).
