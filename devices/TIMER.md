# Timer L1 (graph-local M1GuestBus counter / AIC source 0)

First-party **graph-local timer** stub aligned with Rust `M1Timer` and
`TIMER_SOURCE` (**0**). Behaviour matches the preOS machine-graph contract
(24 MHz counter, compare, enable/mask CTL). Not an Apple physical timer MMIO
block, not architectural CNTP/CNTV sysregs, and not a DT FIQ claim.

## Tier boundary

| Tier | What it is | Public tree |
| --- | --- | --- |
| **L0** | Absent — timer only in Rust graph | historical |
| **L1** | Counter/compare/CTL MMIO + pending → AIC line 0 | **`timer_v1`** (this stub) |
| L2+ | Architectural C-JIT timer source / FIQ path | **deferred** |

L1 stops at the graph overlay shared with Rust `M1MachineGraph`. Architectural
generic-timer sysregs remain owned by the C-JIT / Rust reference core
(`timer_counter` capability stays “no C-JIT timer source” for this stub).

## Guest-visible L1 contract

1. After reset: counter `0`, compare `0`, CTL `0`, frequency
   `VF_TIMER_FREQUENCY_HZ` (**24_000_000**).
2. Pending when `ENABLE` set, `MASKED` clear, and `counter >= compare`.
3. Guest may write compare (`0x08`, 64-bit) and CTL (`0x10`, 32-bit bits 0–1).
4. Counter and frequency are read-only via MMIO; host/test
   `vf_timer_advance` (or bridge helper) advances the counter.
5. Unsupported widths / CTL bits fail closed.

## Graph-local MMIO (`VF_M1_MMIO_WINDOW_TIMER` = `0x2`)

Window id is `M1_LOGICAL_TIMER_BASE` (`0x2000`) >> 12. Not an Apple SoC base.

| Offset | Width | Access | Semantics |
| --- | --- | --- | --- |
| `0x000` | 64 | RO | Counter |
| `0x008` | 64 | RW | Compare |
| `0x010` | 32 | RW | CTL: bit0 `VF_M1_TIMER_CTL_ENABLE`, bit1 `VF_M1_TIMER_CTL_MASKED` |
| `0x018` | 64 | RO | Frequency (24 MHz) |

## AIC IRQ

Graph-local sources **0–3** are **timer** / storage / display / recovery in
Rust `M1MachineGraph`. Timer pending raises `TIMER_SOURCE` (**0**).

C bridge maps the same line: `VF_M1_TIMER_IRQ_LINE` (**0**) via
`vf_m1_guest_aic_set_line` / `timer_sync_aic_irq` when pending.
Inferno / Asahi FIQ timer lines are **not** claimed; this is the sources 0–7
overlay only.

## Deferred

- Architectural CNTP/CNTV as the C-JIT interrupt source for this window
- FIQ delivery path shared with AIC (Inferno AIC note)
- Physical Apple timer MMIO bases or DT bindings
- Any claim that timer IRQ correctness unlocks `macos_boot_verified`
