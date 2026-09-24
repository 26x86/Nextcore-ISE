# Recovery L1 (graph-local M1GuestBus envelope / AIC source 3)

First-party **graph-local recovery** stub aligned with Rust
`M1RecoveryTransport` and `RECOVERY_SOURCE` (**3**). Behaviour matches the
preOS machine-graph contract (CRC/sequence opaque envelope, read-only guest
MMIO, pending while ready). Not Apple RecoveryOS media, not IMG4/signature
validation, and not a Device Tree claim.

## Tier boundary

| Tier | What it is | Public tree |
| --- | --- | --- |
| **L0** | Absent — recovery only in Rust graph | historical |
| **L1** | CRC/sequence envelope MMIO + pending → AIC line 3 | **`recovery_v1`** (this stub) |
| L2+ | Apple trust-chain / RecoveryOS media ingest | **blocked** (never stage blobs) |

L1 stops at the graph overlay shared with Rust `M1MachineGraph`.
`signature_verified` remains false until an external verifier supplies
evidence.

## Guest-visible L1 contract

1. After reset: not ready, sequence `0`, payload length `0`, IRQ clear.
2. Host/test `vf_recovery_ingest` accepts a bounded `VFR1` frame (header +
   payload + CRC32), advances expected sequence, sets ready, and asserts
   pending.
3. Guest MMIO is **read-only** (matches Rust `M1LogicalWindow::Recovery`).
4. `vf_recovery_clear_ready` clears the ready/IRQ bit; last sequence and
   payload length remain sticky (observable ingest epoch).
5. Unsupported widths / unknown offsets / guest writes fail closed.

## Graph-local MMIO (`VF_M1_MMIO_WINDOW_RECOVERY` = `0x5`)

Window id is `M1_LOGICAL_RECOVERY_BASE` (`0x5000`) >> 12. Not an Apple SoC base.

| Offset | Width | Access | Semantics |
| --- | --- | --- | --- |
| `0x000` | 32 | RO | Status: bit0 ready, bit1 signature_verified (always 0 in L1) |
| `0x004` | 32 | RO | Last accepted sequence |
| `0x008` | 32 | RO | Payload length |

## AIC IRQ

Graph-local sources **0–3** are **timer** / **storage** / **display** /
**recovery** in Rust `M1MachineGraph`. Successful ingest raises
`RECOVERY_SOURCE` (**3**).

C bridge maps the same line: `VF_M1_RECOVERY_IRQ_LINE` (**3**) via
`vf_m1_guest_aic_set_line` / `recovery_sync_aic_irq` while ready.
Apple Recovery / DT IRQ numbers are **not** claimed; this is the sources 0–7
overlay only.

## Deferred

- Apple signature / trust-chain verification (`signature_verified` stays false)
- RecoveryOS BaseSystem / IPSW media staging
- Physical Apple recovery MMIO bases or DT bindings
- Any claim that recovery IRQ correctness unlocks `macos_boot_verified`
