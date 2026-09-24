# A64 test-bit branches

Current Status: Generated-native, reference, provider and UEFI compile validation passed.
Physical EFI consumption and original-input replay remain integration-owned.
Target State: Execute TBZ/TBNZ in generated x86 and Rust reference with precise
bit selection, target computation, state preservation and fetch accounting.

## Architectural contract

Arm DDI0602 June 2025, printed pages1015-1016 (TBNZ/TBZ), inspected in the
official instruction-set PDF:
https://documentation-service.arm.com/static/685eb35c3cb5b729562ba389

The exact family mask is `(word & 0x7e000000) == 0x36000000`. Bit24 selects
TBNZ versus TBZ. Bit31 concatenated with bits23:19 selects bit0..63; bit31 also
selects the W/X source width. The low register field is Rt, with31 reading ZR.
Bits18:5 encode signed imm14 scaled by4, relative to the address of the branch
itself: displacement -32768..32764. Every combination inside the family is
allocated. No operand is SP, and no guest flags or registers are written.

TBZ takes the branch when the selected bit is zero; TBNZ takes it when one.
Taken targets and fallthrough PC+4 wrap modulo64 bits. Only the committed next
PC is fetched. A branch retires exactly once even if the subsequent fetch fails;
its target is not preflighted and an untaken target never causes a fetch fault.
Keep existing fetch fault classifications and provider/interrupt/budget gates.

## Implementation contract

Native code uses x86 BT on the selected W/X source and existing branch-block
completion, preserving guest PSTATE and all registers. Rust uses safe bounded
bit selection and wrapping target arithmetic. No new ABI or callback is added.
The direct translator's last-address check becomes inclusive: the aligned word
at UINT64_MAX-3 contains four valid bytes and must execute, including PC wrap.
Source-buffer length checks remain unchanged; a three-byte input still fails
without retirement. This corrects an off-by-one, not a mapped-memory expansion.

## Verification contract

Exercise every bit number and both operations, all16 NZCV values, zero/all-one
and high-half data patterns, ZR, all16384 signed immediate encodings, and
forward/backward/self/wrapping branches. Independently compare generated x86 and
Rust results with arithmetic oracles. Assert complete state preservation, exact
PC and retirement, budgeted self-loops, skipped invalid code, and fetch failure
only after a retired branch. Validate actual v1/v2/dynamic fetch traces and
mapped target faults. Compiled wrong polarity/bit number/displacement/flag-write
mutants must fail assertions. Compile actual Rust UEFI and C COFF targets.
Root owns EFI consumption, original-input replay and physical/macOS acceptance;
these public tests contain no original inputs.

## Validation receipt

`tools/probe_test_bit_branch_native.py` passed with 441,344 generated-native
cases and 5,296,146 assertions. The independent Rust reference exercised the
same 441,344 architectural cases within the 106-test pre-OS suite. Native and
reference coverage includes all 64 bit positions, all 16 NZCV states and every
signed imm14 encoding, together with ZR and wrapping PCs.

The actual C/Rust v1, v2 and dynamic provider suites passed 17, 15 and 16 tests
respectively. Their new branch coverage includes 3,584 basic trace cases,
backward execution and precise next-target faults. Five separately compiled
mutants (reversed polarity, wrong bit, unsigned displacement, guest flag write,
and rejection of the last complete address word) failed the native oracle.
The actual Rust UEFI release target and native C COFF target compiled. The
receipt checks that runtime and proof-tool source bytes remain unchanged during
validation. These results establish instruction and provider behavior; they do
not establish macOS boot or physical display output.
