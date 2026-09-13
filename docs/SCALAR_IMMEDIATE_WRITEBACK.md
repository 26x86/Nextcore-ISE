# Scalar immediate writeback

## Current Status

This extension adds pre-indexed and post-indexed addressing to the existing
thirteen scalar integer memory forms. Independent native, reference, provider
and authored EFI validation passed on 2026-09-13. No original image bytes or
instruction coordinates are part of this contract or its fixtures.

## Contract

The additional family predicate is `(word & 0x3b200400) == 0x38000400`.
Bits 11:10 select post-index (01) or pre-index (11). The immediate is a signed
nine-bit byte displacement, from -256 through 255, without element scaling.
Pre-index accesses `base + displacement`; post-index accesses the original base.
Both compute the updated base modulo 2^64 and commit it only after the complete
memory transaction and provider reply validation succeed.

Supported transfers are stores and zero-extending loads at byte, halfword, word
and doubleword widths; signed byte/halfword loads to W or X; and signed word
loads to X. Existing result extension and flag preservation remain unchanged.
Rn=31 is SP; Rt=31 is ZR. Original SP alignment is checked before offset addition.
Non-SP Rn/Rt overlap is explicitly rejected before data access for both loads
and stores. This is Nextcore's bounded policy for the architectural constrained
unpredictable case, not a claim that rejection is its only possible behavior.
SIMD, prefetch, unprivileged and reserved forms remain rejected.

Faults preserve the original base, load destination, PC and retirement, subject
to the existing exception-record contract. Stores retain their WnR syndrome.
Direct native and reference paths retain their existing memory regime and
alignment restrictions. Canonical providers retain their existing complete-span
preflight and transactional behavior, including supported unaligned Normal
accesses; this extension does not change memory attributes or page tables.

Native code retains the checked effective address in R11 until successful
access, then saves the updated base. The existing provider memory shape modes
reuse successful-transaction-only writeback. No CPU layout, callback ABI, cache
key or readiness gate changes.

Primary sources are the pinned QEMU [scalar immediate encodings](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/tcg/a64.decode)
(`ldst_imm_pre` and `ldst_imm_post`) and [address/transaction ordering](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/tcg/translate-a64.c)
(`op_addr_ldst_imm_pre`, `op_addr_ldst_imm_post`, `trans_LDR_i`, `trans_STR_i`).
The implementation is independently authored and incorporates no QEMU code.

## Validation

The independent native proof passed 910 direct cases and 3,432 assertions.
Seventy-eight independently assembled vectors executed on QEMU's Arm CPU match
the generated x86 implementation and Rust reference. All 35 canonical tests
passed in each of cached, cache-disabled and 64-byte-slot modes (105 test runs).
The repeated post-index +8/pre-index -8 loop retires 32 instructions with 32
fetches and 22 data operations; exported result, ordered request and RAM
snapshots match across all three modes. These snapshots do not expose every
private CPU field or callback reply.

From the module root, reproduce the independent proof with:

```sh
python3 runtime/tests/verify_indexed_native.py --output /tmp/nextcore-indexed-proof
```

The existing preos suite passes 115 tests, and strict freestanding C compilation
and the Rust x86_64-unknown-uefi check pass. The prior unscaled test's active
rejection list now excludes pre/post forms intentionally supported here;
historical published receipts remain unchanged.

Separate actual OVMF mapped consumption passes all ten integration checks,
including guest X/SP pre/post assertions. Cached, uncached and unobserved EFI
variants retire 65,536 instructions and complete 26,193 data operations with
provider status zero. Reported execution states match; cached/uncached request
windows match; disabling observations preserves execution and protection counts.
Writable/executable transitions are 85/84 cached and 65,537/65,536 uncached,
including the final writable restore, with no protection failures. Authored
inputs, source and binaries remain unchanged. This is emulated execution, not
physical hardware or operating-system boot evidence.

## Target State

Validate both modes and all thirteen forms against independent Arm execution,
native execution and the Rust reference. Cover displacement endpoints, SP/ZR,
overlap rejection, flags, load extension, address wrapping, data/SP alignment,
cross-page transactions, failed translation/permission/reply validation and
no writeback on failure across canonical cache modes. Preserve this coverage
through integration. Unchanged-original replay remains a separate gate; these
tests do not establish operating-system or physical desktop boot.
