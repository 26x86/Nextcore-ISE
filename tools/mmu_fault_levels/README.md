# Exact stage1 MMU fault-level oracle

The final authored oracle passes **118 AT/PAR cases and40 real EL1 abort cases**
on QEMU8.2.2's `max` Arm CPU model. Both changed-descriptor negative controls
are detected. The corrected detailed walker matches all158 captured cases and
both mutated controls. No Nextcore walker code or OS image is used to compute
the independent architectural outcomes.

## Reproduce

Copy the seven runnable files together: `oracle_start.S`, `oracle.c`,
`oracle.ld`, `probe_fault_levels.py`, `abort_start.S`, `abort_oracle.c` and
`probe_abort_levels.py`. Python3, Clang, LLD and `qemu-system-aarch64` are needed.
Choose new persistent output directories:

```sh
python3 probe_fault_levels.py --output "$HOME/nextcore-evidence/mmu-fsc-at"
python3 probe_abort_levels.py --output "$HOME/nextcore-evidence/mmu-fsc-abort"
```

The scripts compile freestanding AArch64 ELFs and run only those authored
images. Generated case headers, executables, logs, commands, model identity,
source hashes before/after and executable hashes remain in the output. Each
report includes the unmodified expectation and observed register values.
Existing output directories are rejected so receipts are never overwritten.

The latest architectural evidence is `captured-at/report.json` and
`captured-abort/report.json`. These also contain the actual table/control
inputs used by the CPU, suitable for the walker differential. Earlier
`run-r1`, `abort-r1` and `final-*` receipts remain historical. Comparison files beginning
`review_epd_` are not required by either runnable tool.

## Results for the detailed walker API

| Condition in the selected AArch64 regime | Exact FSC/level observed |
| --- | --- |
| Invalid/reserved descriptor | Translation `4 + descriptor_level` |
| Out-of-width TTBR root | AddressSize0, regardless of initial lookup level |
| Out-of-width next-table address | AddressSize at the descriptor's current level |
| Out-of-width block/page output | AddressSize at the leaf level |
| AF clear on a valid leaf, hardware AF update disabled | AccessFlag `8 + leaf_level` |
| Read-only leaf written; PXN leaf executed | Permission `12 + leaf_level` |
| EPD0/EPD1 enabled on a cold walk | Translation0, FSC4, regardless of initial level |
| Noncanonical input outside both configured VA regions | Translation0, FSC4 |

4 KiB descriptor coverage spans levels0..3; valid leaves are levels1,2,3.
16 KiB descriptor coverage spans levels1..3; valid leaves are levels2,3.
With DS=0, the 16 KiB level1 block encoding is reserved and produces a level1
translation fault. Invalid types are checked for both TTBR halves. Starting
levels0/1/2 for 4 KiB and1/2/3 for16 KiB include reduced-width roots. Physical
width faults use IPS32; positive leaf controls use the same otherwise-invalid
output with IPS36. This suite does not claim exhaustive physical-width testing.

These interpretations follow Arm's `AArch64_S1Walk`, `AArch64_S1Translate`,
descriptor decoding and FSC encoding rules. The primary Arm-authored
[shared pseudocode is mirrored at Stanford](https://www.scs.stanford.edu/~zyedidia/arm64/shared_pseudocode.html).
The [official PAR_EL1 definition](https://developer.arm.com/documentation/ddi0601/2026-03/AArch64-Registers/PAR-EL1--Physical-Address-Register)
defines translation-query fault reporting. Expectations are generated from
the architectural categories and explicit levels, not from QEMU or Nextcore
implementation source.

## Why EL2 setup matters

The AT controller executes at EL2 with **HCR_EL2.RW=1 and stage2 disabled**,
while configuring EL1 stage1 with SCTLR.M=1. This permits even an invalid root
or disabled walk to be queried without breaking the controller's own fetch.
Both scripts record and verify actual HCR_EL2 and CurrentEL values.

A separate review fixture initially reported EPD FSC5 because it left
HCR_EL2.RW=0. Setting only RW changed that result to FSC4. This is an AArch32
versus AArch64 regime difference, not a QEMU disagreement with the selected
AArch64 contract. Arm's AArch32 long-descriptor walker reports EPD at level1;
its AArch64 walker reports level0. The original review fixture was not edited;
the corrected copy and differential receipt remain outside the repository.

## Real abort verification and limits

The real-abort controller gives EL1 separate identity-mapped code/vector memory
through TTBR0 and targets TTBR1. Authored loads, stores and branch targets cause
real synchronous EL1 exceptions. The EL1 handler samples ESR, FAR and ELR, then
uses HVC to return those values to the EL2 controller. Every positive case
checks exception entry, exact FSC, EC, IL, read/write direction where relevant,
FAR equal to the intended guest VA and ELR equal to the actual faulting access
or branch target. It does not synthesize the expected syndrome in the handler.

Both negative controls replace the first invalid level0 descriptor with a
valid table link while retaining the original expectation. They reach an
invalid level3 descriptor instead: observed FSC7 disagrees with expected FSC4.
The normal VM process exits cleanly, so a crash or missing marker cannot count
as successful negative-control detection.

Limits: one independent software CPU model, no physical-hardware receipt;
no native x86 JIT MMU verification; no stage2, LPA2, TBI, hardware AF/dirty
updates, EL0 permissions, hierarchical table-permission or live TLB-coherence
coverage. The controller flushes translations between cases. AT results do
not prove real memory access by themselves, which is why the separate abort
suite covers40 actual accesses. MMIO/external-abort routing is outside scope.

## Compare the actual detailed walker

`compare_walker.py` requires Rust and an explicit ISE checkout with
`translate_detailed`. It imports that checkout's actual `mmu.rs` by path; it
does not copy or implement a second software walker. Its tiny ExceptionLevel
type adapter only supplies the standalone module's EL1/EL0 comparison type.

```sh
python3 compare_walker.py --runtime-checkout /path/to/Nextcore-ISE \
  --at-report "$HOME/nextcore-evidence/mmu-fsc-at/report.json" \
  --abort-report "$HOME/nextcore-evidence/mmu-fsc-abort/report.json" \
  --output "$HOME/nextcore-evidence/mmu-walker-comparison"
```

The controllers emit their actual TCR, TTBR0/1, VA, access type, physical table
bases and sparse descriptor contents. The comparison reads exactly those
target table bytes, treating the rest of the four allocated16 KiB arrays as
zero just as the controller initialized them. No identity code/vector mapping
is reconstructed: this host comparison translates only the measured target
access. Unavailable backing remains a typed reader error and cannot masquerade
as an architectural translation failure. The report preserves the runtime
source digest before/after, generated wrapper, all captured inputs and each
observed detailed result. Positive PA comparisons retain the VA page offset.

The first comparison exposed eight noncanonical-input discrepancies: the
walker returned AddressSize0 where AArch64 requires Translation0. Its other
150 outcomes matched, including all40 real-abort cases. The failed receipt is
preserved in `differential-r1/report.json`. Root corrected that classification
in both detailed and legacy projections while retaining signatures and enum
discriminants; this intentionally fixes incorrect behavior. Actual physical
root/table/output AddressSize semantics were not changed.

`differential-final/report.json` passes all158 unchanged captured inputs and
both changed-descriptor controls. Root-address, EPD and noncanonical-input
cases also require zero table-reader callbacks. The controls must produce the
same FSC7 seen by the independent CPU while rejecting the original FSC4
expectation; an unrelated provider failure is not accepted as detection.
