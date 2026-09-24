# A64 conditional selection

Current Status: Native/reference execution and actual UEFI compilation verified; physical boot remains unverified.
Target State: Native x86 and Rust reference execute CSEL, CSINC, CSINV and
CSNEG at both widths, preserving guest flags and unsupported boundaries.

## Architectural contract

The normative source is Arm DDI0602, A64 Instruction Set, June 2025:
https://documentation-service.arm.com/static/685eb35c3cb5b729562ba389
Printed pages 373-374 (CSEL), 379-380 (CSINC), 381-382 (CSINV), 383-384
(CSNEG), and 5947 (ConditionHolds) were inspected from the official PDF.

The exact family mask is `(word & 0x3fe00800) == 0x1a800000`: sf selects
32/64-bit width, bit30 selects inversion, bit10 selects increment, bits15:12
select the condition, and Rn/Rm/Rd retain their standard fields. S(bit29) and
bit11 are fixed zero. Invalid encodings stay outside this decoder and retain
the existing unsupported-instruction result without partial architectural writes.

Evaluate incoming NZCV using ConditionHolds. Conditions EQ/NE, CS/CC, MI/PL,
VS/VC, HI/LS, GE/LT and GT/LE use their standard predicates; AL and NV both
hold unconditionally. On true, copy width-limited Rn. On false, take Rm and
apply identity (CSEL), +1 (CSINC), bitwise inversion (CSINV), or two's-complement
negation (CSNEG), with width-limited wrapping. Register31 reads zero and discards
writes; SP is never a source or destination. Every W write clears the upper
32 bits. Aliases CINC/CINV/CNEG/CSET/CSETM follow only their underlying encoding;
no alias-specific runtime decoding or condition inversion is added.

Successful execution changes only the destination, advances PC by4 and retires
once. Preserve all NZCV, other PSTATE bits, SP and other general registers,
including when source and destination overlap. No guest-memory access or new
FFI record exists. Reuse the existing JIT condition evaluator and Microsoft-x64
volatile-register ABI; native arithmetic remains inside generated code without
interpreter/helper calls. Preserve provider and dynamic transition gates.

## Verification contract

Independently authored cases cover every width, operation, condition and incoming
NZCV combination, width boundary/overflow data, ZR in all operand roles, shared
sources, aliased destination, and invalid S/bit11 fields. Expected conditions
use an explicit truth table independent of the production grouped predicate.
Run actual generated x86 and separate Rust reference comparisons with complete
state preservation assertions. Validate family execution through actual C/Rust
v1/v2/dynamic providers, including both page granules and pre/post-enable paths.

Compiled mutations must fail for inverted condition selection, missing false
branch increment, and accidental guest-NZCV clobber. Verify actual UEFI target
Rust and freestanding COFF C builds. These are ISA/build proofs only; root owns
EFI consumption and original boot observations, and physical macOS acceptance
remains separate.


## Validation receipt (2026-09-12)

`tools/probe_conditional_select_native.py` passed 131,072 native execution cases
with 1,728,514 assertions, plus 131,072 independent Rust reference cases within
102 preOS tests. All 15 v1, 13 v2 and 14 dynamic provider tests passed. The
family-specific provider sequence creates Z/C flags with CMP and executes all
four selection operations, including a W destination that must clear its upper
half. Native fetch/retirement counts, final registers, preserved PSTATE and
untouched guest RAM are asserted for both page granules and dynamic pre/post
MMU-enable paths.

Three compiled mutations (inverted condition, wrong false-branch increment,
and guest-PSTATE clobber) failed execution assertions. Rust release UEFI and
freestanding C COFF compilation passed. The runner preserves source hashes,
exact commands, stdout/stderr, and negative-control results. No original image
is used in these public tests; these results make no macOS boot claim.
