# Explicit platform and interrupt boundary, ABI v2

BP26 has software thread registers but no platform override provider, and
native execution does not deliver asynchronous inputs. BP27 adds a named
software profile with real pending-level and exception semantics. It does not
claim that its initial state was measured on Apple hardware.

`nextcore-irq-compat-v1` (id 1) defines a complete software register state:
IRQ override [23:22] and FIQ override [21:20] each accept 0 (no override) or
2 (disabled); all other fields are fixed zero. The reset/initial value is
explicitly supplied by the caller within that mask. Changes to any other
field or unsupported field value fail before committing. Profile 0 has no
platform provider and preserves the earlier system-register boundary.
The profile is limited to EL1 AArch64, no hypervisor/security routing,
no power transition or WFI-mode control.

IRQ and FIQ inputs are levels. Masking and delivery preserve the input until
its source deasserts it. PSTATE.I and PSTATE.F act independently; eligible
FIQ is selected before IRQ by this profile's deterministic arbitration.
EL0 targets EL1. Same-level entry selects the saved SP0/SPx group, then
adds 0x80 for IRQ or 0x100 for FIQ; lower AArch64 entry starts at 0x400.
The runner saves PC/PSTATE into ELR/SPSR, switches to EL1h/SP_EL1, masks
DAIF, and returns a diagnostic interrupt result at the vector entry.
It does not silently execute a handler or clear a level source.

Native MMU execution remains explicitly gated: if SCTLR.M is set, the
native runner returns status 13 before polling inputs or compiling/executing
instructions. PC and retired count remain unchanged and the decoded fault
instruction is zero. This is an unsupported execution-regime result, not a
fabricated guest instruction fault. The Rust reference has its separate
bounded page walker. IRQ/FIQ routing through EL2/EL3 or enabled HCR/SCR
routing controls is rejected, preserving pending inputs.

Asynchronous IRQ/FIQ do not write ESR/FAR. Native execution polls at an
instruction boundary while an input or enabled timer can affect the run.
Existing timer events retain the reference profile's IRQ routing; no claim
is made that this routing represents an Apple platform timer topology.
The diagnostic counter advances by one tick for each retired instruction;
it is not a host wall-clock source or a physical CPU timing claim.

Native `MSR DAIFSet/DAIFClr,#imm4` changes only the selected DAIF bits and
returns to the dispatcher before the next instruction; EL0 access remains
outside the supported UMA=0 profile. `MSR SPSel,#0/#1` saves the currently
selected SP bank and selects SP_EL0/SP_ELx. These updates preserve NZCV.
SPSel's architectural immediate is one bit; other immediate encodings are
not accepted. See [Arm stack-pointer selection](https://documentation-service.arm.com/static/5e906b9fc8052b1608760c7b)
and the MSR immediate definitions in the
[Arm architecture manual](https://cs140e.sergio.bz/docs/ARMv8-Reference-Manual.pdf).

The new `vf_boot_run_v2` takes the old explicit-register arguments followed
by `const vf_boot_options_v2 *` and `vf_boot_result_v2 *`. Older APIs remain
unchanged. Options are 64 bytes: four u32 values (version=2, size=64,
profile, flags=0), then six u64 values (initial override, initial PSTATE,
VBAR, IRQ level, FIQ level, reserved=0). PSTATE selects EL1t/EL1h with only
NZCV/DAIF flags. VBAR is 2 KiB aligned in guest RAM when nonzero. Levels
are 0/1. Invalid input returns a data fault before execution.

The 128-byte result begins with the unchanged 64-byte boot result. It adds
u64 platform override; u32 pending-lines/profile; then u64 ELR, SPSR,
exception vector, ESR, PSTATE, SP. IRQ returns existing status 15; FIQ
adds status 18 without renumbering old statuses. Only the known version
is accepted. Rust uses the same repr(C) definitions and layout receipts.

Validation covers full mask/level/PSTATE combinations, EL0/EL1t/EL1h
vectors, repeated level delivery, no ESR/FAR corruption, nonretirement of
rejected accesses, provider absence, C/Rust ABI equality and actual native
code execution. Authored EFI probes supply explicit options; original
traces record the named software profile and retain missing-SPTM fields.

Public interface sources:

- [Arm AArch64 exception model](https://documentation-service.arm.com/static/67ac57fb091bfc3e0a9479cc).
- [Apple public interrupt-override field definitions](https://github.com/apple-oss-distributions/xnu/blob/main/pexpert/pexpert/arm64/apple_arm64_regs.h).
- [Apple public startup use](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/arm64/sptm/start_sptm.s).
- [Asahi public register encodings](https://github.com/AsahiLinux/m1n1/blob/main/src/cpu_regs.h).
