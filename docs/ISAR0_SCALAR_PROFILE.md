# Exact ISAR0 read in the bounded scalar profile

## Current Status

The bounded software profile does not implement the extensions described by
ID_AA64ISAR0_EL1. Its explicit value is derived field by field, not from an
unknown-register fallback or the feature value of an original machine.

## Contract

Only MRS ID_AA64ISAR0_EL1, S3_0_C0_C6_0 (Rt-cleared encoding 0xd5380600),
returns this profile value. The C read API uses key 0x4030. EL1 and live
HCR_EL2=0/SCR_EL3=0 are required, including on cached native execution.
Writes, other exception levels, unsupported controls and unknown neighbors
retain the bounded rejection policy. Success uses XZR destination semantics,
preserves SP/NZCV and memory, and advances execution once. Rejection does not
write a destination or retire; existing exception reporting still applies.
ZFR0 behavior, ISAR1 reset values, CPU layout, cache keys and readiness remain
unchanged. No PFR values or full architectural version compliance are asserted.

| Field | Bits | Explicit profile decision |
| --- | --- | --- |
| Reserved | 3:0 | Zero |
| AES | 7:4 | No AES/PMULL |
| SHA1 | 11:8 | No SHA1 |
| SHA2 | 15:12 | No SHA256/SHA512 |
| CRC32 | 19:16 | No guest CRC32 instructions |
| Atomic | 23:20 | No LSE |
| TME | 27:24 | No transactional memory extension |
| RDM | 31:28 | No rounding doubling multiply accumulate extension |
| SHA3 | 35:32 | No SHA3 instructions |
| SM3 | 39:36 | No SM3 |
| SM4 | 43:40 | No SM4 |
| DP | 47:44 | No SDOT/UDOT |
| FHM | 51:48 | No FMLAL/FMLSL |
| TS | 55:52 | No FlagM/FlagM2 |
| TLB | 59:56 | No outer-shareable or range TLBI extension |
| RNDR | 63:60 | No RNDR/RNDRRS |

Every extension field is zero. Baseline exclusive operations do not imply LSE;
baseline TLBI does not imply the TLB extension; internal software CRC does not
implement a guest CRC32 instruction. Existing C/Rust ISAR1 reset differences
remain a separate issue.

Primary references: [Arm Cortex-A520 Cryptographic Extension TRM, section 2.2,
table 2-2](https://documentation-service.arm.com/static/672e536f27eda361ad4da11a),
[QEMU field definitions](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/cpu.h),
[feature predicates](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/cpu-features.h),
and [read-only registration/access](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/helper.c).
The software inactive-control gate does not model all architectural ID traps.

## Validation

The independent `runtime/tests/verify_isar0_native.py --output <directory>`
2026-09-13 final run passed 928 direct C assertions, 35 reference tests, and
32 canonical provider tests in each of cached, uncached and forced-small-slot
modes. Sixteen independently assembled extension representatives, including
TSTART, were rejected with their exact bounded fault classes. Tests cover all
32 destinations, direct register API access and live control changes after
translation. Exposed run results, ordered requests and RAM matched across modes.

Actual Cortex-A72 reads over 32 destinations observed hardware ISAR0=0x11120.
This verifies register access and destination semantics; it is not a zero-value
hardware oracle. The runtime's zero value is the explicit field-derived policy
above. The existing ZFR0 independent regression passed with unchanged sources.
All native proof processes were reaped and source hashes were preserved.
Existing preos tests passed 115/115; strict host/UEFI COFF C compilation and
Rust x86_64-unknown-uefi checking passed.

Fresh cached, uncached and unobserved authored mapped EFI builds passed all
10 checks. Each retired 65,536 instructions and completed 26,175 data operations
with provider status zero. Cached and unobserved builds used 128 writable and
127 executable protection transitions; uncached used 65,537 and 65,536, with no
failures. Reported execution and observed request windows matched. The old
initialization binary failed the mapped-profile acceptance gate. Source,
binary and authored input hashes were preserved. These are authored OVMF
results, not physical hardware or macOS boot evidence.

## Target State

Maintain this exact feature policy as instruction coverage grows. Any future
nonzero advertisement requires complete implementation and independent evidence
for the corresponding extension. Actual original execution and physical/macOS
boot remain separate validation boundaries.
