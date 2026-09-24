# ASCWrapV2 core map (ANS composite region stub)

First-party stub for the **AppleASCWrapV2 coreRegisterMap** inside the
qemu-t8030 ANS2 composite block (DT `reg` index 1). Behaviour facts are taken
from qemu-t8030 `hw/block/apple_ans.c` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference, behaviour-only):
the ASCWrap core map is an **8-byte** region that **RO/WO → 0**. Inferno ANS
composite layout is surveyed in `docs/research/INFERNO_DEVICE_MODELS.md`.

This stub is **not** the ANS vendor MMIO window (`ans_v1`) and **not** the
AppleA7IOP mailbox (`akfRegMap` / `ans_mbox_v1`).

## Composite layout (reference-only)

| Index | Region | qemu name | Stub |
| --- | --- | --- | --- |
| 0 | `reg[0]`..`reg[1]` | AppleA7IOP akfRegMap (mbox) | `ans_mbox_v1` |
| 1 | `reg[2]`..`reg[3]` | AppleASCWrapV2 coreRegisterMap | **`ascwrap_v1`** |
| 2 | `reg[4]`..`reg[5]` | AppleA7IOP autoBootRegMap | `ans_autoboot_v1` |
| 3 | PCI host | ans_pci_mmio / ans_pci_ioport | `ans_pci_v1` |
| 4 | `reg[6]`..`reg[7]` | vendor MMIO + NVMe alias | `ans_v1` |

## Guest-visible contract

1. After `vf_ascwrap_init`, status is **`BOOTSTRAP`** and flags include
   **`PRESENT`**. Status never advances to `ACTIVE` (would imply a real ASC /
   IOP firmware path).
2. Graph-local core alias at offset `0x010` is width **64** only:
   - **Read** always returns `0`.
   - **Write** is accepted; subsequent reads still return `0` (RO/WO → 0).
3. Host/test may observe `vf_ascwrap_core_write_seen` / `last_core_write` for
   unit honesty; those values are **not** guest-visible on core read-back.
   Core WO never invents **`FLAG_READY`**.
4. Optional **`vf_ascwrap_bind_mbox`**: when bound, **`FLAG_READY`** mirrors
   `vf_ans_mbox_started` (mbox STARTED → ASCWrap READY). Clearing mbox
   STARTED (or never starting) drops READY on the next flags/ready observe
   (fail-closed). Unbound ASCWrap never reports READY.
5. When `ans_v1` binds this ASCWrap via `vf_ans_bind_ascwrap`, Identify
   CNS=CTRL|NS DMA fail-closes unless `vf_ascwrap_ready` (composite gate with
   the ASCWrap↔mbox STARTED→READY bind). Autoboot may mirror READY→ARMED;
   Identify may also gate on ARMED (`vf_ans_bind_autoboot`).
6. **No** MSI, DT IRQ numbers, endpoint handlers, PCIe ECAM, or DMA.

## Graph-local MMIO

Not Apple ASCWrap physical base addresses. Shim style matches SEP L1 / SMC:

| Offset | Width | Access | Semantics |
| --- | --- | --- | --- |
| `0x000` | 32 | RO | Stage (`ABSENT`/`BOOTSTRAP`/`ACTIVE`) |
| `0x004` | 32 | RO | Flags (`PRESENT` [| `READY`]) |
| `0x008` | 32 | RO | Tier = `VF_ASCWRAP_TIER_CORE_MAP` (1) |
| `0x010` | 64 | RO/WO | Core map: read 0; write accept |

Bounds: offsets ≥ `0x018`, wrong widths, and writes to status/flags/tier
fail closed.

## Deferred

- Apple A7IOP/AKF mailbox endpoint handlers and physical AKF map
- ASCWrap registers beyond the 8-byte core zero map
- Real PCIe ECAM / command DMA (COMMAND Memory|BusMaster store/read is in
  `ans_pci_v1`; no DMA/MSI)
- MSI / DT IRQ routing
- Any claim that ASCWrap present/bootstrap/READY unlocks `macos_boot_verified`

## Status honesty (2026-09-18)

`ascwrap_v1` announces present/bootstrap, implements the documented 8-byte
core RO/WO → 0 contract, and optionally binds `ans_mbox_v1` so
`FLAG_READY` mirrors mbox STARTED (fail-closed). Identify CNS=CTRL|NS
host/test DMA fill may bind ASCWrap via `vf_ans_bind_ascwrap` and require
READY (`ANS.md`). Autoboot may bind ASCWrap so ARMED mirrors READY; Identify
may also gate on ARMED (`vf_ans_bind_autoboot`). Mailbox akfRegMap
present/bootstrap + start/wakeup `FLAG_STARTED` (+ optional PCI
Memory|BusMaster latch) + bootstrap EP is in `ans_mbox_v1` (`ANS_MBOX.md`).
IOP autoBootRegMap present/bootstrap + optional ASCWrap READY→ARMED bind is
in `ans_autoboot_v1` (`ANS_AUTOBOOT.md`). PCIe host present/bootstrap +
COMMAND Memory|BusMaster + INTx pending latch is in `ans_pci_v1`
(`ANS_PCI.md`). Next auto step: MSI message path (still not claimed), or AIC
graph hook if pattern-matched. Avoid GXF / NVRAM / SoftMMU (owned elsewhere).
