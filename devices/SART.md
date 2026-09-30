# SART device layer (reference)

Apple SART secondary-IOMMU register facts are checked against qemu-t8030
`hw/arm/apple_sart.c` and `include/hw/arm/apple_sart.h` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference, behaviour-only).
This is an **iOS T8030 topology reference** — not a macOS VMApple guest contract.

## QOM and topology

| Field | Value |
| --- | --- |
| QOM type | `apple.sart` (`TYPE_APPLE_SART`) |
| IOMMU MR type | `apple.sart.iommu` |
| Register file size | `0x8000` bytes (`reg[1]` from DT) |
| IOMMU VA span | 42 bits (`SART_MAX_VA_BITS`) |
| Regions | 16 (`SART_NUM_REGIONS`) |
| Access width | 32-bit aligned only |

## MMIO base (DT-derived, T8030)

Physical base is **not hard-coded**. `t8030_create_sart()` maps the register
window at `soc_base_pa + reg[0]` where `soc_base_pa = arm-io/ranges[1]`.

| DT node | Parent | Consumer |
| --- | --- | --- |
| `sart-ans` | `arm-io/` | ANS/NVMe path (`t8030_create_ans` links IOMMU MR) |

Required DT properties:

| Property | Purpose |
| --- | --- |
| `reg` | `(offset, size)` — size becomes MMIO window (`reg[1]`, typically `0x8000`) |
| `sart-version` | Region decode version (1, 2, or 3) |

The device exposes two sysbus MMIO regions: the register file and the IOMMU
memory region (same QOM object, second region index).

## Region table encoding (by `sart-version`)

Region `i` (0–15) fields are **derived** from the flat `reg[]` array; there is
no separate struct beyond decode helpers.

### Version 1

| Field | Source offset | Extract |
| --- | --- | --- |
| Size (4 KiB pages) | `0x000 + 4×i` | bits → mask `0x7FFFF` |
| Address (4 KiB pages) | `0x040 + 4×i` | mask `0xFFFFFF` |
| Flags | `0x000 + 4×i` | low bits after clearing size mask |

### Version 2

| Field | Source offset | Extract |
| --- | --- | --- |
| Size (4 KiB pages) | `0x000 + 4×i` | mask `0xFFFFFF` |
| Address (4 KiB pages) | `0x040 + 4×i` | mask `0xFFFFFF` |
| Flags | `0x000 + 4×i` | low bits after clearing size mask |

### Version 3

| Field | Source offset | Extract |
| --- | --- | --- |
| Flags | `0x000 + 4×i` | full word |
| Address (4 KiB pages) | `0x040 + 4×i` | mask `0x3FFFFFFF` |
| Size (4 KiB pages) | `0x080 + 4×i` | mask `0x3FFFFFFF` |

## Translation behaviour (reference)

- IOVA is shifted right by 12 (4 KiB granule).
- If `(addr_page, size, flags)` region matches and `flags != 0`, translation
  is RW with 4 KiB mask (`addr_mask = 0xFFF`).
- Otherwise default entry is RW identity at page granularity (reference model
  does not fault-closed on miss).
- Any register write that changes a region triggers IOMMU unmap notifications
  for the old span before updating cached `AppleSARTRegion` state.

## NextCore stub (`sart_v1`)

Public stub implements:

| API | Behaviour |
| --- | --- |
| Region MMIO R/W | Version 1/2/3 banks; 32-bit aligned; bounds-checked |
| `generation` | Bumps on region-table writes (notifier stand-in) |
| `vf_sart_region_decode` | Versioned addr/size/flags per table above |
| `vf_sart_translate` | Identity `pa = iova & ~0xFFF`; returns 1 on flagged hit, 0 on miss |
| Bridge | `vf_m1_guest_sart_generation` / `vf_m1_guest_sart_translate` |

No ANS/NVMe IOMMU MR, no DART page walk coupling, no physical Apple base.

## Deferred

macOS SART presence, MMIO base, and ANS coupling are unvalidated. Do not treat
T8030 `sart-ans` as Golden Gate storage proof. End-to-end DART←SART DMA chain
remains deferred until guest traces demand it.
