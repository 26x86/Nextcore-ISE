# Bounded integer pair memory execution

BP28 adds standard integer STP/LDP to the native x86 translator and the
independent Rust reference. Operands are 32 or 64 bits; modes are signed
offset, pre-index and post-index. Rn31 selects SP, data register31 selects
zero/discard, and W loads zero-extend. Signed imm7 is scaled by element size.
NZCV is unchanged. Pre/post writeback happens after a successful pair.

The profile chooses UNDEFINED for constrained-unpredictable base/writeback
overlap and equal LDP destinations. STP may repeat a source. SIMD/FP pairs,
LDPSW, non-temporal pairs and reserved encodings remain unsupported. The
implementation follows the [Arm STP/LDP definitions](https://documentation-service.arm.com/static/67e40f3398aa3c3b6eea6a85)
and [Arm A64 specification](https://student.cs.uwaterloo.ca/~cs452/docs/rpi4b/ISA_A64_xml_v88A-2021-12_OPT.pdf);
no platform code is copied.

Both backends support this family in little-endian EL0/EL1, with MMU disabled
and no hypervisor/security routing. Native SCTLR.M remains gated globally.
The reference rejects this family when its MMU is enabled or its bus does
not expose bounded RAM. This avoids treating a page-crossing access as
physically contiguous or partially committing MMIO. These are explicit
unsupported-regime boundaries with no memory access or retirement.

Both RAM ranges are checked before the first transfer. A rejected pair
preserves memory, destinations and writeback. FAR records the start of the
first failing element; PC remains on the instruction and no instruction is
retired. Two scalar accesses do not claim 128-bit atomicity. Completed
reference stores invalidate overlapping exclusive reservations. Native
exclusive instructions remain outside its supported subset.

In this MMU-off, HCR=0 regime, data accesses have Device-nGnRnE attributes.
Element alignment is therefore required even when SCTLR.A=0; the bounded
host RAM allocation does not override architectural memory attributes.
This follows sections 3.2 and 12.1 of the
[Arm memory model guide](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Armv8-A%20memory%20model%20guide.pdf?revision=58b1dd0a-3800-4218-b21a-f95a0332034c).
An SP base is checked before applying
the displacement, with SCTLR.SA0 at EL0 and SA at EL1 controlling 16-byte
alignment. SP faults have exception kind14 and native status19, appended
without renumbering prior values; ESR EC=0x26, IL=1, ISS=0. Ordinary pair data
faults encode the data-abort class with IL=1 and correct WnR. See
[Arm SP alignment and exception definitions](https://www.bitsavers.org/components/arm/ARM_Architecture_Reference_Manual_ARMv8_Rev_A.k_201609.pdf).

Public v2 options/result layouts remain 64/128 bytes; the older preOS ABI
maps status19 to its existing alignment termination reason. Status19 retains
its decoded fault instruction. Authored tests cover both widths, all imm7
values and addressing modes, zero/SP behavior, overlap rejection, writeback,
bounds and alignment, followed by actual EFI readback. Original private
traces are separate evidence and do not establish SPTM service availability
or macOS boot completion.

`tools/probe_pair_memory.py` independently executes the ordinary transfers,
writeback, width/zero behavior and a data-alignment exception in QEMU.
QEMU's [SP-alignment hook is empty](https://qemu.googlesource.com/qemu/+/d27e7c359330ba7020bdbed7ed2316cb4cf6ffc1/target/arm/tcg/translate-a64.c),
so the oracle explicitly excludes SA/SA0 validation. This limitation does
not relax the specification-based native/reference SP checks.
The oracle's data-alignment case sets SCTLR.A=1 and does not validate the
MMU-off Device rule with A=0; its receipt reports that separate coverage gap.

The initial BP28 receipt allowed unaligned RAM pairs when SCTLR.A=0. That
expectation is superseded by the Device-attribute rule above. Historical
receipts remain evidence of their exact source versions, not validation of
that incorrect expectation. Corrected native/reference tests require an
alignment fault for both values of A, both element widths and all addressing
modes, with unchanged memory, destinations and base/writeback.
