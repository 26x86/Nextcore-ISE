# GPIO (Apple General Purpose I/O) device layer

Register facts are extracted from qemu-t8030 `hw/gpio/apple_gpio.c` and
`include/hw/gpio/apple_gpio.h` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference, behaviour-only).
This is an **iOS T8030 topology reference** — not a macOS M1 guest contract.

## QOM and topology

| Field | Value |
| --- | --- |
| QOM type | `apple.gpio` (`TYPE_APPLE_GPIO`) |
| MMIO size | DT `reg[1]` (per-instance) |
| Max pins | 512 (`GPIO_MAX_PIN_NR`) |
| Max IRQ groups | 7 (`GPIO_MAX_INT_GRP_NR`) |
| Access width | 32-bit aligned only |

DT properties consumed by `apple_gpio_create()`:

| Property | Purpose |
| --- | --- |
| `reg` | `(offset, size)` MMIO window |
| `name` | MMIO region label / device id |
| `#gpio-pins` | pin count (`npins`, `< 512`) |
| `#gpio-int-groups` | interrupt group count (`nirqgrps`) |
| `AAPL,phandle` | GPIO controller phandle |

Each instance exposes `npins` GPIO inputs, `npins` GPIO outputs, and
`nirqgrps` IRQ lines (one per interrupt group).

## MMIO map

| Macro | Offset | Purpose |
| --- | --- | --- |
| `rGPIOCFG(n)` | `0x000 + n×4` | per-pin configuration (`n` = pin index) |
| `rGPIOINT(g, n)` | `0x800 + g×0x40 + ((n+31)>>5)×4` | group `g` interrupt pending (bit per pin) |
| `rGPIO_NPL_IN_EN` | `0xC48` | NPL input-enable shadow (`s->npl`) |

Valid access ranges in the reference:

- `rGPIOCFG(0)` … `rGPIOCFG(511)`
- `rGPIOINT(0,0)` … `rGPIOINT(7,511)`
- `rGPIO_NPL_IN_EN`

Unknown offsets log guest error and read as zero.

## Pin configuration word (`rGPIOCFG`)

| Bits | Field | Values |
| --- | --- | --- |
| 0 | `DATA` | 0/1 output level |
| 2:1 | `CFG` | GP in/out, level/edge IRQ modes, disable |
| 6:5 | `FUNC` | GPIO / ALT0 / ALT1 / ALT2 |
| 8:7 | `PULL` | none / down / up-strong / up |
| 9 | `INPUT_ENABLE` | input buffer enable |
| 14 | input type | CMOS vs Schmitt |
| 18:16 | `INTR_GRP` | IRQ group 0–6; `7` = masked |

Preset composites from reference:

| Name | Value role |
| --- | --- |
| `CFG_DISABLED` | disabled, IRQ masked |
| `CFG_IN` | GPIO input, IRQ masked |
| `CFG_OUT` / `CFG_OUT_0` / `CFG_OUT_1` | GPIO output |
| `CFG_FUNC0` … `CFG_FUNC2` | alternate function |

### Read behaviour

For `FUNC_GPIO` + `CFG_GP_IN`, bit 0 reflects live input level from the GPIO
input line; other fields read stored config.

### Write side effects

- Updates stored config and may drive `out[pin]` (output level or ALT0 → high).
- Level-sensitive IRQ groups set/clear pending bits in `int_cfg[group]`.
- Edge modes compare against `old_in[]`.
- IRQ line asserted when any pending bit set in group; write-1-to-clear on
  `rGPIOINT` clears pending and may lower IRQ.

## GPIO address encoding (firmware helpers)

Reference macros for packed GPIO numbers:

```text
GPIO2PIN(gpio)        = gpio & 7
GPIO2PAD(gpio)        = (gpio >> 8) & 0xFF
GPIO2CONTROLLER(gpio) = (gpio >> 24) & 0xFF
```

## Reset state

All pins → `CFG_DISABLED`; interrupt pending arrays cleared; input shadow
registers zeroed.

## Implementation

First-party stub: `apple_gpio_v1.c` / `apple_gpio_v1.h` with unit test
`test_apple_gpio.c` (`sandbox/devices/build.py build_apple_gpio_test`).

## preOS M1GuestBus deferral

There is **no** graph-local GPIO window in `M1MachineGraph` today. VMApple TCG
records GPIO at `0x20060000/0x1000` (QEMU reference only); the native M1 graph
uses compact 4 KiB logical windows ending at `M1_LOGICAL_SMC_BASE` (`0xa000`).
GPIO dispatch through `preos_bridge.c` is deferred until a bounded logical window
and AIC wire-up contract exist. Unit tests remain the evidence surface.

## Deferred

ALT1/ALT2 function routing, NPL write path, full edge-IRQ self-test, AIC IRQ
wiring, and M1/t8103 physical bases remain unvalidated. Do not substitute
generic `virt` GPIO or PrimeCell PL061 without differential tests.
