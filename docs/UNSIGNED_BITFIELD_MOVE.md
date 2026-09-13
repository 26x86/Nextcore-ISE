# BP36 — complete unsigned bitfield move

Before BP36, the native and reference translators did not implement UBFM. This change adds the standard 32-bit and 64-bit instruction, including all valid immediate combinations. It does not add BFM, SBFM, a new execution ABI, new memory/control modes or an original-image fixture.

## Frozen semantic contract

The primary instruction source is Arm A-profile A64 ISA, UBFM (page1052 onward in [Arm publication](https://documentation-service.arm.com/static/67e40f3398aa3c3b6eea6a85)); its public encoding and operation define this work. The [Arm Instruction Set Reference Guide, D2.180](https://documentation-service.arm.com/static/6245c734b059dc5ff9a8bdab) independently describes the unsigned move and aliases. No original operating-system instruction, operand or implementation supplies test values.

Recognize fixed instruction bits with `(word & 0x7f800000) == 0x53000000`. `sf` selects width32/64; `N` must equal `sf`. For width32 both immediate high bits must be zero. A mismatch is Undefined before register/PC/retirement effects. BFM is implemented separately with destination merging; SBFM and reserved opcode variants remain unsupported.

Let width be W, rotation be R and endpoint be S. All R,S in0..W-1 are accepted, including a full-width field. If S>=R, copy source bits R..S to the low destination bits. Otherwise insert source bits0..S at destination bit W-R. All destination bits outside that field are zero. This is equivalent to Arm's ROR plus write/top mask operation and covers LSL/LSR immediate, UBFX, UBFIZ, UXTB and UXTH aliases without separate alias decoders.

Both source and destination register31 mean ZR, never SP. Read the source before writing an overlapping destination. W writes zero-extend the complete X register. Preserve guest NZCV and all other PSTATE bits, SP and unrelated registers. A valid instruction advances PC four bytes and retires once. The existing exception path handles invalid encodings without adding unrelated ESR ISS bits.

## Implementation boundaries

One new native decode branch emits ordinary x86 register shifts/AND into the existing generator; it does not call a semantic helper. It is shared by direct, memory-v1, immutable memory-v2 and dynamic memory entry points. The Rust reference branch uses the equivalent rotate-and-two-masks computation. Existing memory, MMU, pending-control and privilege gates remain intact; the dynamic pending-enable window must still reject anything except the specified ISB.

The existing vf_cpu and exported memory/platform layouts remain byte-identical. The new worktree is based on immutable ISE0d722886. Only runtime instruction code, authored tests/tools and this module contract are owned here; parent metadata, pins, indices, CI and publication remain root-owned.

## Validation acceptance

Exhaust all1024 width32 and4096 width64 immediate pairs with authored mixed, zero, all-one and edge source patterns; include nonzero destination sentinels, overlapping registers, both ZR forms, all16 NZCV values and invalid sf/N/high-immediate encodings. A bit-by-bit independent expected-value function avoids repeating either production implementation. Assert exact register/PSTATE/SP/PC/retirement/fault effects from actual generated x86 and the Rust reference.

Execute authored UBFM through real C-to-canonical-Rust memory callbacks in v1, v2 and dynamic modes, with nonidentity mapping where supported and no data operations caused by UBFM. Preserve gates and existing ABI/layout regressions. Independently compare with actual Arm execution using a separately owned oracle and a real wrong-shift or wrong-zero-extension instruction mutation. Invalid BFM/SBFM must not be described as architecturally invalid: they are valid instructions outside this supported subset.

No original rerun is part of this contract. That requires completed authored native/EFI gates and a new root authorization.

## Completed host validation

The generated x86 proof passes 36,352 authored cases with 436,261 assertions, including every valid immediate pair. The complete canonical reference suite passes 100 tests. Real C-to-Rust callback suites pass in memory-v1 (13 tests), immutable stage-1 (11 tests) and dynamic control (12 tests). Separate compiled wrong-wrap-shift and wrong-W-zero-extension variants fail the arithmetic assertions as intended. Existing instruction, PAC, exception, pair/scalar memory and ABI regressions pass. Freestanding Win64 compilation and the reference UEFI check pass.

An independent Arm EL1 oracle supplies 10,706 observations: 10,240 exhaustive immediate/operand cases, 450 register and zero-register edges, and 16 reserved encodings. Both generated x86 and the canonical reference match every case, including the corrected exception syndrome described in [UNDEFINED_A64_SYNDROME.md](UNDEFINED_A64_SYNDROME.md). The separate portable comparison reproduces these results and detects an intentionally corrupted expected syndrome. This latter negative tests the comparator; it is distinct from the actual executable arithmetic mutations.

The reference comparison retains its loader-relative PC and ELR, then explicitly applies the captured origin for this PC-independent instruction. Reserved observations retain their complete saved PSTATE, including BTYPE; successful observations provide NZCV only, with the remaining initial bits explicitly chosen as diagnostic EL1h/DAIF. These tests do not claim a full captured platform, MMU or BTI regime. Actual x86 EFI acceptance is recorded separately after rebuilding the frozen runtime; host results alone do not establish that gate or an original OS boot.
