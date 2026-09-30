# DART device layer (reference)

Apple DART IOMMU register facts are checked against qemu-t8030
`hw/arm/apple_dart.c` and `include/hw/arm/apple_dart.h` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference, behaviour-only).
This is an **iOS T8030 topology reference** — not a macOS VMApple guest contract.

## QOM and topology

| Field | Value |
| --- | --- |
| QOM type | `apple.dart` (`TYPE_APPLE_DART`) |
| IOMMU MR type | `apple.dart.iommu` |
| Max instances | 2 (`DART_MAX_INSTANCE`) |
| Max streams (SID) | 16 (`DART_MAX_STREAMS`) |
| Instance MMIO window | `0x4000` bytes per DT `reg` tuple |
| Access width | 32-bit aligned only |

## MMIO base (DT-derived, T8030)

Physical base is **not hard-coded** in the emulator sources. `t8030.c` maps each
instance at `soc_base_pa + reg[i*2]` where `soc_base_pa = arm-io/ranges[1]`.

T8030 machine creates these DT nodes under `arm-io/`:

| DT node | Role |
| --- | --- |
| `dart-usb` | USB DMA path |
| `dart-sio` | SIO / storage-adjacent path |
| `dart-disp0` | Display DMA path |

Additional DART names appear in `xnu.c` device lists (`dart-aop`, `dart-pmp`,
`dart-sep`, …) but are not all instantiated on T8030 bring-up.

Device-level DT properties consumed by `apple_dart_create()`:

| Property | Purpose |
| --- | --- |
| `reg` | Pairs of `(offset, size)` per instance window |
| `instance` | Triplets describing instance type (`DART`, `SMMU`, `DAPF`) |
| `page-size` | Translation page size (4096 or 16384) |
| `sids` | Bitmask of enabled stream IDs |
| `bypass` | Per-SID bypass bitmask |
| `bypass-address` | Identity-map target when bypassed |
| `interrupts` | Wired to AIC (one line per instance) |

## Per-instance register map (DART type)

Offsets are within each `0x4000` instance MMIO window. Only `DART` instances
implement translation; `SMMU`/`DAPF` instances return zero on read.

| Offset | Name | R/W | Notes |
| --- | --- | --- | --- |
| `0x000` | PARAMS1 | R/W | Page shift in bits 27–24 (`DART_PARAMS1_PAGE_SHIFT`) |
| `0x004` | PARAMS2 | R/W | Bit 0: bypass support |
| `0x020` | TLB_OP | R/W | Bit 2 BUSY; bit 20 INVALIDATE triggers IOTLB flush |
| `0x034` | SID_MASK_LO | R/W | Low 32 bits of invalidate SID mask |
| `0x038` | SID_MASK_HI | R/W | High 32 bits of invalidate SID mask |
| `0x040` | ERROR_STATUS | R/W1C | Bit 31 FLAG; bits 0–11 fault codes; bits 24–27 fault SID |
| `0x050` | ERROR_ADDRESS_LO | R/W | Fault IOVA low |
| `0x054` | ERROR_ADDRESS_HI | R/W | Fault IOVA high |
| `0x060` | CONFIG | R/W | Bit 15 LOCK |
| `0x080` + `4×n` | SID_REMAP | R/W | Remap table (`n` 0–3 covers 16 bytes at 0x80) |
| `0x100` + `4×sid` | TCR | R/W | Bit 7 TXEN; bit 8 BYPASS_DART; bit 12 BYPASS_DAPF |
| `0x200` + `16×sid` + `4×idx` | TTBR | R/W | Bit 31 VALID; PA = `(val & 0xFFFFFFF) << 12`; 4 entries/SID |

### ERROR_STATUS fault codes (low bits)

| Bit | Code |
| --- | --- |
| 0 | TTBR invalid |
| 1 | L2 entry invalid |
| 2 | PTE invalid |
| 3 | Write protect |
| 4 | Read protect |
| 5 | AXI decode |
| 6 | AXI slave error |
| 7 | Region protect |
| 8 | CTRR write protect |
| 9 | Unknown |
| 11 | APF reject |

### Translation behaviour (reference)

- 3-level walk using `ttbr[sid][idx]` and in-memory PTEs (`DART_TTE_*`).
- IOTLB keyed by `(sid << 53) | (iova >> page_shift)`.
- TXEN clear or BYPASS_DART set → RW pass-through to bypass/identity path.
- Fault sets ERROR_STATUS/ERROR_ADDRESS and raises device IRQ.

## NextCore stub behaviour (2026-09-18)

`sandbox/devices/dart_v1.c` stores PARAMS/CONFIG/TLB_OP/ERROR/TCR/TTBR without a
page walk. TLB invalidate bumps `tlb_generation` (host/test-observable, same
pattern as SART). `vf_dart_simulate_fault(sid, code)` latches ERROR_STATUS with
fault SID in bits 24–27; FLAG high means `vf_dart_irq_pending()`.

`preos_bridge.c` wires FLAG → AIC line `VF_M1_DART_IRQ_LINE` (4) via
`vf_m1_guest_aic_set_line` / `vf_m1_guest_dart_simulate_fault`. W1C of the FLAG
bit deasserts the line. No fabricated guest MMIO registers were added for the
generation counter.

## Deferred

macOS Golden Gate DART topology, SID assignment, and guest driver traces are
**not** validated. Do not substitute x86 DMAR or VMApple GIC paths for native
DART proof. Page-table walk and DMA translation remain deferred.
