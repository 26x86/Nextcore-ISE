# Baseline AArch64 software thread registers

The x86 EFI JIT directly emits reads and writes of `TPIDR_EL0`,
`TPIDRRO_EL0`, and `TPIDR_EL1`. The C architectural bank and Rust reference
decoder implement the same access rules. The values belong to each guest CPU;
they never read or change the host's native TLS registers.

| Register | AArch64 encoding (op0, op1, CRn, CRm, op2) | EL0 | EL1–EL3 |
| --- | --- | --- | --- |
| TPIDR_EL0 | 3, 3, 13, 0, 2 | Read/write | Read/write |
| TPIDRRO_EL0 | 3, 3, 13, 0, 3 | Read; write is UNDEFINED | Read/write |
| TPIDR_EL1 | 3, 0, 13, 0, 4 | UNDEFINED | Read/write |

Every value is 64 bits. Software interprets these values; the PE does not
dereference them, select a page table, or invalidate translations when they
change. `Rt=31` reads XZR on MSR and discards the result on MRS, without
accessing SP. Access failure preserves the faulting PC and does not retire the
instruction or change the target register. Changing exception level does not
select a different copy of these three registers.

Arm specifies an architecturally UNKNOWN reset value. This implementation
chooses deterministic zero. This is an implementation choice, not a guarantee
that physical ARM hardware resets the registers to zero.

This is the baseline AArch64 profile without FEAT_FGT or AArch32 aliases.
FGT control-register accesses remain unsupported rather than silently enabling
unimplemented trap behavior. `TPIDR2_EL0`, `CONTEXTIDR_EL1`, and unknown system
registers also remain explicit boundaries. Context ID, VHE, security-state
banking, and implementation-defined platform control are separate contracts;
these three software stores do not provide them.

The private `vf_cpu` structure was 864 bytes on x86_64 at BP26 and is 888
bytes after BP27's per-CPU platform state. It never crosses the C/Rust
boundary. The stable boot result remains 64 bytes and PAC context 368 bytes;
the separately versioned platform ABI has its own checked options/result
layouts. The reproducible native probe compares the exported preOS C and
Rust layouts and runs both reference and generated-code tests:

```sh
RUSTUP_TOOLCHAIN=stable python3 tools/probe_efi_native_pauth.py \
  --rustc "$HOME/.cargo/bin/rustc" --output /tmp/efi-thread-proof.json
```

The authored native thread test covers all four exception levels, full-width
values, read-only EL0 visibility after an EL change, zero/discard operands,
unchanged SP/NZCV/TLB state, independent CPUs, and unsupported-register
boundaries. It executes generated x86 code with W^X protection. This is an
instruction implementation proof and does not assert macOS boot completion.

Primary architecture references:

- [Arm AArch64 register reference: TPIDR_EL0](https://developer.arm.com/documentation/ddi0601/2025-03/AArch64-Registers/TPIDR-EL0--EL0-Read-Write-Software-Thread-ID-Register?lang=en).
- [Arm Architecture Reference Manual, ARMv8-A, DDI 0487B.a](https://cs140e.sergio.bz/docs/ARMv8-Reference-Manual.pdf), the TPIDR_EL0, TPIDR_EL1, and TPIDRRO_EL0 register definitions (Arm document, university mirror).
- [Arm Morello Architecture Reference Supplement, DDI 0606A.j](https://kib.kiev.ua/x86docs/ARM/Morello/DDI0606_A.j_morello_architecture_external.pdf), TPIDRRO_EL0 AArch64 access table (Arm document, mirror; capability extensions are outside this implementation).
- [Arm SME architecture supplement](https://documentation-service.arm.com/static/6526e1bd9e189a266cef8412), fine-grained read/write trap controls. FGT is outside this baseline profile.
