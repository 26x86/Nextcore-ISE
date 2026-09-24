# A64 extended-register arithmetic

Current Status: Native and reference implementation verified on Linux x86_64; actual UEFI compilation verified. Physical boot remains unverified.
Target State: Native x86 and Rust reference execute ADD, ADDS, SUB, SUBS
extended-register forms, including CMN/CMP aliases, without interpreter fallback.

## Architectural contract

The normative source is Arm DDI0602, A64 Instruction Set, June 2025,
ADD/ADDS/SUB/SUBS (extended register), DecodeRegExtend, ExtendReg and AddWithCarry:
https://documentation-service.arm.com/static/685eb35c3cb5b729562ba389
https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions/ADD--extended-register---Add-extended-and-scaled-register-

Decode only fixed family bits `(word & 0x1fe00000) == 0x0b200000`.
Width is 32 or 64 from sf. imm3 values 0..4 are legal; 5..7 are undefined.
All eight extension options are defined at both widths: zero/sign extension of
8, 16, 32, or 64 source bits, limited to the operation width, followed by the
specified left shift and truncation to the operation width. Rm=31 reads zero.
Rn=31 reads SP; Rd=31 writes SP only for non-flag forms. Flag forms discard
Rd=31 (CMN/CMP), and otherwise write a general register. Every 32-bit write,
including WSP, zero-extends into the 64-bit storage slot.

Addition/subtraction wraps at operand width. Flag forms replace only NZCV:
N is the result sign, Z is zero, C is addition carry or subtraction no-borrow,
and V is signed overflow. Non-flag forms preserve all guest PSTATE bits.
Undefined encodings preserve registers/SP/PSTATE, do not retire, and report the
faulting word and PC through the existing undefined-instruction boundary.

The native implementation uses the existing Microsoft-x64 JIT ABI and volatile
RAX/R9/R10/R11 only; no helper calls, stack changes, guest memory accesses, new
FFI shapes, or changed provider gates. Source/destination overlap must work.
The reference uses safe integer operations with bounded shifts.

## Verification contract

Public fixtures are authored solely from architectural encodings and integer
arithmetic. Cover all widths/operations/options/legal shifts, source sign and
carry/overflow edges, SP/ZR and overlapping registers, reserved bits and invalid
shifts. Execute generated native x86 and compare independent expected values;
validate the Rust reference separately. Execute a family-specific sequence with
actual C dispatcher/Rust provider callbacks and preserve unsupported gates.
Compiled wrong-extension and wrong-flags mutants must fail assertions. Compile
actual x86_64-unknown-uefi Rust and freestanding COFF C. These establish instruction
and build evidence only, not macOS or physical machine boot.

## Validation receipt (2026-09-12)

`tools/probe_extended_arithmetic_native.py` passed: 96,000 generated-x86 cases
and 1,249,154 assertions; 432,000 reference family cases within 101 preOS tests;
14 v1, 12 v2 and 13 dynamic provider tests. The provider family sequence executes
signed byte addition, scaled unsigned subtraction, and signed word CMP with
observed N/C flags, untouched input RAM, and native fetch counts. Both 4 KiB and
16 KiB mapped profiles and dynamic pre/post-enable paths are exercised.
Wrong signed-extension and subtraction-carry mutations compiled and failed
execution assertions. Rust release x86_64-unknown-uefi and freestanding C COFF
compilation passed. The command emits source hashes, exact commands and outputs.

The broader preOS `cargo fmt --check` reports pre-existing formatting differences;
no unrelated reformat is included. Repository whitespace error check passed.
