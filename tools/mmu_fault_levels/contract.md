# BP29 exact MMU fault-level oracle contract

Root delegates this outside-repository directory only. Current oracle receipts
distinguish broad fault classes, while the next walker API needs exact level
and FSC. No runtime, module, root source, Git metadata or original OS assets
are changed by this work.

Wanted: independently authored page tables executed by QEMU's Arm CPU model,
reporting exact PAR fault status for invalid/reserved descriptors, valid leaf
AF/permission faults, root/table/leaf physical-width failures and supported
start-level/EPD behavior for 4 KiB and 16 KiB without LPA2. Add real EL1 abort
observations where needed to distinguish translation queries from execution.

Use an EL2 test controller with stage2 disabled for AT S1E1R/W queries so a
disabled or malformed tested EL1 address space cannot prevent the controller
from recording the outcome. The real-abort path must retain an independently
mapped code/vector region and report ESR/FAR/ELR. Record model identity and
feature limitations explicitly. Do not claim physical hardware or native JIT
MMU validation from this independent architectural oracle.

Expected FSC values come from the Arm architectural encoding (address-size
0..3, translation4..7, AF9..11, permission13..15 in the selected regimes), not
from the Nextcore walker or QEMU decoder implementation. Keep observed values
and anticipated values separate. If a model/spec uncertainty arises, preserve
the observed result and identify the limitation before adjusting expectations.

The runnable harness preserves authored sources and executable hashes, reports
per-case configuration and results, and includes a deliberately changed
descriptor negative control that must fail the original expectation. Normal
table memory is actual model RAM; out-of-width pointers must fault before an
unbacked table access. No generic success marker may replace exact code checks.

Root's follow-up delegates a host differential against the actual detailed
walker. Emit the Arm controller's real control registers/VA and sparse table
bytes, then feed those identical inputs to a Rust wrapper that imports mmu.rs
from an explicit --runtime-checkout. Do not copy or reimplement the walker.
Compare exact architectural kind/level for every representable oracle case,
and preserve typed provider failures separately from architectural faults.

The first differential proved that noncanonical virtual input was incorrectly
classified as AddressSize by the walker. Root explicitly approves correcting
that to Translation level0 in both detailed and legacy projection, preserving
signatures and enum discriminants rather than preserving incorrect semantics.
Physical root/descriptor/output AddressSize remains unchanged. Preserve the
150/158 failing receipt and verify the corrected source against identical data.
