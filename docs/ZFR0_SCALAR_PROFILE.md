# Exact ZFR0 read in the bounded scalar profile

## Current Status

This software profile provides neither SVE nor SME. PFR0 and PFR1 remain
unsupported; no vector feature advertisement or execution is added.

## Contract

Only MRS ID_AA64ZFR0_EL1 (S3_0_C0_C4_4, instruction with Rt cleared
0xd5380480) returns zero, and only at EL1 with HCR_EL2 and SCR_EL3 both zero.
All other controls and exception levels retain the bounded system-register
trap policy. MSR and neighboring unsupported IDs remain rejected. This EL0
policy does not implement every FEAT_IDST exception configuration.

Every successful read writes the 64-bit result with XZR destination semantics,
preserves SP/NZCV and memory, and advances execution once. Rejection does not
write a destination or retire the instruction; ordinary exception reporting
remains active. Native code checks live controls before writing the result,
including on cached execution. No CPU field, ABI or cache-key change is needed.
The C system-register read API applies the same gate to key 0x4024.

Arm DDI0616 B.a page 983 specifies this encoding and the EL1 access conditions.
HCR.TID3 trapping depends on applicable architectural conditions; this bounded
implementation accepts only inactive HCR/SCR rather than modeling those cases.
[Arm SME supplement](https://documentation-service.arm.com/static/6526e1bd9e189a266cef8412).

Authored QEMU 8.2.2 cortex-a72 EL1 reads return zero. The independently retained
max,sve=off,sme=off observation returns nonzero ZFR0 despite cleared PFR feature
fields. Zero here is an explicit scalar-profile contract, not an inference from
those QEMU options or a universal hardware value. Relevant model definitions:
[QEMU SVE option setter](https://github.com/qemu/qemu/blob/v8.2.2/target/arm/cpu64.c#L264-L275)
and [QEMU max](https://github.com/qemu/qemu/blob/v8.2.2/target/arm/tcg/cpu64.c#L1141-L1151).

## Validation

The independent `runtime/tests/verify_zfr0_native.py --output <directory>`
run on 2026-09-13 passed 912 direct native assertions, 32 actual Arm Cortex-A72
oracle vectors compared with native and Rust execution, 34 reference tests,
and 31 canonical v2 provider tests in each of cached, uncached and forced-small
slot modes. Exposed run results, ordered requests and RAM matched across modes.
Live generated-code guards were checked in both directions after compilation,
including high control bits, EL2/EL3, every destination and direct C API reads.
The QEMU process was reaped and all proof source hashes remained unchanged.

Existing preos tests passed 115/115. Strict host and UEFI COFF C compilation
and the Rust x86_64-unknown-uefi check passed. Independent source review found
no required implementation correction.

Authored mapped EFI consumption passed all 10 checks with fresh cached,
uncached and unobserved builds. Each retired 65,536 instructions and completed
26,178 data operations with provider status zero. Cached and unobserved builds
used 121 writable and 120 executable transitions; uncached used 65,537 and
65,536. All protection operations succeeded. Reported execution and observed
request windows matched; observation did not change execution. The old firmware
negative control was rejected. Source, binary and authored input hashes were
preserved. These are authored OVMF results, not physical or macOS boot evidence.

## Target State

Preserve this exact scalar-profile contract as further instruction families
are implemented. Normal startup readiness remains unchanged; real platform
initialization and physical macOS boot require separate evidence.
