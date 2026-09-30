# ADP L1 (Apple Display Pipe — framebuffer / vblank stub)

First-party **L1 framebuffer** stub for the BP37 display-first surface.
Behaviour facts are taken from Inferno `apple_displaypipe_v4.c` at sync
`cc4302a99167abec69b714cfd00c38caece7e7de` (GPL reference, behaviour-only):
frame update latches `OUTPUT_READY` / `FRAME_PROCESSED` into `int_status`, and
`irq[0]` asserts when `int_status & int_enable`. Contract:
`docs/research/INFERNO_DEVICE_MODELS.md` ADP section.

## Tier boundary

| Tier | What it is | Public tree |
| --- | --- | --- |
| **L0** | Absent — no guest-visible ADP surface | historical |
| **L1** | Present + mode configure + present/vblank IRQ | **`adp_display_v1`** (this stub) |
| L2+ | Full ADP V4 register map, scaler, MIPI DSI, GPU | **deferred** — no Metal/GPU claims |

L1 intentionally stops at a graph-local framebuffer contract that matches Rust
`M1Framebuffer` present semantics. It does **not** implement Apple ADP V4 MMIO,
hardware scaler, DSI bridge, or Metal acceptance.

## Guest-visible L1 contract

1. After reset the window is **present** (`FLAG_PRESENT`); tier reports
   `VF_ADP_TIER_L1_FRAMEBUFFER`.
2. Host/test `vf_adp_display_configure` (or bridge helper) sets width / height /
   stride / guest base with XRGB8888 bounds (max 8192).
3. CTRL write `PRESENT` (1) requires configure; sets enabled/presented, bumps
   `frame_count`, latches **vblank pending**.
4. CTRL write `DISABLE` (0) clears enabled; VBLANK_ACK write-1-to-clear drops
   pending.
5. No scaler / DSI / GPU ops — unsupported CTRL values fail closed.

## Graph-local MMIO (`VF_M1_MMIO_WINDOW_ADP` = `0x6`)

Window id is `M1_LOGICAL_DISPLAY_BASE` (`0x6000`) >> 12. Not Apple ADP physical
MMIO.

| Offset | Width | Access | Semantics |
| --- | --- | --- | --- |
| `0x000` | 32 | RW | CTRL: RO packed flags; WO `0` disable / `1` present |
| `0x004` | 32 | RO | Width |
| `0x008` | 32 | RO | Height |
| `0x00c` | 32 | RO | Stride (bytes) |
| `0x010` | 32 | RO | Frame count |
| `0x014` | 32 | RO | Tier = `VF_ADP_TIER_L1_FRAMEBUFFER` (1) |
| `0x018` | 32 | WO | VBLANK_ACK (write `1` clears pending) |
| `0x01c` | 32 | RO | Configure generation |

Flags (CTRL read): `PRESENT`, `CONFIGURED`, `ENABLED`, `PRESENTED`,
`VBLANK_PENDING`.

## AIC IRQ

Graph-local sources **0–3** are timer / storage / **display** / recovery in
Rust `M1MachineGraph`. Display present raises `DISPLAY_SOURCE` (**2**).

C bridge maps the same line: `VF_M1_ADP_IRQ_LINE` (**2**) via
`vf_m1_guest_aic_set_line` / `adp_sync_aic_irq` when vblank is pending.
Inferno DT interrupt numbers are **not** claimed; this is the sources 0–7
overlay only.

**Metal Driver Track M1 freeze:** window `0x6` / AIC line **2** must not be
remapped by Metal feature work without updating
`docs/research/METAL_DRIVER_TRACK.md` (`M1_DISPLAY_PATH_FREEZE:window=0x6,irq=2`).

## Deferred

- Apple ADP V2/V4 physical register map and base addresses
- Hardware scaler (`apple_scaler.c`)
- MIPI DSI bridge / panel chain
- Metal / GPU acceleration and driver acceptance
- Any claim that ADP correctness unlocks `macos_boot_verified`
