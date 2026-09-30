# Storage L1 (graph-local M1GuestBus attach / AIC source 1)

First-party **graph-local storage** stub aligned with Rust `M1Storage` and
`STORAGE_SOURCE` (**1**). Behaviour matches the preOS machine-graph contract
(attach metadata, generation, read-only guest MMIO). Not Apple ANS/NVMe vendor
MMIO, not a block backend, and not a Device Tree storage claim.

## Tier boundary

| Tier | What it is | Public tree |
| --- | --- | --- |
| **L0** | Absent — storage only in Rust graph | historical |
| **L1** | Attach/status/generation MMIO + pending → AIC line 1 | **`storage_v1`** (this stub) |
| L2+ | ANS vendor MMIO / NVMe BAR / DMA / namespaces | **deferred** (`ans_v1` separate) |

L1 stops at the graph overlay shared with Rust `M1MachineGraph`. Full ANS2 /
NVMe command paths remain deferred.

## Guest-visible L1 contract

1. After reset: detached, `block_count` `0`, `generation` `0`, IRQ clear.
2. Host/test `vf_storage_attach(block_count, read_only)` attaches, bumps
   generation, and asserts pending while attached.
3. Guest MMIO is **read-only** (matches Rust `M1LogicalWindow::Storage`).
4. `vf_storage_detach` clears attach metadata; generation is sticky.
5. Unsupported widths / unknown offsets / guest writes fail closed.

## Graph-local MMIO (`VF_M1_MMIO_WINDOW_STORAGE` = `0x4`)

Window id is `M1_LOGICAL_STORAGE_BASE` (`0x4000`) >> 12. Not an Apple SoC base.

| Offset | Width | Access | Semantics |
| --- | --- | --- | --- |
| `0x000` | 64 | RO | Status: bit0 attached, bit1 read-only |
| `0x008` | 64 | RO | Block count (4096-byte blocks) |
| `0x010` | 64 | RO | Generation (bumps on each successful attach) |

## AIC IRQ

Graph-local sources **0–3** are **timer** / **storage** / display / recovery in
Rust `M1MachineGraph`. Storage pending raises `STORAGE_SOURCE` (**1**).

C bridge maps the same line: `VF_M1_STORAGE_IRQ_LINE` (**1**) via
`vf_m1_guest_aic_set_line` / `storage_sync_aic_irq` while attached.
Apple ANS MSI / DT IRQ numbers are **not** claimed by `storage_v1`; this is the
sources 0–7 overlay only. Standalone `ans_pci_v1` may optionally bind the same
line **1** via `vf_ans_pci_bind_aic` (INTx|MSI delivery pending mirror;
fail-closed unbound) without owning this window. `preos_bridge` auto-wires
bridge `ans_v1` → `ans_pci` → this AIC line on mmio_reset (still ≠ this
window's attach semantics / Apple DT IRQ).

## Deferred

- ANS vendor MMIO (`ans_v1`) mapped into this graph STORAGE window
- NVMe command path beyond Identify CQ host/test / namespaces / MSI-X
- Physical Apple storage MMIO bases or DT bindings
- Any claim that storage IRQ correctness unlocks `macos_boot_verified`
