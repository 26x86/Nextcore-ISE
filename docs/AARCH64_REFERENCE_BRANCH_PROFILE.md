# Reference compare and register branch corrections

## Contract

W CBZ/CBNZ must test only the low 32 bits; X forms test all 64 bits. Preserve
the tested register and all flags, and apply the signed imm19 displacement to
the current PC. BR/BLR/RET must validate their complete fixed encoding fields.
Capture the register target before BLR writes X30, including BLR X30 itself.
Retire the branch before any subsequent target-fetch exception.

The reference follows the existing native profile's rejection of Rn31. This
is a project restriction, not an architectural reserved encoding: architectural
BR/BLR/RET read X[n,64], so Rn31 supplies XZR, never SP. No native widening,
authenticated-branch change, guarded control stack or provider ABI change is
part of this correction.

Primary source: Arm DDI0602 June 2025, printed pages 82/85/738 for BLR/BR/RET
and 137/138 for CBNZ/CBZ, independently inspected in the official PDF:
https://documentation-service.arm.com/static/685eb35c3cb5b729562ba389

## Evidence

An isolated archive of a8a06dad15449b15ffb2383934f6a4239a54327f reproduced four
new test failures: W-width comparison, compare-branch retirement/control,
acceptance of malformed fixed bits, and Rn31 incorrectly selecting SP. Five
new regression tests then passed with the complete 111-test archived suite.
Unchanged native C independently passed 23 authored cases and 287 assertions.

Integration atop ordinary multiply commit 0409a3d626a8b33d0248bd567a5698ccb7c597f7
passed all 113 preOS tests and a release x86_64-unknown-uefi build. The correction
changes only the reference execution code and tests; native C bytes remain
unchanged. The parent integration retains the isolated patch and comparison
receipt separately from later EFI and original-input observations.

These are instruction and supported-profile checks, not normal macOS startup,
physical boot, or display evidence. Other native/reference coverage gaps are
outside this patch.
