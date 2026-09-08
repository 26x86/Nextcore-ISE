# Stage-1 physical output width

Current: the bounded reference walker previously ignored TCR_EL1.IPS. It could
return addresses outside the configured physical range and pass oversized table
addresses to the caller. BP27 enforces the selected output width. Native JIT
translated-memory execution remains a separate, unsupported path.

The supported IPS encodings 0..5 select 32, 36, 40, 42, 44, 48-bit addresses. Wider
regimes and LPA2 DS are rejected atomically; a failed configuration leaves the
previous table/ASID/TLB state intact. TTBR ASID bits are excluded from the base.
An out-of-range root or next-table address produces AddressSize before any read
at that address. Page and block outputs, including block offsets from the VA,
are checked. Cached blocks obey the same range so a cache hit cannot admit an
invalid physical output. Disabled translation remains identity.

These are guest architectural address checks, separate from actual RAM/IO bounds.
The physical reader still validates whether an in-range address is backed by RAM
or a supported device. Unsupported descriptor/permission regimes remain outside
this change. No physical host pointer is formed from a guest descriptor here.

The 16 MMU unit tests include 24 upper/lower/width/granule page-boundary mappings,
first-forbidden output and table checks, reserved 16 KiB L1 blocks and valid L2 block cache boundaries,
and rejected-regime preservation. An authored AArch64 oracle executes AT S1E1R:
for both 4 KiB and 16 KiB, 4 GiB leaf output faults under IPS=32 bits, succeeds under IPS=36 bits,
and 4 GiB table/root references fault with AddressSize under IPS=32 bits. L3 page chains add four cases. Two further cases reject reserved DS=0 16 KiB
L1 blocks at both low and high offsets with Translation faults. All 14 cases
pass in QEMU's independent CPU model. A changed descriptor that should succeed
under IPS=32 bits causes the oracle's expected-fault check to fail, proving the guard.

Run `cargo test --manifest-path runtime/preos/Cargo.toml mmu::tests` and
`python3 tools/probe_mmu_physical.py`. The test oracle does not run Apple code
or prove native JIT MMU execution.

References: [Arm Cortex-A73 TCR_EL1](https://documentation-service.arm.com/static/5e7b6c837158f500bd5c03fc),
[Arm memory management](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/LearnTheArchitecture-MemoryManagement-101811_0100_00_en.pdf),
and [Arm memory pseudocode](https://developer.arm.com/documentation/ddi0602/2026-06/Shared-Pseudocode/shared-functions-memory).

Review follow-up: the initial unit fixture incorrectly accepted a 16 KiB L1
block. Independent execution exposed that assumption; this is reserved without
LPA2/DS=1, which the bounded walker does not support. Reject it before output
address or access-flag checks and never cache the descriptor. See Arm's
[FEAT_LPA2 description](https://documentation-service.arm.com/static/66fab3ba1669c0388dca72f7).
