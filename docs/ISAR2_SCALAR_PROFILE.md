# Exact ISAR2 read and baseline PAC address selection

## Current Status

The bounded scalar profile implements baseline QARMA5 address authentication.
It does not implement PAuth2, constant PAC fields or the other extensions listed
below. Asymmetric address-size qualification identified an AddPAC selector error
shared with QEMU 8.2.2. Agreement with that emulator alone is insufficient.

## Contract

Only MRS ID_AA64ISAR2_EL1, S3_0_C0_C6_2, Rt-cleared encoding 0xd5380640,
returns the explicit value zero. The C read API uses key 0x4032. Require EL1
and live HCR_EL2=0/SCR_EL3=0, including on native cache hits. Preserve XZR,
SP/NZCV, memory and exact retirement/fault accounting. Writes, other ELs,
unsupported controls and unknown registers retain existing rejection policy.
No CPU layout, cache key, ISAR1, ZFR0, ISAR0 or readiness changes are implied.

| Field | Bits | Bounded zero policy |
| --- | --- | --- |
| WFXT | 3:0 | No timed WFE/WFI extension |
| RPRES | 7:4 | No increased reciprocal estimate precision; no FP implementation claim |
| GPA3 | 11:8 | No generic QARMA3 authentication |
| APA3 | 15:12 | No address QARMA3 authentication |
| MOPS | 19:16 | No memory copy/set extension |
| BC | 23:20 | No branch consistency extension |
| PAC_frac | 27:24 | No constant PAC field behavior |
| CLRBHB | 31:28 | No clear branch history extension |
| SYSREG_128 | 35:32 | No 128-bit system register extension |
| SYSINSTR_128 | 39:36 | No 128-bit system instruction extension |
| PRFMSLC | 43:40 | No SLC prefetch extension |
| RPRFM | 51:48 | No range prefetch extension |
| CSSC | 55:52 | No common short sequence compression extension |
| ATS1A | 63:60 | No ATS1A extension |

Bits 47:44 and 59:56 are zero under the pinned field map, not a claim that later
architecture revisions cannot allocate them. Existing scalar shifts, ordinary
conditional branches and QARMA5/PACGA do not imply these newer extensions.

For the supported non-TBI APA1 profile, signing selects T0SZ/T1SZ from pointer
bit 63; authentication and stripping select from bit 55. Selected TnSZ remains
16 or 17. Invalid controls and unsupported widths retain rejection. The
constant-field override belongs to EnhancedPAC2 plus ConstPACField and is not
enabled by baseline PAC support. See [mapped PAC qualification](MAPPED_PAUTH_V2.md).

Primary references: Arm-authored [DDI0596 ID121321, hosted mirror](https://student.cs.uwaterloo.ca/~cs452/docs/rpi4b/ISA_A64_xml_v88A-2021-12_OPT.pdf),
printed pages 2941–2942, 2947, 2952 and 2961; pinned [field definitions](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/cpu.h#L2046-L2059)
and [read-only register/access definition](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/helper.c#L8507-L8511).
The software inactive-control gate does not implement every architectural ID trap.

## Validation

Independent native, reference, provider, live-control and adapted APA1
address-selection proofs are required before publication. Existing ISAR0 tests
used ISAR2 as an unknown neighbor: admitting this exact register requires an
explicit positive test and a genuinely unsupported replacement negative.
Historical evidence must remain unchanged.

## Target State

Keep feature advertisements coupled to implemented semantics. An adapted QEMU
control must be labeled separately from an identical-state execution. Authored
native and EFI tests do not establish original initialization, physical display
or macOS boot readiness.
