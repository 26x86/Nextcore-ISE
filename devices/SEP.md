# SEP mailbox L1 (Secure Enclave trust-boundary stub)

First-party **L1 mailbox** stub for the AP ↔ SEP handshake surface. Behaviour
facts are taken from Inferno `sep-sim.c` / `sep.c` at sync
`cc4302a99167abec69b714cfd00c38caece7e7de` (GPL reference, behaviour-only).
Contract: `docs/research/INFERNO_DEVICE_MODELS.md` SEP section.

## Tier boundary

| Tier | What it is | Public tree |
| --- | --- | --- |
| **L0** | Absent — no guest-visible SEP surface | historical |
| **L1** | Mailbox interface + stage status codes | **`sep_mailbox_v1`** (this stub) |
| L2+ | SEP ROM + SEP OS / IMG4 / crypto / GPIO/I2C | **blocked** — never stage blobs |

L1 intentionally stops at the mailbox. It does **not** load SEP ROM, SEP
firmware, tickets, keys, or IMG4 payloads. `BOOT_TZ0` / `BOOT_IMG4` / nonce /
keystore ops receive `L1_REJECT` and leave status at `BOOTSTRAP`.

## Guest-visible L1 contract

1. After reset the mailbox is **present** (`FLAG_PRESENT`) and stage status is
   `BOOTSTRAP` (1). An `ANNOUNCE_STATUS` word is queued in `MSG_OUT`.
2. Bootstrap endpoint **255** accepts `PING` (1) → `PING_ACK` (101) and
   `GET_STATUS` (2) → `STATUS_REPLY` (102) with current status data.
3. Other endpoints / firmware boot ops → `L1_REJECT` (0xFE, graph-local).
4. Status never advances to `ACTIVE` (2) at L1 — that would imply firmware.

## Graph-local MMIO (`VF_M1_MMIO_WINDOW_SEP` = `0xb`)

Not Apple A7IOP/AKF physical MMIO. Same shim style as SMC:

| Offset | Width | Access | Semantics |
| --- | --- | --- | --- |
| `0x000` | 32 | RO | Stage status (`SLEEPING`/`BOOTSTRAP`/`ACTIVE`) |
| `0x004` | 32 | RO | Tier = `VF_SEP_TIER_L1_MAILBOX` (1) |
| `0x008` | 32 | RO | Flags: `PRESENT`, `REPLY_PENDING` |
| `0x010` | 64 | RO | Last reply mailbox word (`MSG_OUT`; clears pending) |
| `0x018` | 64 | WO | Submit mailbox word (`MSG_IN`) |

Packed message (little-endian 64-bit): `ep`, `tag`, `op`, `param`, `data`.

## AIC IRQ

Inferno wires SEP `interrupts` from the device tree into the full AIC (large
line numbers), not the graph-local sources **0–7** overlay used by Rust
`M1Aic` / `vf_m1_guest_aic_set_line`. L1 therefore **defers** AIC wiring.
`vf_sep_mailbox_irq_pending` exists for unit tests / future bridge sync; the
bridge does **not** assert an AIC line for SEP at this tier.

## Deferred

- Apple A7IOP/AKF register map and physical base addresses
- SEP ROM / SEP OS / `sep-rom=` / `sep-fw=`
- `BOOT_TZ0` / `BOOT_IMG4` acceptance and `ACTIVE` promotion
- Endpoint advertise, OOL DMA, keystore, ART, crypto engines
- AIC DT interrupt lines and `dart-sep` coupling
- Any claim that SEP correctness unlocks `macos_boot_verified`
