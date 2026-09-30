# ANS PCIe host (ANS composite region stub)

First-party stub for the **PCIExpressHost** containers inside the qemu-t8030
ANS2 composite block (layout index 3). Behaviour facts are taken from
qemu-t8030 `hw/block/apple_ans.c` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference, behaviour-only):
`apple_ans_create` allocates empty `ans_pci_mmio` / `ans_pci_ioport` memory
regions and calls `pcie_host_mmcfg_init` for a root bus. Inferno ANS composite
layout is surveyed in `docs/research/INFERNO_DEVICE_MODELS.md`.

This stub is **not** the ANS vendor MMIO window (`ans_v1`), **not** ASCWrap /
mailbox / autoboot maps, and **not** a real ECAM / AIC / command-DMA path.
Bound MSI message write (below) is host/test honesty only —
**message write ≠ AIC by itself**. Preferred path is GPA→host buffer
translate (`vf_ans_pci_bind_msi_gpa`); DMA callback remains when GPA is
unbound. Optional AIC STORAGE-line bind (below) mirrors INTx|MSI|MSI-X
delivery pending onto graph source **1** with fail-closed unbound honesty —
**not** ownership of graph STORAGE window `0x4`, Apple DT IRQ numbers, or
guest acceptance. MSI-X stub is Enable + Table BIR prepare + pending latch
only (no PBA / table MMIO walk).

## Composite layout (reference-only)

| Index | Region | qemu name | Stub |
| --- | --- | --- | --- |
| 0 | `reg[0]`..`reg[1]` | AppleA7IOP akfRegMap (mbox) | `ans_mbox_v1` |
| 1 | `reg[2]`..`reg[3]` | AppleASCWrapV2 coreRegisterMap | `ascwrap_v1` |
| 2 | `reg[4]`..`reg[5]` | AppleA7IOP autoBootRegMap | `ans_autoboot_v1` |
| 3 | PCI host | ans_pci_mmio / ans_pci_ioport | **`ans_pci_v1`** |
| 4 | `reg[6]`..`reg[7]` | vendor MMIO + NVMe alias | `ans_v1` |

## Guest-visible contract

1. After `vf_ans_pci_init`, status is **`BOOTSTRAP`** and flags include
   **`PRESENT`**, **`MMIO_CONTAINER`**, **`IOPORT_CONTAINER`**,
   **`MSI_CAPABLE`**, and **`MSIX_CAPABLE`**. Status never advances to
   `ACTIVE` (would imply a real PCIe host bring-up with ECAM / AIC / command
   DMA), even when Memory|BusMaster bits, MSI Enable, or MSI-X Enable are set.
2. Graph-local ioport size reads **`65536`** (qemu `64 * 1024`).
3. Graph-local MMCFG size reads **`1<<28`** (`PCIE_MMCFG_SIZE_MAX` honesty);
   this does **not** claim ECAM enumeration or config-space device IDs.
4. Graph-local config probe at offset `0x018` is width **64** only:
   - **Read** returns the stored PCI `COMMAND` Memory|BusMaster mask
     (`0x0002` | `0x0004`); other bits always read `0`.
   - **Write** accepts any 64-bit value; only Memory|BusMaster
     (`VF_ANS_PCI_COMMAND_ENABLE_MASK`) are latched for read-back.
   - Setting BusMaster alone does **not** invent mailbox start/wakeup
     (`FLAG_STARTED` lives on `ans_mbox_v1`).
5. Host/test **`vf_ans_pci_enable_memory_bus_master`** ORs Memory|BusMaster
   into COMMAND (qemu `apple_ans_start`). Used by bound `ans_mbox` start/
   wakeup; never enables command DMA, AIC, or ECAM.
6. Host/test may observe `vf_ans_pci_command`,
   `vf_ans_pci_bus_master_enabled`, `vf_ans_pci_config_write_seen`,
   `last_config_write`, `vf_ans_pci_set_irq`, `vf_ans_pci_irq_pending`,
   `vf_ans_pci_msi_enabled`, `vf_ans_pci_msi_pending`,
   `vf_ans_pci_msix_enabled`, `vf_ans_pci_msix_pending`,
   `vf_ans_pci_msix_table_bir`,
   `vf_ans_pci_msi_addr`, `vf_ans_pci_msi_data`,
   `vf_ans_pci_msi_gpa_bound`, `vf_ans_pci_msi_message_written`,
   `vf_ans_pci_last_msi_message_addr`, `vf_ans_pci_last_msi_message_data`,
   `vf_ans_pci_delivery_pending`,
   `vf_ans_pci_aic_sync_seen`, and `vf_ans_pci_aic_line_high`.
   `last_config_write` is the raw last WO (not masked); it is **not**
   guest-visible except via the masked config read-back.
7. **INTx pending latch:** `vf_ans_pci_set_irq(level)` stores pin level
   (qemu-t8030 `apple_ans_set_irq` → `qemu_set_irq(s->irq, level)`) when
   MSI Enable and MSI-X Enable are clear. Bound `ans_v1` mirrors
   `vf_ans_irq_check` into this latch (INTMS fail-closed deasserts).
8. **MSI capability / pending stub:** `FLAG_MSI_CAPABLE` announces a
   graph-local MSI surface (not a real PCI capability list). MSI Enable @
   `0x020`, Message Address @ `0x028`, Message Data @ `0x030` store/read for
   prepare honesty. When MSI Enable is set and MSI-X Enable is clear,
   `vf_ans_pci_set_irq` routes pending into `vf_ans_pci_msi_pending` and
   forces INTx clear (PCI-style MSI-vs-INTx exclusivity). Enable toggle while
   pending re-routes between INTx and MSI pending (MSI-X Enable owns delivery
   when set — MSI Enable is then prepare-only).
9. **MSI-X capability / table pending stub:** `FLAG_MSIX_CAPABLE` announces a
   graph-local MSI-X surface (not a real PCI capability list / table BAR).
   MSI-X Enable @ `0x038` and Table BIR @ `0x03c` (bits 2:0 only) store/read
   for prepare honesty. When MSI-X Enable is set, `vf_ans_pci_set_irq` routes
   pending into `vf_ans_pci_msix_pending` and forces INTx + MSI pending clear
   (MSI-X > MSI > INTx). Enable toggle while pending re-routes into MSI-X, or
   on disable back to MSI (if still enabled) else INTx. **No** PBA / table
   entry MMIO or MSI-X message write at this stub.
10. **MSI GPA→host buffer translate (preferred message write):** optional
   `vf_ans_pci_bind_msi_gpa(mem, len, base_gpa)` binds a host stand-in for
   the Message Address GPA window (minimum `VF_ANS_PCI_MSI_MSG_BYTES` = 2).
   When MSI pending asserts, if `msi_addr` is in
   `[base_gpa, base_gpa+len)` with room for 2 bytes, the stub stores LE16
   `msi_data` at offset `(msi_addr - base_gpa)`. **Fail-closed** on OOB /
   short window / unbound (pending still latches; `msi_message_written`
   stays clear). NULL mem unbinds; non-NULL `len < 2` returns `-1` without
   binding. Preferred over the DMA callback when both are set.
11. **MSI message write (bound DMA callback fallback):** optional
   `vf_ans_pci_bind_msi_message(fn, opaque)` registers a host/test sink
   used only when GPA is unbound. When MSI pending asserts (set_irq
   level=1 while enabled, or Enable toggle that re-routes INTx→MSI
   pending), the stub invokes `fn(opaque, msi_addr, msi_data)`. Opaque may
   be a test-visible host buffer. **Fail-closed if unbound** (or if `fn`
   returns non-zero): `msi_pending` still latches, but no write is claimed
   (`msi_message_written` stays clear / prior write-seen cleared on
   unbind when GPA also unbound). Successful writes set
   `msi_message_written` and record `last_msi_message_addr` /
   `last_msi_message_data`. **Message write ≠ AIC by itself** — message
   write alone does not assert an AIC line.
12. **AIC STORAGE-line pending mirror (bound callback):** optional
   `vf_ans_pci_bind_aic(fn, opaque)` registers a host/test / bridge sink
   matching `vf_aic_set_line` shape. On INTx, MSI, or MSI-X delivery-pending
   changes (and on bind), the stub invokes
   `fn(opaque, VF_ANS_PCI_AIC_STORAGE_IRQ_LINE, delivery_pending)` where
   line **1** pattern-matches M1GuestBus `STORAGE_SOURCE` /
   `VF_M1_STORAGE_IRQ_LINE`. **Fail-closed if unbound** (or if `fn`
   returns non-zero): pending latches still hold; `aic_sync_seen` /
   `aic_line_high` stay clear / prior sync-seen cleared on unbind.
   Successful syncs set `aic_sync_seen` and record `aic_line_high`.
   **Does not claim** graph STORAGE window `0x4` (`storage_v1` remains
   separate), Apple DT storage IRQ numbers, ECAM, ACTIVE, or guest
   acceptance. Unbind does not invent a deassert without a sink.

## Graph-local MMIO

Not Apple APCIe / ECAM physical base addresses. Shim style matches ASCWrap /
autoboot:

| Offset | Width | Access | Semantics |
| --- | --- | --- | --- |
| `0x000` | 32 | RO | Status (`ABSENT`/`BOOTSTRAP`/`ACTIVE`) |
| `0x004` | 32 | RO | Flags (`PRESENT` \| containers \| `MSI_CAPABLE` \| `MSIX_CAPABLE`) |
| `0x008` | 32 | RO | Tier = `VF_ANS_PCI_TIER_HOST` (3) |
| `0x010` | 32 | RO | Ioport container size (`65536`) |
| `0x014` | 32 | RO | MMCFG size (`1<<28`) |
| `0x018` | 64 | R/W | COMMAND Memory\|BusMaster store/read |
| `0x020` | 32 | R/W | MSI Enable bit0 (`VF_ANS_PCI_MSI_ENABLE`); other bits drop |
| `0x028` | 64 | R/W | MSI Message Address prepare (written out only via bind) |
| `0x030` | 32 | R/W | MSI Message Data prepare (low-16 latch) |
| `0x038` | 32 | R/W | MSI-X Enable bit0 (`VF_ANS_PCI_MSIX_ENABLE`); other bits drop |
| `0x03c` | 32 | R/W | MSI-X Table BIR prepare (bits 2:0; Table Offset deferred) |

Bounds: offsets ≥ `0x040`, wrong widths, and writes to status/flags/tier/sizes
fail closed.

## PCI COMMAND bits (store/read honesty)

Public PCI config `COMMAND` bit numbers (behaviour-only; not an ECAM claim):

| Bit | Macro | Meaning |
| --- | --- | --- |
| 1 | `VF_ANS_PCI_COMMAND_MEMORY` (`0x0002`) | Memory Space Enable |
| 2 | `VF_ANS_PCI_COMMAND_MASTER` (`0x0004`) | Bus Master Enable |

Only these two bits latch. IO Space Enable and all other COMMAND bits are
ignored on write and read as clear.

## Deferred

- Real ECAM / config-space enumeration and NVMe PCI function IDs
- Real PCI MSI / MSI-X capability-list walk / MSI-X table+PBA MMIO
- Live guest RAM walk for MSI Message Address (bound GPA window is a
  host/test stand-in only)
- Apple DT storage IRQ numbers / guest acceptance (bridge auto-wire present;
  window `0x4` / `storage_v1` remain separate)
- Root-bus BAR decode into `ans_pci_mmio` / `ans_pci_ioport`
- ANS command DMA and block backend
- Any claim that COMMAND / BusMaster / mailbox start / INTx / MSI pending /
  MSI-X pending / MSI GPA translate / MSI message write / AIC STORAGE-line
  bind unlocks `macos_boot_verified`

## Status honesty (2026-09-18)

`ans_pci_v1` announces present/bootstrap and container/MMCFG size honesty with
PCI `COMMAND` Memory|BusMaster store/read at the config probe, plus
`vf_ans_pci_enable_memory_bus_master` for mailbox start latch, plus
`vf_ans_pci_set_irq` / `vf_ans_pci_irq_pending` INTx latch synced from bound
`ans_v1` irq_check (INTMS fail-closed), plus MSI capability/pending stub
(Enable + address/data prepare; pending latch when enabled), plus MSI-X
capability/table pending stub (Enable + Table BIR prepare; pending latch when
enabled; MSI-X > MSI > INTx; **no** PBA/table MMIO), plus optional
`vf_ans_pci_bind_msi_gpa` Message Address GPA→host buffer LE16 translate
(fail-closed OOB / unbound; preferred over DMA callback), plus optional
`vf_ans_pci_bind_msi_message` host/test DMA-callback write of prepared
addr/data on MSI pending assert when GPA unbound (fail-closed;
**message write ≠ AIC alone**), plus optional `vf_ans_pci_bind_aic`
mirroring INTx|MSI|MSI-X delivery pending onto STORAGE AIC line **1**
(fail-closed unbound; **≠ graph window 0x4 / Apple DT IRQ / guest
acceptance**). `preos_bridge` auto-wires bridge `ans_v1` → `ans_pci` on
`vf_m1_guest_mmio_reset` (`vf_ans_bind_pci`; Identify CQ irq_check →
delivery_pending), auto-wires the AIC bind
(`vf_m1_guest_ans_pci_bind_aic` / unbind / set_irq observability; fail-closed
if MMIO/AIC unbound), and auto-binds a research MSI GPA window
(`vf_m1_guest_ans_pci_bind_msi_gpa`; base `0xfee00000`, 16-byte host stand-in;
fail-closed; **≠ live guest RAM / MSI-X table walk**). Host/test MSI-X
observability: `vf_m1_guest_ans_pci_prepare_msix` +
`vf_m1_guest_ans_pci_msix_pending` (Identify CQ → msix_pending /
delivery_pending when MSI-X Enable set; fail-closed; **≠ PBA/table MMIO**).
Mailbox
start/wakeup + `FLAG_STARTED` honesty is in `ans_mbox_v1` (`ANS_MBOX.md`).
ASCWrap mbox STARTED→READY bind is in `ascwrap_v1` (`ASCWRAP.md`). Identify
CNS=CTRL|NS host/test DMA fill + admin SQ doorbell Identify submit +
simplified admin CQ completion / CQH drain + `vf_ans_irq_check` / INTMS mask
+ PCI INTx/MSI/MSI-X pending + optional MSI GPA translate / message-write
sync + optional AIC STORAGE-line sync + ASCWrap READY gate + autoboot ARMED
gate are in `ans_v1` (`ANS.md`). `ans_autoboot_v1` optionally binds ASCWrap so
ARMED mirrors READY (`ANS_AUTOBOOT.md`). Next auto step: MSI-X table/PBA
MMIO deepen if a guest probes it. Avoid GXF / NVRAM / SoftMMU /
`verify_boot_runtime` (owned elsewhere).
