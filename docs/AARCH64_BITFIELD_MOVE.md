# A64 bitfield move with destination merge

Current Status: Native, reference, provider, mutation and UEFI compile validation passed.
Target State: BFM and its BFI/BFXIL/BFC aliases execute in generated x86 and the
Rust reference with exact old-destination preservation and width semantics.

## Architectural contract

Primary source: Arm DDI0602 June 2025, BFM printed pages 73-74, BFC page 69,
BFI page 71 and BFXIL page 75. Inspected the official instruction-set PDF:
https://documentation-service.arm.com/static/685eb35c3cb5b729562ba389.

Recognize `(word & 0x7f800000) == 0x33000000`. Bit 31 selects W/X; bit 22
(N) must equal it. For W forms, immr and imms (bits 21:16 and 15:10) must each
be below 32. All remaining immediate combinations are legal, including full
width. Invalid N or high W immediates cause Undefined before state changes.
Rn and Rd use ZR for register 31; neither refers to SP. W destination writes
zero the upper 32 bits. Guest PSTATE is preserved.

When imms >= immr, copy source bits immr through imms into destination bit 0
upward. Otherwise copy source bits 0 through imms into destination bit
width-immr upward. Preserve all other destination bits within the selected
width. Snapshot both operands before writes so Rn=Rd is well-defined. Rn=ZR
clears the selected field (including the BFC alias); Rd=ZR discards the result.

The exact operation can be expressed using Arm's rotated write mask and top
mask, merged with old Rd. Native code uses the equivalent contiguous field
extraction and insertion followed by an explicit old-destination merge. Rust
uses rotate/write-mask/top-mask semantics. Independently authored tests place
each destination bit from its source or old-destination origin without these
mask formulas. SBFM remains outside this change because its sign-fill behavior
requires separate semantic coverage. Existing UBFM behavior remains intact.

## Verification contract

Exercise all legal immr/imms pairs in both widths, different source and old
value patterns, every ZR role, overlap, all NZCV values, full-width copies and
edge positions. Check registers, SP, PSTATE, PC and exact retirement. Invalid N,
high W immediates and unallocated fixed-bit neighbors must preserve state.
Run generated-native and Rust reference oracles, actual v1/v2/dynamic C/Rust
provider traces, compiled semantic mutants and actual UEFI Rust/C COFF builds.
Replace earlier UBFM tests that classified now-supported BFM as unsupported,
while preserving their invalid and SBFM gates. No original Apple input is used.
Root owns independent EFI consumption and original/physical boot acceptance.

## Validation receipt

`tools/probe_bfm_native.py` passed with canonical LF runtime/tool sources
unchanged throughout validation. Generated native execution passed 615,040 legal
cases, 11,270 invalid/unsupported cases and 7,414,292 assertions. The independent
Rust bit-origin oracle covered the same legal and invalid cases within 115
passing pre-OS tests. Existing UBFM native regression passed 36,352 cases and
436,261 assertions.

Actual C/Rust provider suites passed 20 v1, 18 v2 and 19 dynamic tests. New BFM
coverage executes every immediate pair with five register roles: 179,200 legal
provider executions plus 56 invalid cases across the three paths, both stage-1
granules and pre/post-enable dynamic states. Traces, retirement, zero data
requests, RAM and preserved architectural/control state were checked.

Five compiled mutants failed the native oracle: missing old-destination merge,
wrong insertion position, PSTATE corruption, missing W zero-extension and
omitted encoding validation. Actual Rust UEFI release and native C COFF builds
passed. No original image was used. These results do not establish physical
macOS boot or display output; root-owned EFI consumption and replay are separate.
