# Integer scalar unscaled transfers

## Current Status

All thirteen forms are implemented in native direct/provider and Rust reference
paths. Independent authored native and EFI validation passes; no original or
physical macOS boot result is inferred from those checks.

## Contract

The native direct/provider decoders and canonical Rust reference accept thirteen
integer forms: STURB/H/W/X, LDURB/H/W/X, LDURSB W/X, LDURSH W/X, and LDURSW X.
The encoding uses bit21=0 and addressing mode bits11:10=00, with a signed
nine-bit immediate in bits20:12. Effective address is base plus the signed byte
displacement (-256 through 255), modulo 64 bits, without scaling or writeback.
Rn=31 selects SP; Rt=31 is ZR. Loads to W clear the upper 32 bits after any sign
extension. Stores use the low transfer-width bits. Flags remain unchanged.

The common scalar size/opc validation rejects SIMD/FP, PRFUM and reserved
integer combinations. Adjacent pre/post-index, unprivileged and other addressing
classes remain unsupported. Existing unsigned scaled and register-offset
semantics are unchanged. Rn=Rt is legal because there is no writeback: the
address uses the pre-load base value.

Public encoding contract: [QEMU pinned decoder](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/tcg/a64.decode),
load/store register unscaled immediate patterns (`imm:s9`, no pre/post indexing).
The implementation is independently authored from that public contract.

Existing SP checks use the unmodified base. Native M0 Device alignment, fixed
profile1 A=1 and profile3 Normal-NC A=0 policies retain their existing behavior.
Provider transactions still validate every covered byte before committing data;
failures preserve base/destination/RAM and do not retire. Success makes one data
operation and retires once. The Rust reference retains its explicit unsupported
translated-unaligned boundary; this ISA addition does not expand that bus API.
Fresh fetch, native cache keys and memory/control provider ABI are unchanged.

Independent authored tests must cover all thirteen forms, displacement endpoints,
negative byte offsets, SP/ZR, same base/destination, signed W/X results, unchanged
base/flags, precise faults and rejected neighboring encodings. Native/reference,
provider and EFI evidence must remain distinct from original or physical boot.

## Validation (2026-09-12)

Run the independent native proof from the module root:

```sh
python3 runtime/tests/verify_unscaled_native.py --output /tmp/nextcore-unscaled-new-proof
```

The final r5 proof preserves all tested source bytes and passes 33 tests in each
of cached, uncached and forced-small-slot modes. The direct generated-x86 suite
passes 546 cases and 2,022 assertions: thirteen forms, seven displacements,
SP/general bases, and destination/source roles including ZR and base overlap.
Thirty-nine authored LLVM/QEMU observations match both generated-x86 execution
and the canonical Rust reference. Provider coverage includes 117 combinations
of thirteen forms, three offsets and three transfer-register roles in each mode,
plus 64 cross-page precise-fault cases. Tests cover M0/SP alignment and reject
SIMD, unprivileged, pre/post-index and reserved neighboring encodings.

The independent native test initially detected missing unscaled-store WnR
classification in the C exception encoder. That classification was corrected;
the final proof passes with exact fault syndromes. The existing scalar native
regression also passes 106,496 cases and 426,981 assertions. Existing preos
Rust tests (115), strict freestanding UEFI C compilation and no_std UEFI checking
pass. The final authored EFI r2 consumer passes all ten checks. These evidence
layers remain distinct; the QEMU vectors use authored instructions, not an
original OS image.

## Target State

Preserve these exact decoder, fault and no-writeback contracts across subsequent
runtime changes. Original replay and physical machine acceptance are separate
integration checks. No unsupported neighboring instruction, memory regime or
normal-startup readiness gate is relaxed by this addition.
