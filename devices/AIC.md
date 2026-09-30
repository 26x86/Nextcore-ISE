# AIC device layer

The first-party model covers AIC v1 wired interrupt routing. Register facts are
checked against the [Asahi Linux AIC driver](https://github.com/AsahiLinux/linux/blob/asahi/drivers/irqchip/irq-apple-aic.c)
and bank offsets from qemu-t8030 `hw/intc/apple_aic.c` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference, behaviour-only).

## Implemented MMIO (adapted-port)

| Offset | Name | Read | Write |
| --- | --- | --- | --- |
| 0x0000 | REV | version 2 | — |
| 0x0004 | INFO/CAP0 | `(cpu_count-1)<<16 \| irq_count` | — |
| 0x000C | RST | — | reset wired state |
| 0x0010 | GLB_CFG | stored config | store config |
| 0x2000 | WHOAMI | accessing CPU | — |
| 0x2004 | EVENT/IACK | auto-ack ext IRQ `0x10000\|n` | — |
| 0x3000+4×n | TARGET | per-IRQ CPU mask | per-IRQ CPU mask |
| 0x4000+ | SW_SET | — | OR software pending |
| 0x4080+ | SW_CLR | — | AND-NOT software pending |
| 0x4100+ | MASK_SET | mask word readback | OR mask |
| 0x4180+ | MASK_CLR | mask word readback | AND-NOT mask |
| 0x4200+ | HW_STATE | level \| software | — |
| 0x5000+0x80×cpu | CPU view | alias of 0x2000 block | alias of 0x2000 block |

Banks use 128-byte strides (SW 0x4000, MASK 0x4100/0x4180) matching both Asahi
v1 and qemu-t8030 flat EIR layouts.

Acknowledgment masks the delivered IRQ. An asserted level is delivered again
after unmasking. CPU affinity and lowest-numbered priority are implemented.
Software pending and physical level are independent. Unknown MMIO, invalid
widths and out-of-range targets fail explicitly.

## preOS M1GuestBus overlay contract

Graph-local window `M1_LOGICAL_AIC_BASE` (`0x1000`, 4 KiB) splits ownership:

| Offset | Owner | Semantics |
| --- | --- | --- |
| `0x00` | Rust `M1Aic` | 64-bit pending bitmask (sources 0–7) |
| `0x08` | Rust `M1Aic` | 64-bit enable mask |
| `0x10` | Rust `M1Aic` | Read: route target for source 0; Write: acknowledge source |
| `0x18` | Rust `M1Aic` | Route `(source \| cpu_mask<<8)` |
| `0x04`, `0x0c`, `0x20+` | `aic_v1` via bridge | REV/INFO/RST/mask banks when non-conflicting |

**Offset overlap at `0x10`:** Apple `GLB_CFG` and the graph acknowledge path share
the same byte offset. Through `M1MachineGraph::mmio_*`, `0x10` is graph-owned
(acknowledge / route-target read); storing a non-source value such as sample
`GLB_CFG=0x29` fails closed. Direct `vf_m1_guest_mmio_*` (C bridge unit tests)
still reaches `aic_v1` `GLB_CFG` at the same offset.

**WHOAMI/EVENT (`0x2000+`) and CPU alias (`0x5000+`):** Outside the 4 KiB logical
window (`M1_LOGICAL_WINDOW_BYTES`). Graph absolute `AIC_BASE+0x2000` aliases the
next window (DART), so AIC WHOAMI is unreachable via graph MMIO; reachable only
via direct bridge dispatch (`VF_M1_MMIO_WINDOW_AIC` + offset, uncapped). Locked by
`aic_glb_cfg_whoami_4kib_window_overlap_contract` and bridge WHOAMI/MASK asserts.

**Dual IRQ state sync:** `vf_m1_guest_aic_set_line(irq, high)` mirrors graph
raises/acknowledges (sources 0–7 map 1:1 to `aic_v1` level lines). Rust `M1Aic`
remains authoritative for graph MMIO at `0x00–0x18`; `aic_v1` owns mask/TARGET
banks and EVENT delivery when accessed through the bridge. Device stubs wire
pending into the same overlay: timer → line **0**, storage attach → **1**,
ADP → **2**, recovery → **3**, DART → **4**, UART → **5**
(`VF_M1_TIMER_IRQ_LINE` / `VF_M1_STORAGE_IRQ_LINE` / `VF_M1_ADP_IRQ_LINE` /
`VF_M1_RECOVERY_IRQ_LINE` / `VF_M1_DART_IRQ_LINE` / `VF_M1_UART_IRQ_LINE`).
Standalone `ans_pci_v1` may optionally bind the same STORAGE line **1** via
`vf_ans_pci_bind_aic` (INTx|MSI delivery pending mirror; fail-closed unbound;
**≠** ownership of graph STORAGE window `0x4` / Apple DT IRQ / guest
acceptance). `preos_bridge` auto-wires bridge `ans_v1` → `ans_pci` (Identify CQ
irq_check → delivery_pending) and that AIC bind on `vf_m1_guest_mmio_reset`
(`vf_m1_guest_ans_*` / `vf_m1_guest_ans_pci_*` observability; fail-closed if
AIC/ans unbound).

## Deferred (documented in M1_SOURCE_PORT_MANIFEST.md)

IPI (0x2008–0x2030), per-CPU IPI pages (0x5024+), FIQ/event types, fast IPI
sysregs, AIC2/3 die stride, timer alias inside AIC MMIO, and privileged CPU
exception delivery. Not sufficient to boot iBoot or macOS. No GIC substitute.
The scheduler must serialize model accesses. MMIO base and DT identity belong in
config.plist Hardware; no physical Apple address is invented here.
