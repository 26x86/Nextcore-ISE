# Integer scalar register-offset memory

Current Status: Native/reference execution, actual provider integration and UEFI compilation verified; physical boot remains unverified.
Target State: Execute all integer scalar register-offset loads/stores through
native direct RAM and existing v1/v2/dynamic services, with precise fault state.

## Primary architectural basis

Arm DDI0602 June 2025, official A64 instruction set:
https://documentation-service.arm.com/static/685eb35c3cb5b729562ba389
Printed pages 511-512, 518-519, 522-523, 527-528, 532-533, 537-538 and
899-900, 903-904, 908-909 specify LDR/B/H/SB/SH/SW and STR/B/H register forms.
ExtendReg was inspected at PDF page 5421. The public instruction semantics,
not any operating-system bytes or operands, define this implementation.

## Decode and address contract

Recognize `(word & 0x3b200c00) == 0x38200800`; reject V=1 (SIMD/FP),
invalid size/opc forms, and option bit1=0. PRFM and reserved integer forms stay
undefined, consistent with the existing scalar unsigned-offset profile.
The thirteen supported forms are byte/halfword/word/doubleword stores and zero
loads, byte/halfword signed loads into W or X, and signed word loads into X.

Options 010/011/110/111 mean UXTW/LSL/SXTW/SXTX. Extend the low 32 bits
(unsigned or signed) or use all 64 bits of Rm, then shift left by size when
S=1, otherwise zero. Byte accesses accept either S value, both with shift0.
Rm31 is ZR; Rn31 is SP. Effective address is base plus offset modulo 64 bits
in the existing supported address profile. Check original SP alignment before
using the effective address. There is no base writeback. Rt31 reads zero for
stores and discards load values, while still performing the memory access.
Loads commit only after successful access; narrow result writes zero the high
32-bit register storage when the destination is W. Preserve PSTATE and source
registers unless one is also the successful load destination.

## Fault and implementation contract

Extend private memory_shape with register index/extension/shift metadata.
Native code computes the effective address using volatile registers and reuses
full-span bounds, alignment, load/store and sign-extension code. Provider paths
use a shared C address function, without changing callback ABI or service gates.
The Rust reference reuses its scalar memory path. arch.c recognizes this family
when creating native data-abort IL/WnR metadata; ISV remains0 as for existing
scalar instructions. v2/dynamic continue to preserve provider ESR/FAR provenance.

Decode failure precedes any data callback. Original SP faults precede EA checks.
On alignment/permission/translation/bounds failure, destination/base/index,
PSTATE, PC and retirement remain unchanged and FAR identifies the effective
address (original SP for SP faults). Actual service stores preflight the full
span. Existing host/provider failures remain host failures: a misbehaving store
callback may modify its RAM before returning a malformed reply, so this change
does not promise rollback across untrusted callbacks. No new MMIO, unaligned
mapped reference, MTE, SIMD, endian, control or unsupported-profile support is
claimed.

## Validation contract

Author independent cases for every size/opc, legal/illegal extension, S value,
negative signed and upper-contaminated W index, wrapping address addition,
SP/ZR roles and destination/source overlap. Compare actual generated x86 and
Rust reference against byte-array and arithmetic oracles. Verify full-span
bounds, alignment, original-SP priority and exact ESR/FAR without partial state.
Exercise real v1/v2/dynamic providers, mapped page granules, permission/translation
and callback failures. Compiled wrong extension/scaling/fault metadata mutations
must fail execution assertions. Compile actual Rust UEFI and C COFF targets.
These are memory instruction/build proofs; root owns EFI consumption and actual
macOS/physical-boot acceptance.


## Validation receipt (2026-09-12)

`tools/probe_register_offset_native.py` passed 13,312 generated-x86 cases with
58,418 assertions. The unchanged immediate-scalar native suite passed 106,496
cases and 426,981 assertions. The Rust reference passed 13,312 family cases
and additional decode/fault cases within 104 preOS tests.

Actual provider suites passed 16 v1, 14 v2 and 15 dynamic tests. Each family
matrix contains 1,664 forms/indices/destination-role cases: once for v1, for
both v2 page granules, and before/after dynamic enable at both granules (11,648
provider family executions). Additional cases verify invalid SIMD/PRFM/options,
no data callbacks on decode failure, data alignment, mapped translation,
permission/access-flag errors, full state/RAM preservation, and malformed-fault
rejection. Direct native/reference tests also verify original-SP priority and
full-span bounds with load destination aliasing the index.

Four compiled mutations failed execution assertions: lost signed index,
incorrect native scale, missing native scalar fault metadata, and incorrect
provider scale. The provider-scale mutant fails the actual C/Rust v2 family
matrix. Rust release UEFI and both native C translation units compile to the
actual UEFI/COFF targets. Source hashes and exact output are captured by the
standalone runner; no macOS or physical-boot claim follows from these results.
