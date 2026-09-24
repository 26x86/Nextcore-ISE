# AArch64 logical immediate instruction contract

The native and reference cores add the standard 32/64-bit AND, ORR, EOR,
and ANDS immediate class. The current implementation has no such decoder;
this change supplies the bitmask operation required by platform mask updates.

Decode the N:immr:imms fields into a rotated nonempty, non-all-ones run of
ones in a 2/4/8/16/32/64-bit element and replicate that element to the
operand width. Reject a 32-bit encoding with N=1, an invalid element size,
or an all-ones element before changing any state. Rotation bits outside the
element width are ignored as specified; equivalent encodings are valid.

Rn31 is XZR/WZR. For non-flag-setting operations, Rd31 is SP/WSP. For
ANDS, Rd31 discards the result (the TST alias). A 32-bit destination is
zero-extended to 64 bits. ANDS sets N/Z from the selected result width and
clears C/V; other forms preserve all guest flags. Each accepted instruction
retires once, advances PC by four, and performs no memory access.

The implementation is independently written from the published instruction
encoding and operation definitions. Tests construct masks by circular runs
of bit positions, then encode them independently, and verify actual native
results across element sizes, rotations, widths and operations. Reserved
encodings, flag behavior, SP/ZR operands and upper-half clearing are checked.

Primary sources: [Arm A64 instruction reference, 2025-09](https://documentation-service.arm.com/static/68da52dfbd7cab51328c0622),
AND/ORR/EOR/ANDS immediate definitions, and the DecodeBitMasks operation in
the [Arm ARMv8-A architecture manual](https://cs140e.sergio.bz/docs/ARMv8-Reference-Manual.pdf).

The existing ADD/SUB shifted-register decoder also accepts its flag-setting
forms and all three defined shifts (LSL, LSR, ASR). Shift counts are 0–31
for 32-bit operands and 0–63 for 64-bit operands. Reserved shift type 3 and
32-bit counts 32–63 are UNDEFINED before state changes. Both sources use
ZR for register31; destination31 discards
the result, including CMP/CMN aliases. Arithmetic NZCV follows the existing
immediate implementation (subtraction C means no borrow). The operand is
truncated to its width before shifting; ASR extends that width's sign bit.
