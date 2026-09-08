# Unsigned-offset integer scalar memory

BP29 extends the existing unsigned-immediate decoder in both the x86 native
JIT and Rust reference; it does not introduce a second competing decoder.
The implementation uses the public
[Arm A64 instruction definitions](https://documentation-service.arm.com/static/67e40f3398aa3c3b6eea6a85)
for STR/LDR, STRB/LDRB, STRH/LDRH, LDRSB, LDRSH and LDRSW.
All fixtures are independently authored and contain no operating-system code.

The integer encoding has V=0, size=0..3 (1/2/4/8 bytes), and opc=0..3:

| opc | Operation | Valid sizes | Destination |
| --- | --- | --- | --- |
| 0 | Store low element bits | 1, 2, 4, 8 | Memory |
| 1 | Zero-extending load | 1, 2, 4, 8 | W for 1/2/4; X for 8 |
| 2 | Sign-extending load | 1, 2, 4 | X |
| 3 | Sign-extending load | 1, 2 | W |

W writes clear the upper 32 bits, including signed byte/halfword loads.
The effective address is Xn/SP plus unsigned imm12 scaled by element size,
with modulo-64 address arithmetic followed by range checks. Rn31 selects
SP; Rt31 supplies zero or discards a load. Loads whose base equals their
destination remain defined: the address is captured before the register
changes. There is no writeback, and NZCV is unchanged. SIMD/FP encodings,
PRFM, reserved opc/size combinations and other addressing modes remain
unsupported without a memory access; they cannot alias integer stores.

The common execution regime is little-endian EL0/EL1 with HCR/SCR zero.
Native SCTLR.M remains explicitly rejected; this milestone adds no native
MMU support. MMU-off data has Device-nGnRnE attributes, so natural alignment
is mandatory for every multi-byte transfer regardless of SCTLR.A. See the
[Arm memory model guide, sections 3.2 and 12.1](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Armv8-A%20memory%20model%20guide.pdf?revision=58b1dd0a-3800-4218-b21a-f95a0332034c).
Before effective-address checks, an SP base receives the existing SA/SA0
check on its original value. Unsupported regimes stop without retirement.

The reference retains its existing translated, naturally aligned scalar
RAM/MMIO path and bus widths. A naturally aligned element of at most eight
bytes cannot cross a supported 4-KiB/16-KiB translation page. Unaligned
translated accesses with A=0 stop as an unsupported regime before any data
access: the walker does not yet expose memory attributes or safe spanning
transactions. With A=1 they raise AlignmentFault. This is an explicit
limitation, not an assumption that translated pages are physically adjacent.
Bus methods must validate their entire scalar span before committing a
write; the RAM and device buses already receive the complete element width.

The native path preflights the full element against caller-owned RAM before
dereferencing a host pointer. A failed access preserves memory, source,
destination and base; FAR holds the guest effective address and PC remains
on the failing instruction, which is not retired. Data and alignment aborts
include IL=1 and correct WnR (all sign-extending forms are reads). SP alignment
has its existing distinct EC. Bounded RAM misses retain the runtime's current
diagnostic DataAbort/FSC=7 convention; this is not a claim that an MMU-off
hardware external abort is a level-three translation fault. Reference MMU
faults retain the existing class-only adapter's FSC=7/0x0d for translation/
permission errors; the legacy CPU adapter does not consume the actual level.
The companion detailed MMU API now preserves it for future native providers. Undefined
ISS stays zero. These limitations do not affect the exact alignment ESR.

Existing C/Rust external ABI layouts and enum values are unchanged. Validation
executes every valid size/opc with the complete imm12 range, sign edges,
SP/ZR, base/destination overlap and exact failure state in native and reference
tests. Independent Arm CPU fixtures will check assembled width/sign operations
and explicit-A alignment; negative controls must fail. QEMU omits SP checks
and also permits MMU-off unaligned data with A=0 in the observed version, so
those two rules use explicit specification-based tests rather than claiming
QEMU agreement. Actual EFI fixtures exercise this same C JIT. Private original
prefix advancement remains separate from usable macOS boot or guest Metal.

The native scalar fixture executes 106,496 cases with 426,981 assertions;
the combined scalar and MMU reference suite has 91 tests, including the same
complete imm12 matrix,
MMIO direction/width, mapped permissions and rejected spanning accesses.
The native proof runs the actual C/Rust layout comparison and existing PAC,
IRQ/vector, integer-pair and arithmetic regressions. The legacy C caller also
executes the Rust static library with 76 ABI/runtime assertions.

Reproduce with `tools/probe_efi_native_pauth.py` and
`tools/probe_scalar_memory.py`. The latter builds and executes 13 assembler
forms and explicit-A read/write alignment faults on the independent Arm CPU
oracle; changing its expected signed byte result is required to fail. A
separate native negative control that disables signed-load emission also
fails the exhaustive readback fixture. These are local CPU execution proofs;
EFI integration results are recorded separately by the EFI harness.
