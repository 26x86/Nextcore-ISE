# Variable-register shifts

## Current Status

LSLV, LSRV, ASRV and RORV are implemented in the native translator and Rust
reference at W and X widths. This is a distinct family from immediate shifts
and shifted logical operands. Independent native, emulated Arm, reference and
provider validation passed on 2026-09-13.

## Contract

Only `(word & 0x7fe0f000) == 0x1ac02000` is admitted. Bits 11:10 select LSLV,
LSRV, ASRV or RORV; all other fixed bits retain their architectural values.
W counts use the low five bits and X counts the low six bits of Rm. Larger
counts are reduced modulo the operand width, including multiples that produce
a zero shift. ASR uses the selected width's sign; ROR rotates within that width.
W results clear the upper 32 bits. Register 31 is ZR for all three operands,
never SP. Both sources are read before destination writeback. Guest PSTATE,
SP and all other registers remain unchanged. Success advances PC by four modulo
2^64 and retires once without a data-memory request.

Native code loads both operands while RCX still holds the CPU pointer, saves
that pointer in volatile R10, places the count in ECX, executes the x86 D3
shift/rotate, and restores RCX before any CPU access or block exit. No callback,
memory access through RCX, or branch leaves this short interval. RDX/R8 and the
native stack are unchanged. x86 operand width supplies the low-five/six count
mask. No ABI, cache key or memory policy changes are involved.

The independently authored implementation follows the pinned public QEMU
[variable shift and width semantics](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/tcg/translate-a64.c)
in `handle_shift_reg`, `shift_reg` and their data-processing decoder. No QEMU
code or original guest image inputs are incorporated.

## Validation

The independent proof passes 16,464 direct native cases and 53,246 assertions.
Its 1,920 independently assembled vectors execute on QEMU's Arm CPU and match
native and Rust reference results, including every count residue, high count
bits, overlapping registers and ZR. The reference harness passes 34 tests.
The complete canonical provider harness passes 31 tests in each of cached,
cache-disabled and 64-byte-slot modes. Exported result, ordered request and RAM
snapshots match across modes; they do not expose every private CPU field or
callback reply. All proof source bytes are preserved.

Reproduce the independent proof from the module root:

```sh
python3 runtime/tests/verify_variable_shift_native.py --output /tmp/nextcore-variable-shift-proof
```

The existing preos suite passes 115 tests, strict freestanding native C
compilation passes, and the Rust x86_64-unknown-uefi check passes. Independent
source review confirms RCX restoration before destination or block-state stores.

Actual OVMF mapped consumption passes ten integration checks. Cached, uncached
and unobserved variants each retire 65,536 instructions and complete 26,181 data
operations with provider status zero. Reported execution states and observed
request windows match; disabling observation preserves execution and protection
counts. Writable/executable transitions are 113/112 cached and 65,537/65,536
uncached, including the final writable restore, with no protection failures.
Authored inputs, binaries and source hashes remain unchanged after the three
builds and execution. Emulated EFI evidence is not a physical or macOS boot claim.

## Target State

Verify every count residue, high count bits, zero/sign-bit/all-one inputs,
W upper-bit clearing, all ZR positions and register overlap. Compare actual
emulated Arm results against native and Rust reference results; test adjacent
and reserved rejection with unchanged PC/retirement. Compare canonical cache
modes and authored EFI consumption without broadening normal readiness gates.
These checks do not establish physical hardware or operating-system boot.
