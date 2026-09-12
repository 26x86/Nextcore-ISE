# Logical shifted-register execution

## Current Status

The native translator and architectural reference implement the complete base
integer logical shifted-register family. Independent native, reference and
canonical provider checks pass, including 768 vectors executed by QEMU's Arm CPU
emulation. This instruction contract does not establish XNU, userspace, physical
desktop or graphics readiness.

## Contract

The family is selected by `(word & 0x1f000000) == 0x0a000000`. It contains AND,
BIC, ORR, ORN, EOR, EON, ANDS and BICS at 32-bit and 64-bit widths. The second
operand is shifted using LSL, LSR, ASR or ROR before optional inversion. W forms
accept amounts 0 through 31; X forms accept 0 through 63. A W encoding with
imm6 bit 5 set is undefined and must not modify architectural operands, flags,
PC or retirement before recording the existing exception result.

All register-31 operands mean ZR, never SP. Sources are read before destination
writeback; W results zero-extend into X. ANDS/BICS set N/Z from the result at
the selected width and clear C/V while preserving the remaining PSTATE bits.
Other operations preserve PSTATE. MOV, MVN and TST aliases use their underlying
logical encodings. Successful execution advances PC by four modulo 2^64 and
retires once; it does not issue a data-memory request.

Native emission uses only the existing volatile-register contract. It reads
live guest registers and stores flags only after the final logical operation.
No CPU layout, provider API, cache key, memory regime or readiness gate changes.

Primary specification reference: [QEMU's pinned logical shifted-register
decoder](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/tcg/translate-a64.c#L7011-L7095).
The implementation is independently authored; QEMU code is not incorporated.
The [Arm official ORR reference](https://developer.arm.com/documentation/100076/0100/A64-Instruction-Set-Reference/A64-General-Instructions/ORR--shifted-register-?lang=en)
identifies the instruction; the pinned decoder provides the inspected encoding,
width rejection and operation contract.

## Validation

Independent authored tests cover all eight operations, both widths, all shifts,
zero and maximum amounts, sign bits, inverted operands, source/destination
overlap, ZR, aliases and preserved non-NZCV state. Reserved W amounts must fail
without retirement. Actual Arm results are compared with native and reference
execution. Canonical provider tests compare cached, disabled and small-slot
modes, including exact fetch accounting and absence of data requests. Existing
logical-immediate and arithmetic regressions remain required.

The 2026-09-12 independent run passed 64,512 native cases and 197,122 assertions.
Its 768 independently assembled QEMU Arm vectors match both generated x86
execution and the Rust architectural reference. Canonical provider loops pass
in cached, cache-disabled and 64-byte-slot modes with matching exported result
and ordered request snapshots, unchanged RAM, exact fetch counts and no data
requests. These snapshots do not expose every private CPU field or callback
reply. The runner does not claim a full private-CPU differential comparison.

Reproduce the dedicated proof from the module root:

```sh
python3 runtime/tests/verify_logical_shifted_native.py --output /tmp/nextcore-logical-shifted-proof
```

The full canonical harness was additionally run without the dedicated test
filter: 31 tests passed in each of the three cache modes. Existing preos tests
passed 115 cases, and the x86_64-unknown-uefi Rust check and strict freestanding
native compilation passed. QEMU evidence is emulated Arm execution; it is not
physical Arm hardware or an operating-system boot result.

## Target State

Integrate the complete independently verified family into the existing EFI
execution path without broadening unrelated instruction or platform gates.
Actual operating-system startup remains a separate acceptance boundary.
