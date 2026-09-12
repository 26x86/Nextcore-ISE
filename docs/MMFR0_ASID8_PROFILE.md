# MMFR0 and immutable eight-bit ASID context

## Current Status

This contract replaces the prior hardware-like MMFR0 literal with a bounded
software memory model. Independent validation is required before publication.

## Target State

Exact MRS S3_0_C0_C7_0 (0xd5380700 with variable Rt), C key 0x4038, returns
0x0f100005 only at EL1 with live inactive HCR/SCR. API and reference reads use
the same explicit constant; retained ID storage resets to that value for ABI
compatibility and cannot create a second identity by mutation. Writes and other
accesses retain bounded rejection. SP/NZCV, layout and other ID policies remain.

This non-secure EL1 diagnostic model has 48-bit maximum PA, eight-bit ASIDs,
little-endian execution, 4 KiB/16 KiB stage-1 pages, no 64 KiB pages and no
EL2/stage-2 or secure-memory service. Generic reference EL2/EL3 bank tests remain
separate. The stage-2 granule fields are read-only required zero when EL2 is
absent. ECV/FGT/ExS extensions are not advertised. No full Arm-version or original
hardware identity is claimed.

With AS=0, active ASID is the lower eight bits of the A1-selected TTBR tag:
((A1 ? TTBR1 : TTBR0) >> 48) & 255. Immutable profiles 1/3 permit these tags
and A1 at construction; high tag bits and AS=1 remain unsupported. Initialize
the strict walker with this tag, without changing full-snapshot equality,
table immutability, epoch or native cache lifetime. Generic reference control
writes reject unsupported tag/AS configurations before committing them.

Dynamic profile 2 explicitly retains ASID=0/A1=0/AS=0 in both validators. It
must not inherit relaxed immutable admission. Existing alignment, granule,
physical width, endian, descriptor, fault and transactional restrictions remain.

Primary ASID reference: [Arm Cortex-A57 DDI0488H](https://documentation-service.arm.com/static/5e906b9fc8052b1608760b6b),
sections 4.3.44/4.3.46, tables 4-56/4-58, pages 153–154. MMFR0 field definition:
[Arm-authored System Registers 2026.06, community mirror](https://arm.jonpalmisc.com/latest_sysreg/AArch64-id_aa64mmfr0_el1),
including the explicit EL2-absent condition for all three TGran*_2 fields.
The mirror is not an Arm-hosted endpoint.

## Validation boundary

Require independent A1/tag selection, cold/warm TLB and context-isolation tests;
unsupported writes and immutable snapshot rollback; unchanged dynamic admission;
all destination/live-control MMFR0 tests; native/reference/provider comparisons;
and authored EFI consumption. Authored evidence does not establish original
initialization, physical macOS display or boot readiness.
