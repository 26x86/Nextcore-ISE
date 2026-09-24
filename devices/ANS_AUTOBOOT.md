# ANS IOP autoBootRegMap (ANS composite region stub)

First-party stub for the **AppleA7IOP autoBootRegMap** inside the qemu-t8030
ANS2 composite block (DT `reg` index 2). Behaviour facts are taken from
qemu-t8030 `hw/block/apple_ans.c` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference, behaviour-only):
the IOP autoboot map **reads as 0** and **accepts writes** with no modeled
side effects (no firmware load). Inferno ANS composite layout is surveyed in
`docs/research/INFERNO_DEVICE_MODELS.md`.

This stub is **not** the ANS vendor MMIO window (`ans_v1`), **not** the
ASCWrapV2 core map (`ascwrap_v1`), and **not** the akfRegMap mailbox
(`ans_mbox_v1`).

## Composite layout (reference-only)

| Index | Region | qemu name | Stub |
| --- | --- | --- | --- |
| 0 | `reg[0]`..`reg[1]` | AppleA7IOP akfRegMap (mbox) | `ans_mbox_v1` |
| 1 | `reg[2]`..`reg[3]` | AppleASCWrapV2 coreRegisterMap | `ascwrap_v1` |
| 2 | `reg[4]`..`reg[5]` | AppleA7IOP autoBootRegMap | **`ans_autoboot_v1`** |
| 3 | PCI host | ans_pci_mmio / ans_pci_ioport | `ans_pci_v1` |
| 4 | `reg[6]`..`reg[7]` | vendor MMIO + NVMe alias | `ans_v1` |

## Guest-visible contract

1. After `vf_ans_autoboot_init`, status is **`BOOTSTRAP`** and flags include
   **`PRESENT`**. Status never advances to `ACTIVE` (would imply a real IOP
   firmware autoload path).
2. Graph-local autoboot alias at offset `0x010` is width **64** only:
   - **Read** always returns `0`.
   - **Write** is accepted when unbound; subsequent reads still return `0`
     (RO/WO → 0). Map WO never invents **`FLAG_ARMED`**.
3. Host/test may observe `vf_ans_autoboot_write_seen` / `last_autoboot_write`
   for unit honesty; those values are **not** guest-visible on read-back.
4. Optional **`vf_ans_autoboot_bind_ascwrap`**: when bound, **`FLAG_ARMED`**
   mirrors `vf_ascwrap_ready` (ASCWrap READY ← mbox STARTED). Clearing READY
   (or never starting mbox) drops ARMED on the next flags/armed observe
   (fail-closed). Unbound autoboot never reports ARMED.
5. When ASCWrap is bound, map **WO fail-closes** unless ARMED/READY. Unbound
   skips this gate (standalone RO/WO → 0 honesty preserved).
6. When `ans_v1` binds this autoboot via `vf_ans_bind_autoboot`, Identify
   CNS=CTRL|NS host/test DMA fill fail-closes unless `vf_ans_autoboot_armed`
   (ARMED←ASCWrap READY←mbox STARTED). Unbound skips this gate.
7. **No** MSI, DT IRQ numbers, mailbox start/wakeup, PCIe, or command DMA.
   Writes do **not** claim IOP firmware load or endpoint start.

## Graph-local MMIO

Not Apple A7IOP autoBootRegMap physical base addresses. Shim style matches
ASCWrap / SEP L1:

| Offset | Width | Access | Semantics |
| --- | --- | --- | --- |
| `0x000` | 32 | RO | Stage (`ABSENT`/`BOOTSTRAP`/`ACTIVE`) |
| `0x004` | 32 | RO | Flags (`PRESENT` [| `ARMED`]) |
| `0x008` | 32 | RO | Tier = `VF_ANS_AUTOBOOT_TIER_REGMAP` (2) |
| `0x010` | 64 | RO/WO | Autoboot map: read 0; write accept (gated when bound) |

Bounds: offsets ≥ `0x018`, wrong widths, and writes to status/flags/tier
fail closed.

## Deferred

- Physical autoBootRegMap size / unconstrained native access widths
- IOP firmware image load, start/wakeup, and reply generation
- Real PCIe ECAM / command DMA (COMMAND Memory|BusMaster store/read is in
  `ans_pci_v1`; no DMA/MSI)
- AIC / MSI / DT IRQ routing
- Mailbox endpoints and ANS command DMA
- Any claim that autoboot present/bootstrap/ARMED unlocks `macos_boot_verified`

## Status honesty (2026-09-18)

`ans_autoboot_v1` announces present/bootstrap, implements the documented
RO/WO → 0 autoboot map contract, and optionally binds `ascwrap_v1` so
`FLAG_ARMED` mirrors ASCWrap READY (composite with mbox STARTED→READY);
bound map WO fail-closes without READY. `ans_v1` `vf_ans_bind_autoboot`
gates Identify CNS=CTRL|NS DMA on ARMED. PCIe host present/bootstrap + COMMAND
Memory|BusMaster + INTx pending latch is in `ans_pci_v1` (`ANS_PCI.md`).
Mailbox start/wakeup `FLAG_STARTED` (+ optional BusMaster latch) is in
`ans_mbox_v1` (`ANS_MBOX.md`). ASCWrap mbox STARTED→READY bind is in
`ascwrap_v1` (`ASCWRAP.md`). Identify CNS=CTRL|NS host/test DMA fill + admin SQ
doorbell Identify submit + simplified admin CQ completion / CQH drain +
`vf_ans_irq_check` / INTMS mask + bound PCI INTx pending sync + bound ASCWrap
READY gate + bound autoboot ARMED gate are in `ans_v1` (`ANS.md`). Next auto
step: MSI message path (still not claimed), or AIC graph hook if
pattern-matched. Avoid GXF / NVRAM / SoftMMU (owned elsewhere).
