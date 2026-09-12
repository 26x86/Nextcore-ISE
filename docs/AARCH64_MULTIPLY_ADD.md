# A64 integer multiply-add and multiply-subtract

Current Status: Native, reference, provider, negative-control and UEFI compile validation passed.
Target State: Ordinary MADD/MSUB execute identically in generated native x86 and
Rust reference, with independent arithmetic and provider proofs.

## Architectural contract

Primary source: Arm DDI0602 June 2025, MADD printed page 630, MSUB page 653,
and the data-processing (3 source) decode table on page 5086, inspected in the
official PDF https://documentation-service.arm.com/static/685eb35c3cb5b729562ba389.

The exact supported mask is `(word & 0x7fe00000) == 0x1b000000`.
Bit 31 selects 32/64 bits; bit 15 selects addition/subtraction. Rn is bits 9:5,
Rm bits 20:16, Ra bits 14:10 and Rd bits 4:0. MADD returns Ra + Rn * Rm;
MSUB returns Ra - Rn * Rm, reduced modulo the selected width. W destinations
zero their upper 32 bits. Every register field uses ZR for register 31; none
selects SP. Ra=31 provides MUL/MNEG aliases. Read all sources before writing
any destination, including overlapping registers. Preserve all guest PSTATE.

Widened SMADDL/UMADDL/SMSUBL/UMSUBL, high-half SMULH/UMULH and feature-specific
MADDPT/MSUBPT have different op31 fields. They remain unsupported, as do
unallocated neighboring fields. Do not broaden the family mask to admit them.
Existing undefined-instruction gates and fault accounting remain authoritative.

## Implementation and verification contract

Native code uses two-operand x86 IMUL for the low-width product and existing
volatile RAX/R9 staging for the addend minus/plus product; RCX/RDX/R8 retain
the generated block ABI. Signed and unsigned low-width products are identical.
Rust uses explicit wrapping arithmetic and existing width-aware register writes.
There are no memory operations, ABI changes or new callbacks.

Independently authored native and Rust fixtures cover both widths, both
operations, all NZCV states, zeros, sign edges, maximum values, multiplication
and addition overflow, every ZR role and overlapping source/destination roles.
Use a wider arithmetic oracle rather than mirroring the implementation. Verify
full state preservation and exact retirement/PC, reject adjacent instruction
families, execute through actual C/Rust v1/v2/dynamic providers, and compile
semantic mutants for subtraction order, product truncation, flags and decode.
Compile actual UEFI Rust and native C COFF targets. No original Apple input is
used. Root owns independent EFI consumption and physical/macOS acceptance.

## Validation receipt

`tools/probe_multiply_add_native.py` passed with source bytes preserved.
Generated native execution passed 393,216 architectural cases with 4,718,842
assertions. The reference exercised 393,216 cases with the wider arithmetic
oracle within 108 passing pre-OS tests. Both implementations rejected all 124
neighboring opcode combinations outside the supported mask.

The actual C/Rust provider suites passed 18 v1, 16 v2 and 17 dynamic tests,
including 2,240 new multiply executions with exact fetch traces, zero data
requests and RAM/state preservation. Four compiled mutants were rejected:
reversed subtraction, product truncated to 32 bits, guest PSTATE corruption,
and an overly broad decoder admitting widened operations. Actual Rust UEFI
release and native C COFF builds passed. No original image was used; physical
macOS boot and display output are not established by this instruction proof.
