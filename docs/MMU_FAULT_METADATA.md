# Detailed stage-1 fault metadata for native memory integration

Current: the existing walker returns only a Fault class and accepts an optional
descriptor value from a physical-read callback. It loses the failing walk level
and the distinction between an invalid descriptor and unavailable backing.
TLB permission failures also lose the cached leaf level. Native SCTLR.M remains
disabled; the reference architectural caller uses coarse legacy syndromes.

Decision: root owns this isolated worktree and runtime/preos/src/mmu.rs. Add a
backward-compatible detailed translation method using a typed descriptor-read
result. Keep the existing translate API and existing Fault discriminants for
current callers; map detailed results into its documented coarse legacy form.
Do not add a second page walker. Both entry points must use the same actual
translation logic and TLB, and successful mappings must remain identical.

A detailed failure records the architectural class or typed backing error,
actual walk/leaf level when defined, descriptor-read physical address when
available, and failing output address when available. Non-table instruction
alignment and pre-walk address failures must not be relabeled as L3. Preserve
leaf level in cached translations so a later denied access reports the same
level with and without a TLB hit. Typed unavailable backing is not automatically
an architectural external abort; only an explicitly classified external-abort
reader failure may become that result. The future execution provider chooses
how to report unsupported backing and constructs ESR using access/origin.

This step does not change translation-control acceptance, enable native MMU,
claim table attribute/MAIR completeness, or change reference CPU exception
formatting. Strict descriptor profiles, whole-span translation, provider ABI
and control transactions are follow-up implementation scopes.

Validation: old walker tests remain; explicit tables test fault class/level for
supported 4KiB and16KiB levels, descriptor-read versus output addresses, typed
read failures, TLB-hit permission failures, and legacy equivalence. An independent
Arm CPU oracle checks exact FSC/level wherever modeled, with a negative control.
Keep independent model limitations explicit. No original image is needed.

Parallel ownership: CPU agent's scalar work is in a separate worktree and does
not edit this file. EFI agent authors the independent oracle outside the repo;
GPU agent reviews the narrow interface without source mutations. Root integrates
only after both CPU and memory source scopes are frozen and validated.

Resolved: AArch64 EPD reports Translation level0 independently of the selected
walk start, as defined by AArch64_S1Walk. Root output-address failures report AddressSize level0. A VA outside the
configured ranges reports Translation level0; PC alignment has no table level.
Independent EL2-issued AT explicitly selects HCR_EL2.RW=1 and disables stage2.
An initial RW-unset probe observed the AArch32 level1 rule; that probe is
superseded rather than used as evidence for AArch64 behavior.

Leaf AF/permission failures report the effective output PA including the VA
offset consistently with cached leaf failures. A rejected descriptor output
retains its invalid output value under Walk context. Cached descriptor PA is
provenance from the cached mapping and does not imply a table read at fault time.
A nonzero block/page offset regression protects these distinctions.

Public reference: Arm AArch64_S1Walk and AArch32_S1WalkLD shared pseudocode,
https://developer.arm.com/documentation/ddi0602/2026-06/Shared-Pseudocode/shared-functions-memory

OPEN_QUESTION: none for this metadata-only interface.

Independent differential correction: captured Arm inputs exposed eight VA range
failures that the old walker incorrectly classified AddressSize. AArch64
VAIsOutOfRange requires Translation level0. Both APIs now return the correct
class; the legacy API signature and enum values remain unchanged. This is an
intentional correction to prior coarse behavior, not preserved faulty behavior.
Actual root/table/leaf physical-width faults remain AddressSize. The initial
150/158 result is retained as superseded evidence, and range-gap regressions
assert that no descriptor callback runs.

Integrated validation: all91 combined architectural tests pass. The native
scalar/PAC/IRQ/pair proof and independent scalar oracle remain green, and the
actual detailed walker matches all158 captured ARM inputs plus both changed-
descriptor controls. Runnable oracle sources and original failed/final receipts
are preserved under tools/mmu_fault_levels with content hashes.
