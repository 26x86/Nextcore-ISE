# GXF / SPRR device layer (reference)

Apple A13 Guarded Execution (GXF) and SPRR/MPRR pointer-authentication
region sysreg facts are checked against qemu-t8030 `hw/arm/apple_a13_gxf.c`
at commit `fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference,
behaviour-only). This is an **iOS T8030 / A13 CPU extension reference** —
not a macOS VMApple guest contract.

## Topology

| Field | Value |
| --- | --- |
| Integration | AArch64 system registers (no MMIO window) |
| CPU hook | `apple_a13_init_gxf()` / `apple_a13_init_gxf_override()` |
| Guarded predicate | `arm_is_guarded(env)` gates `*_GL11` bank access |
| PAuth coupling | SPRR/MPRR region tables feed PAC permission checks |

GXF registers are **not** memory-mapped. They are installed as ARM coprocessor
/sysreg overrides on the Apple A13 CPU model. macOS 27 ARM64E KC paths may
probe GXF/SPRR presence before PAC-enabled code regions are entered.

## Sysreg encoding

All registers below use `op0=3` (AA64 system register space). Encoding key
used by the v1 stub:

```text
key = (op0 << 16) | (op1 << 12) | (crn << 8) | (crm << 4) | op2
```

## GXF control registers (EL1)

| Name | op1 | CRn | CRm | op2 | Access | Reset | Notes |
| --- | ---: | ---: | ---: | ---: | --- | ---: | --- |
| `GXF_CONFIG_EL1` | 6 | 15 | 1 | 2 | PL1 RW | 0 | Bit 0 (`VF_GXF_CONFIG_EN`) enables GENTER |
| `GXF_STATUS_EL1` | 6 | 15 | 8 | 0 | PL1 R | 0 | Bit 0 (`VF_GXF_STATUS_GUARDED`) = guarded |
| `GXF_ENTER_EL1` | 6 | 15 | 8 | 1 | PL1 RW | 0 | Enter vector returned by GENTER |
| `GXF_ABORT_EL1` | 6 | 15 | 8 | 2 | PL1 RW | 0 | Abort vector (`vf_apple_gxf_abort`) |

## Guarded bank (`*_GL11`, trapped unless guarded)

Access function `access_gxf()` returns **trap** when `!arm_is_guarded(env)`.

| Name | op1 | CRn | CRm | op2 | Access | Shadow / alias |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| `ASPSR_GL11` | 6 | 15 | 8 | 3 | PL1 RW | `gxf.aspsr_gl[1]`; bit0 = `VF_GXF_ASPSR_NEST` |
| `SP_GL11` | 6 | 15 | 9 | 0 | PL2 RW | `gxf.sp_gl[1]` / `sp_el[1]` |
| `TPIDR_GL11` | 6 | 15 | 9 | 1 | PL1 RW | `gxf.tpidr_gl[1]` / `tpidr_el[1]` |
| `VBAR_GL11` | 6 | 15 | 9 | 2 | PL1 RW | `gxf.vbar_gl[1]` / `vbar_el[1]` |
| `SPSR_GL11` | 6 | 15 | 9 | 3 | PL1 RW | `gxf.spsr_gl[1]` / `banked_spsr[SVC]` |
| `ESR_GL11` | 6 | 15 | 9 | 5 | PL1 RW | `gxf.esr_gl[1]` / `esr_el[1]` |
| `ELR_GL11` | 6 | 15 | 9 | 6 | PL1 RW | `gxf.elr_gl[1]` / `elr_el[1]` |
| `FAR_GL11` | 6 | 15 | 9 | 7 | PL1 RW | `gxf.far_gl[1]` / `far_el[1]` |

## EL1 register overrides (dual normal / guarded view)

These replace the architectural EL1 accessors so reads/writes route to the
guarded bank when `arm_is_guarded(env)`:

| Name | op1 | CRn | CRm | op2 | Notes |
| --- | ---: | ---: | ---: | ---: | --- |
| `TPIDR_EL1` | 0 | 13 | 0 | 4 | AA64 override |
| `VBAR` | 0 | 12 | 0 | 0 | Both AA32/AA64; `VBAR_EL1` alias |
| `SPSR_EL1` | 0 | 4 | 0 | 0 | Alias override |
| `ELR_EL1` | 0 | 4 | 0 | 1 | Alias override |
| `ESR_EL1` | 0 | 5 | 2 | 0 | TVM/TRVM gated |
| `FAR_EL1` | 0 | 6 | 0 | 0 | TVM/TRVM gated |
| `SCTLR_EL1` | 0 | 1 | 0 | 0 | Single-bank; VMSA lock bit1 / bit63 M |
| `TTBR0_EL1` | 0 | 2 | 0 | 0 | Single-bank; VMSA lock bit3 |
| `TTBR1_EL1` | 0 | 2 | 0 | 1 | Single-bank; VMSA lock bit4 |
| `TCR_EL1` | 0 | 2 | 0 | 2 | Single-bank; VMSA lock bit2 |

Clean-room v1 dual-view (`vf_apple_gxf_read` / `write_el`):

| Mode | Storage |
| --- | --- |
| Unguarded | normal bank `tpidr_el1` / `vbar_el1` / `spsr_el1` / `elr_el1` / `esr_el1` / `far_el1` |
| Guarded | shared with `*_GL11` bank (same fields) |
| `VBAR_EL1` | always masked `value & ~0x1f` (32-byte align) |
| TVM/TRVM | `hcr_el2` shadow (`VF_GXF_HCR_TVM` bit26 / `VF_GXF_HCR_TRVM` bit30); EL1 `ESR_EL1`/`FAR_EL1` read traps on TRVM, write traps on TVM (fail closed `-1`); `el>=2` bypass; direct `ESR_GL11`/`FAR_GL11` ungated |
| HCR_EL2 live | architectural `VF_GXF_REG_HCR_EL2` (`op1=4,CRn=1,CRm=1,op2=0`); `write_el(..., el>=2)` or host `vf_apple_gxf_set_hcr_el2` updates the same shadow; EL1 MSR/MRS fail closed |
| VMSA lock | `VMSA_LOCK_EL1` (`op1=4,CRn=15,CRm=1,op2=2`); guest write sticky-OR; unguarded EL1 silent no-op consumers: bit0 `VBAR_EL1`, bit1 `SCTLR_EL1`, bit2 `TCR_EL1`, bit3 `TTBR0_EL1`, bit4 `TTBR1_EL1`; bit63 `SCTLR_M` preserves SCTLR.M when full SCTLR lock clear; guarded / `el>=2` / direct `VBAR_GL11` unlocked; SCTLR/TCR/TTBR are single-bank (not dual-view) |
| ASPSR nest | Hardware auto-set on `vf_apple_gxf_abort` while already guarded (`ASPSR_NEST`); EL→GL abort clears nest |
| VBAR vector | `vf_apple_gxf_take_exception`: guarded → `VBAR_GL11` base, else `VBAR_EL1`; + table (0/`0x200`/`0x400`/`0x600`) + kind (0/`0x80`/`0x100`/`0x180`); ESR for SYNC/SERR; FAR for SYNC; does not enter/leave guarded |
| Deferred | Live QEMU translate-a64 GXF hooks (clean-room stub: `vf_apple_gxf_tcg_*`) |

## SPRR / MPRR region-permission registers (PAuth)

| Name | op1 | CRn | CRm | op2 | Access | Field | Notes |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- |
| `SPRR_CONFIG_EL1` | 6 | 15 | 1 | 0 | PL1 RW | `sprr.sprr_config_el[1]` | EL1 SPRR config |
| `SPRR_CONFIG_EL0` | 6 | 15 | 1 | 1 | PL1 RW | `sprr.sprr_config_el[0]` | EL0 SPRR config |
| `SPRR_EL0BR0_EL1` | 6 | 15 | 1 | 5 | PL0 RW | `sprr.sprr_el_br_el[0][0]` | Masked EL0 write |
| `SPRR_EL0BR1_EL1` | 6 | 15 | 1 | 6 | PL1 RW, PL0 R | `sprr.sprr_el_br_el[0][1]` | |
| `SPRR_EL1BR0_EL1` | 6 | 15 | 1 | 7 | PL1 RW | `sprr.sprr_el_br_el[1][0]` | |
| `SPRR_EL1BR1_EL1` | 6 | 15 | 3 | 0 | PL1 RW | `sprr.sprr_el_br_el[1][1]` | |
| `MPRR_EL0BR0_EL1` | 6 | 15 | 3 | 1 | PL1 RW | `sprr.mprr_el_br_el[0][0]` | Permission mask |
| `MPRR_EL0BR1_EL1` | 6 | 15 | 3 | 2 | PL1 RW | `sprr.mprr_el_br_el[0][1]` | |
| `MPRR_EL1BR0_EL1` | 6 | 15 | 3 | 3 | PL1 RW | `sprr.mprr_el_br_el[1][0]` | |
| `MPRR_EL1BR1_EL1` | 6 | 15 | 3 | 4 | PL1 RW | `sprr.mprr_el_br_el[1][1]` | |

`SPRR_EL0BR0_EL1` writes from EL0 are masked by `MPRR_EL0BR0_EL1` allow
bits. Clean-room v1 merge (`vf_apple_gxf_write_el`, `el==0`):

| Field | Layout |
| --- | --- |
| SPRR attr slots | 16 × 4-bit at bit `i*4`; EL0-writable subset is RWX (low 3 bits) |
| MPRR allow packs | 16 × 2-bit at bit `i*2` (`0b11` = W+X allow) |
| Merge rule | per slot, only `(requested ^ original) & allow` bits change |
| EL1+ write | unmasked full store via `vf_apple_gxf_write` / `write_el(..., el>=1)` |
| TLB flush hook | EL0 merge bumps `sprr_perm_generation` **and** `sprr_tlb_flush_count`, then invokes optional `sprr_tlb_flush` callback (NULL = count-only) |
| Guest JIT bind | Bridge `vf_m1_guest_gxf_bind_guest_tlb(cpu)` wires callback → `vf_cpu_invalidate_tlb`; unbound fails closed (count only); **not** a QEMU SoftMMU TLB |
| SoftMMU path (VenFire) | Research-gated QEMU writefn calls SoftMMU `tlb_flush()` on EL0 merge change — see VenFire **0012** (authored; **not** yet in `series`) |

Host helpers: `vf_apple_gxf_set_sprr_tlb_flush`, `vf_apple_gxf_sprr_perm_generation`,
`vf_apple_gxf_sprr_tlb_flush_count` (bridged as `vf_m1_guest_gxf_sprr_*`);
bind accessors `vf_m1_guest_gxf_bind_guest_tlb` / `vf_m1_guest_gxf_guest_tlb_generation`.

## Clean-room GENTER / GEXIT / GXF_ABORT (v1 stub)

Host/tests call `vf_apple_gxf_genter` / `vf_apple_gxf_abort` / `vf_apple_gxf_gexit`
(bridged as `vf_m1_guest_gxf_genter` / `abort` / `gexit`). Optional TCG opcode
stub `vf_apple_gxf_tcg_classify` / `vf_apple_gxf_tcg_exec` (bridged
`vf_m1_guest_gxf_tcg_*`) recognizes qemu-t8030 encodings and dispatches to the
same helpers. Live QEMU decode is a separate VenFire patch (below):

| Encoding | Value | Fields |
| --- | ---: | --- |
| GENTER | `0x00201420` | bit31=0, bits[28:25]=0, opcode=5, rn=1 |
| GEXIT | `0x00201400` | bit31=0, bits[28:25]=0, opcode=5, rn=0 |

| Step | Behavior |
| --- | --- |
| GENTER preconditions | `GXF_CONFIG_EL1 & CONFIG_EN`; fail if already guarded |
| GENTER side effects | `ELR_GL11=return_pc`, `SPSR_GL11=spsr`, clear `ASPSR_NEST`, set `STATUS_GUARDED`, bump `gxf_mode_generation`, `*enter_pc=GXF_ENTER_EL1` |
| GXF_ABORT preconditions | `GXF_CONFIG_EL1 & CONFIG_EN` (allowed while guarded — GL nest) |
| GXF_ABORT EL→GL | save `ELR`/`SPSR`/`ESR`/`FAR`; clear `ASPSR_NEST`; set `STATUS_GUARDED`; `*abort_pc=GXF_ABORT_EL1` |
| GXF_ABORT GL→GL nest | save `ELR`/`SPSR`/`ESR`/`FAR` (overwrite GL bank); **set** `ASPSR_NEST`; stay guarded; `*abort_pc=GXF_ABORT_EL1` (dedicated abort vector, not VBAR) |
| GEXIT preconditions | currently guarded |
| GEXIT + ASPSR nest | if `ASPSR_GL11 & ASPSR_NEST`: clear nest, **stay** guarded (`*return_pc=ELR_GL11`); else clear `STATUS_GUARDED` |
| Predicate sync | `guarded` mirrors `GXF_STATUS_EL1 & 1` (same as `arm_is_guarded` bit) |
| VBAR take_exception | host/tests call `vf_apple_gxf_take_exception` (bridged `vf_m1_guest_gxf_take_exception`): base = guarded ? `VBAR_GL11` : `VBAR_EL1`; table offsets `CURRENT_SP0=0` / `CURRENT_SPX=0x200` / `LOWER_A64=0x400` / `LOWER_A32=0x600`; kind offsets `SYNC=0` / `IRQ=0x80` / `FIQ=0x100` / `SERR=0x180`; saves ELR/SPSR; ESR on SYNC/SERR; FAR on SYNC; no guarded mode change |
| TCG opcode stub | EL0 / unrecognized → fail closed; GENTER/GEXIT → same helpers; no exception inject / MMU |

Public ASPSR nest bit semantics follow Asahi/Sven (gexit returns to GL vs EL).
`GXF_ABORT` vector address follows qemu-t8030 `EXCP_GXF_ABORT` → `gxf_abort_el[]`.
GL-nested abort auto-sets `ASPSR_NEST` so the next GEXIT resumes the interrupted GL context.
Ordinary exceptions while guarded follow qemu-t8030 `arm_cpu_do_interrupt_aarch64`
(`VBAR_GL` + AArch64 table/kind offsets); they do not use `GXF_ABORT_EL1`.

## VenFire 0010 / 0011 (in `series`)

Reversible GPL patches
`research/venfire/patches/0010-vmapple-gxf-genter-gexit-tcg.patch` and
`0011-vmapple-gxf-opc1-access-relax.patch` are listed in `series` (0001–0011).
`STOP_AFTER=check` PASS; durable rebuild via
`Tools/rebuild_patched_qemu_aarch64.sh` (SHA `473db26d…`). Guest smoke:
`Tools/smoke_vmapple_gxf_genter_gexit.py` **PASS**. Contract:
[`docs/research/VMAPPLE_TCG_GXF_OPCODES.md`](../../../../docs/research/VMAPPLE_TCG_GXF_OPCODES.md).

## VenFire 0012 SoftMMU SPRR TLB (authored; not in `series`)

Reversible GPL patch
`research/venfire/patches/0012-vmapple-gxf-sprr-softmmu-tlb.patch`:
research-gated `VMAPPLE_SPRR_EL0BR0_EL1` / `VMAPPLE_MPRR_EL0BR0_EL1` with
EL0 MPRR-masked merge; on change bumps SoftMMU counters and calls
`tlb_flush(CPU(cpu))`. `git apply --check` **PASS** on pin + series
0001–0011. **`series` unchanged**; durable QEMU rebuild deferred.

## Deferred

- PAuth QARMA instruction execution (`pauth-mlo` / `pauth-mhi` key material)
- macOS 27 ARM64E KC PAC entry proof on VMApple TCG
- Append VenFire 0012 to `series` + rebuild + SoftMMU host/guest smoke

`OPEN_QUESTION: Build:Append VenFire 0012 to series and rebuild qemu-system-aarch64-vmapple-tcg for SoftMMU SPRR tlb_flush host-probe OR live QEMU NVRAM BlockBackend attach without promoting macos_boot_verified?`

Implementation target: `sandbox/devices/apple_gxf_v1.c` after register
differential tests against pinned reference encoding.
