# Native stage-1 memory provider v2

Status: implemented BP32 contract, with independent operation-oracle replay and
actual EFI acceptance recorded separately. Legacy M=1 gates remain unchanged;
only the new explicit immutable-profile entry may execute M=1. This document
uses authored architectural examples only and makes no macOS boot claim.

## Execution and source ownership

The change extends the existing BP31 C JIT and Rust reference runtime:

* `runtime/jit.c` includes `runtime/memory_stage1.inc` to reuse its memory decoder,
  register semantics and native emitter. The v2 dispatcher fetches one translated
  instruction and executes its generated x86 block with a null guest RAM pointer.
  Scalar/pair operations use the same decoded shape and the v2 Rust callback.
* `runtime/memory-service/src/lib.rs` retains the M=0 service. The new
  `stage1.rs` owns borrowed RAM, an immutable table image and its private walker
  state. Neither service allocates, defines a panic handler or returns pointers.
* `runtime/preos/src/mmu.rs`: `translate_detailed` is the canonical walker;
  its typed `TableReadError` already separates unavailable backing from an
  explicitly reported external abort. `FaultContext::Walk` is not ESR.S1PTW.
  strict mode additionally validates the supported descriptor subset and caches
  Normal-NC attributes. Legacy mode remains available; unmodeled hierarchy and
  attribute bits cannot silently enter the v2 profile.
* `runtime/memory_boot_v2.c` validates/copies the immutable controls and invokes
  the new entry. `runtime/arch.c` remains the precise exception/register bank.
  C control writes and reference TLBI counters are not a substitute for service
  reconfiguration; the immutable v2 entry stops at those instructions.
* The PAC slow path retains its M=1 gate and receives no new hidden permission
  to modify the translated regime. The new entry has no PAC callback argument.

## Canonical shared-source packaging

The approved external compile experiment established that the existing
staticlib-only preos package cannot be consumed directly as a Rust dependency.
The implemented layout instead shares the canonical source without its panic
handler or a duplicate walker/exception-level enum:

1. The one enum plus crate-visible `from_u8` lives in
   `runtime/preos/src/exception_level.rs`.
2. Preos root includes `mod exception_level`; `arch.rs` re-exports it to retain
   existing internal call sites. The canonical walker's type import is
   `super::exception_level::ExceptionLevel`.
3. Memory service root includes the same two canonical files using relative
   `#[path = "../../preos/src/exception_level.rs"]` and
   `#[path = "../../preos/src/mmu.rs"]`. No `arch` shim is needed in production.
4. `nextcore-memory-service` remains the same no_std rlib in the same ISE Git
   repository. No new repository or dependency on the preos staticlib is needed.
   Both crates compile the same source; each has its own private caller-owned
   state, which is intentional. `VfMmu` never crosses the C ABI.
5. Canonical Git dependency/archive validation must include both sibling source
   paths. A crate-only tarball omitting `runtime/preos/src` is unsupported;
   `publish=false` stays. The parent keeps its existing path patches/excludes.

## First complete implementation scope

Implement **initially enabled, immutable stage-1 EL0/EL1 translation**, exposed
only by a new memory-v2 entry and explicit software profile
`nextcore-stage1-fixed-nc-v1`. It is an authored compatibility profile, not an
Apple reset configuration. Old entry points and memory-v1 retain M=0 behavior
and all their M=1 gates.

The new entry receives a validated immutable control snapshot and starts with
M=1. Both instruction fetch and every currently supported scalar/pair access
must use the canonical walker. ALU/branch/CCMP execution remains generated x86.
The positive fixture must place code and data at VA != PA, including a pair
across two adjacent virtual pages backed by nonadjacent physical pages.

The initial profile is deliberately specific:

| Control | Accepted value/meaning |
| --- | --- |
| EL/PSTATE | AArch64 EL0t, EL1t or EL1h; current_el must match PSTATE mode. Only existing NZCV/DAIF/SP selection bits are variable; PAN/UAO/TCO and unmodeled state are zero. |
| SCTLR | `0x30d00803`, optionally OR SA bit 3 and SA0 bit 4. This preserves the baseline RES1 pattern and requires M=1, A=1, little endian, C=I=WXN=0. All other values reject this profile before effects. |
| TCR | TG0/TG1 select the same 4 KiB or 16 KiB granule with their distinct encodings; both T0SZ/T1SZ are architecturally valid. IPS=0..5, EPD0/1 modeled. IRGN/ORGN/SH fields are zero: noncacheable, nonshareable table walks. All remaining bits zero, including A1/AS/TBI/HA/HD/HPD/TBID/E0PD/DS. |
| TTBR0/1 | ASID=0, CnP=0, no unmodeled high bits, whole-granule aligned table roots. The existing `T1SZ=0 means absent` shortcut is not accepted by this strict profile; use valid T1SZ plus EPD1 for a disabled upper walk. |
| MAIR | exactly `0x44`: Attr0 is Normal inner/outer noncacheable; Attr1..7 are zero and cannot be selected by accepted leaf descriptors. |
| HCR/SCR | zero inactive diagnostic fields; this profile exposes only EL0/EL1, with EL2/EL3 absent, a single Non-secure PA domain, and no stage 2. This is not a measured hardware HCR/SCR reset claim. |
| Epoch | exactly 1 for the complete run; immutable controls and table image. |

A=1 makes natural element alignment a fully supported rule for this first
stage. A=0 is a profile rejection, not a fabricated alignment exception for
Normal memory. Broader Normal unaligned handling and Device memory are separate
extensions. M=1 PAC execution remains explicitly unavailable until its existing
provider and control contract are extended; PAC with M=0 is preserved.

Both VA halves support the existing walker limits: 4 KiB TnSZ16..39, 16 KiB
TnSZ17..47, IPS32/36/40/42/44/48. Physical backing can be smaller than the
architectural PA range; missing backing is a provider condition, not a guest
address-size fault. DeviceTree parsing stays above this API and cannot redefine
the architectural memory type or prove that absent backing is a bus abort.

## Descriptor and memory-attribute boundary

Add strict-profile selection to the **canonical** walker, preserving its legacy
translate/translate_detailed callers by default. Do not validate hierarchical
bits in the service's `read64` closure: that closure lacks descriptor level and
kind, and such a workaround would become a second partial decoder.

For this profile, table descriptors permit their architectural type/address and
software-use bits58:55. APTable/PXNTable/UXNTable/NSTable must be zero. Page/block
descriptors permit type/output address, all AP values, AF, UXN/PXN, nG, and
software-use bits58:55. AttrIndx/SH/NS must be zero. Contiguous, DBM, GP, LPA/LPA2,
hardware-use attributes, and every other unmodeled bit reject the profile.
Misaligned block-output address bits must not be silently discarded.

Invalid descriptor types and the existing DS=0 reserved 16 KiB L1 block retain
their architectural Translation faults. Valid but unsupported descriptor modes
return a new typed `Unsupported` failure with descriptor provenance; they do not
become fabricated guest permission/translation faults. Strict checks occur in
the canonical decoder after existing descriptor validity/output-width checks
and before returning or caching a supported leaf. Unsupported inputs have no
claim to architectural fault priority. Accepted-input AF/AP/XN fault priority
stays canonical and is checked by the independent oracle.

Extend canonical Translation and its private TLB with validated memory-attribute
metadata and strict-profile identity. This can be a small typed enum plus raw
AttrIndx/SH for diagnostics. A cold success and its TLB hit must return identical
attributes. Strict mode never accepts a legacy cache entry. Add typed unsupported
failure to the same walker; the legacy wrapper's existing accepted inputs and
coarse error mapping remain unchanged.

## Caller-owned backing and preflight transaction

The v2 service owns a mutable RAM slice and borrows a separate immutable table
image with explicit physical base/length. Their host and guest-physical spans
must not overlap, lengths must be nonzero, and end arithmetic must not overflow.
The table image is backed memory, not a callback-supplied host pointer. All table
reads must resolve complete 8-byte descriptors within it. A table pointer into
another unsupported region returns typed unavailable backing.

Ordinary reads may read the table image; stores to it stop as unsupported
provider backing unless translation already denies the write architecturally.
The table image cannot be changed concurrently or through guest aliases. This
explicit immutable-table arrangement makes retaining the canonical TLB sound
without silently inventing TLBI behavior. It is temporary scope, not normal
macOS startup memory layout.

For fetch validate PC alignment before table access. For data, preserve the C
original-SP SA/SA0 check before effective-address computation, then natural
element alignment under A=1. Arm D1.13.3 and D4.7.3 establish these priorities;
authored actual EL1 abort cases independently check the data ordering. Known
QEMU PC-alignment overlap disagreements remain failed model comparisons below.
Use the walker for each covered element/page and preflight the full byte span,
not just the first PA or first element. Never assume adjacent VA pages map to
adjacent PA pages. The maximum current operation is 16 bytes, so a fixed local
array of byte/chunk locations suffices without allocation.

Before a store, all translations, permissions, profile attributes, and backing
bounds for both elements must succeed. Before a load, all checks must succeed
before destination/writeback changes. After preflight, byte transfers cannot
fail in the owned slices; then C commits result registers/SP, PC+4 and one
retirement. This provides failure atomicity for canonical service preflight
architectural, unsupported-profile, and unavailable-backing failures; it
does not claim architectural pair atomicity against other observers. TLB fills
from successful preflight are permissible microstate; rejected operations do
not change guest RAM/registers, descriptor AF, PC or retirement.

The synchronous callback and its owner are trusted. A broken wrapper can alter
the reply or return a transport failure after a successful STORE has already
changed RAM. C rejects that acknowledgement and does not retire or commit
registers, but cannot roll back callback-side effects. Such host/provider-error
runs are not resumable precise guest-state results. The result counter
`completed_data_operations` counts validated acknowledgements, not evidence
that RAM stayed unchanged when an acknowledgement failed. An actual STORE-only
reply-epoch mutation and postcommit callback failure are regression tests of
this limit; no two-phase transaction ABI is implied.

## Fixed-width memory ABI v2

New named records; do not change v1 request80/reply80/result192, existing
platform options64/result128, CPU layout888, or PAC ABI. All records align8,
use only fixed-width integers, little-endian host ABI, and C/Rust extern C
(including actual Win64 UEFI verification). The new named entry and records
are isolated from every existing v1 entry and layout.

**Controls80**: offsets0/4/8/12 u32 version=2, size=80, profile=1, reserved=0;
offsets16/24/32/40/48/56/64/72 u64 SCTLR, TTBR0, TTBR1, TCR, MAIR, HCR, SCR,
epoch=1. The entry copies this value before execution. C and Rust both validate
it; no shared mutable register bank pointer is passed.

**Request160**: offsets0/4/8/12/16/20/24/28 u32 version=2, size=160,
operation(1 fetch/2 load/3 store), flags=0, width, count, current_el, reserved0=0;
offsets32/40/48/56/64 u64 PC, VA, value0, value1, PSTATE; offset72 Controls80;
offset152 u64 reserved1=0. C supplies its actual current controls every time and
the service rejects any mismatch with the initialized epoch/snapshot. The
service must not reconfigure/invalidate its TLB on every request.

**Reply128**: offsets0/4/8/12/16/20/24/28 u32 version=2, size=128, result,
fault, level, context, FSC, metadata_flags; offsets32/40/48/56/64/72/80 u64
value0, value1, address, ESR, epoch, descriptor_PA, output_PA; offsets88..127
five reserved u64 zeros. Results:0 success,1 architectural guest fault,
2 unsupported profile,3 unavailable physical backing,4 invalid request.
Fault:0 none,1 PC alignment,2 data alignment,3 AddressSize,4 Translation,
5 Permission,6 AccessFlag. Level absent is UINT32_MAX; otherwise0..3. Context
uses explicit input/walk/leaf/cached-leaf values; flags distinguish whether
descriptor_PA/output_PA are present. No host pointer is returned.

Success returns only raw width-bounded values, epoch, zero fault/FSC/ESR/address,
absent level/context/provenance and zero reserved fields. Stores return zero
values; unused value1 is zero. C applies signed loads/W-register zero extension.
Invalid requests return no values or diagnostic addresses. Nonarchitectural
failures return fault/FSC/ESR zero; typed context/provenance is allowed when
known. `address` for these failures is the first failing element/byte VA in
request order; it is a diagnostic position, not FAR. Exact wrap is handled
without unchecked pointer arithmetic; no span crossing 2^64 is accepted.

For a guest fault, `address` is the actual faulting VA (FAR), not a descriptor
PA. C validates that it is the start of a requested element, cross-checks kind,
context, level, current EL and load/store direction, and reconstructs the exact
supported ESR before comparing it with the reply. No arbitrary provider ESR is
trusted. Guest values are zero on every failure. A nonzero callback return or
malformed reply is a host/provider failure with no synthesized guest exception.
AF/Permission must identify a valid leaf level (4 KiB:L1..L3; 16 KiB:L2..L3),
not merely any number0..3. Input AddressSize/Translation use the established
level0 cases. Walk levels must be reachable from the configured start level;
cached-leaf metadata can report only Permission: immutable successful fills
cannot later discover an AddressSize, Translation or AF error. Walk Translation
has descriptor-only provenance; Leaf Translation is impossible. Leaf AF and
Permission include both descriptor and effective output PA, each inside IPS.
AddressSize requires provenance identifying an actual PA outside IPS: the
root output at Input, descriptor address/output at Walk, or effective output
at Leaf. Descriptor addresses must be eight-byte aligned and all metadata
addresses fit the supported 48-bit representation. Alignment has no level or
PA provenance. A known-misaligned request rejects success or a different guest
fault, regardless of a plausible ESR. A wrapping full span also cannot receive
a successful acknowledgement; the inclusive last byte is checked, permitting
an aligned four-byte fetch ending exactly at UINT64_MAX. C checks these
combinations rather than accepting independently plausible fields.

Use a new entry `vf_boot_run_memory_v2` taking Controls80, the callback and its
caller-owned service, alongside existing execution/protection inputs. It must
not reuse the legacy physical-entry validation for guest virtual PC/SP/args:
entry fetch validates the VA through the service; args may be a guest VA and
is validated only when accessed. C still never receives a guest RAM pointer.
Result320 can retain the v1 result field layout as a **new version=2 size=320**
record followed by the full Reply128 at offset192. It preserves an independent
provider status, precise guest FAR, first failing VA and callback counters.
Old v1 function/result values are unaffected.

## Exact abort and execution boundaries

For accepted stage-1 faults, FSC is AddressSize level L=0x00+L,
Translation=0x04+L, AccessFlag=0x08+L and Permission=0x0c+L. Data alignment is
0x21 without a translation level. PC alignment uses EC0x22, IL1, ISS0.
Instruction abort uses EC0x20 from EL0 or0x21 at EL1; data uses0x24/0x25.
All supported A64 aborts have IL1; data WnR reflects the operation and ISV=0.
S1PTW stays0 for these ordinary stage-1 walks; descriptor provenance does not
turn it on. No FnV, EA, CM, external-abort FSC, tag fault or stage-2 syndrome is
accepted by this first service. Explicit `TableReadError::ExternalAbort` remains
typed in the canonical API, but this RAM/image service has no hardware source
that can assert it and must never manufacture one.

A guest abort stops at the faulting instruction without retirement or RAM/
destination/writeback change; ESR/FAR/ELR/SPSR are committed using the existing
precise exception API, not its legacy inferred level3 syndrome helper. A fetch
abort has no decoded instruction word. Provider failure stops at the same PC,
without inventing an exception, and reports its independent provider status.
Existing pending IRQ/FIQ input is polled between guest instructions and retains
the current return-at-vector contract. Handler execution/ERET is not implied.

## Control changes, barriers and cache lifetime

In this first immutable profile, guest MRS of supported controls may return the
copied bank, but **all MSR writes to SCTLR/TTBR/TCR/MAIR/HCR/SCR stop before any
bank or service change**, even if the value is identical. No accepted callback
can mutate those controls. ISB, DSB, DMB and TLBI remain explicit unsupported
instruction boundaries in the new native profile until a control-operation
callback is implemented; do not inherit reference ordered-no-op behavior as
evidence for native support. This restriction is part of the named profile.
There is no pending control commit and epoch remains1. Existing M=0 paths retain
their present behavior. Ordinary memory/register instructions remain executable.

The subsequent dynamic-control contract should add two-phase validate/commit:
C decodes an MSR, validates the candidate snapshot with Rust before architectural
bank writes, ends the native block, then commits an agreed effective epoch.
MRS reads the architectural bank; instruction context synchronization and data
translation visibility need explicitly tested rules. An ISB cannot be treated
as an instruction whose old/new fetch mapping is automatically safe: MMU enable
needs a mapped transition sequence. TLBI invalidates the canonical Rust TLB via
an explicit operation and acknowledgement, with the required DSB/ISB ordering.
Changing MAIR/permissions also requires cache invalidation semantics. No new
persistent x86 code cache is introduced here; future cache keys require physical
code identity, effective translation epoch and invalidation on stores/maintenance.

The approved immutable stage is one complete executable result: nonidentity
native fetch/loads/stores and precise permission/fault effects using the same
walker. Dynamic enabling, mutable page tables, full attribute semantics,
translated PAC, handler dispatch/ERET and the separate EL1t stack-bank correction
are subsequent contracts. They remain required for general boot and stable use.

## Required acceptance and independent controls

* Run the same canonical walker tests from preos and memory service, exact
  equality of cold/hot attributes/fault metadata, UEFI no_std compilation and
  canonical standalone Git source packaging. No separate walker expectations.
* Actual C-to-Rust callbacks execute generated x86 with VA != PA for instruction
  fetch, all13 scalar forms and32/64 pair offset/pre/post forms. Cover high
  TTBR1 VA and both granules. Deliberately use discontiguous physical pages.
* Whole-pair second-page permission/translation/AF/backing/unsupported-attribute
  failure preserves first destination, SP/base and every byte of RAM. Include
  callback malformed ESR, epoch, level, width, PA/VA confusion, wrong WnR and
  false S1PTW. No host pointer appears in a reply.
* Authored Arm oracle captures real EL1 ESR/FAR/ELR for each supported fault
  family/level and lower/same EL where applicable. Compare faults on actual
  load/store/fetch, not only AT/PAR. Include alignment plus invalid translation
  priority. Known QEMU MMU-off Device/SA omissions stay documented.
* The native negative control bypasses memory dispatch. Separate compiled
  service mutations reuse first-element locations, omit second-element store
  permission, and turn unavailable backing into a guest abort. The same authored
  success/failure fixtures must detect every mutation.
* Actual x86 OVMF invokes the Win64 C/Rust ABI and nonidentity authored code with
  native block and callback evidence. Regress all legacy M=0/PAC/IRQ/CCMP paths.
  No original diagnostic is necessary to prove this authored capability.

## Reproduce the runtime checks

From the standalone ISE checkout on a Linux x86_64 build host with Rust and
clang, use fresh output directories outside the source tree:

```sh
python3 tools/probe_efi_stage1_provider.py --work-dir "$HOME/nextcore-proof/stage1" --output "$HOME/nextcore-proof/stage1/receipt.json"
python3 tools/probe_efi_memory_provider.py --work-dir "$HOME/nextcore-proof/memory-v1" --output "$HOME/nextcore-proof/memory-v1/receipt.json"
python3 tools/probe_efi_native_pauth.py --output "$HOME/nextcore-proof/native-legacy.json"
cargo test --manifest-path runtime/preos/Cargo.toml --lib --offline
cargo build --manifest-path runtime/memory-service/Cargo.toml --release --target x86_64-unknown-uefi --offline
python3 tools/mmu_fault_levels/compare_current_walker.py --runtime-checkout . --at-report tools/mmu_fault_levels/captured-at/report.json --abort-report tools/mmu_fault_levels/captured-abort/report.json --output "$HOME/nextcore-proof/walker-current"
```

The BP32 runtime checkpoint passes 10 actual C/Rust host integration tests,
39 service/shared-walker tests, all four semantic negative controls, 98 preos
tests, the legacy native and memory-v1 probes, and the unchanged 158 captured
Arm cases with both existing negative controls. The walker comparison imports
both the canonical enum and walker and records hashes before/after; historical
BP29 receipts are preserved. The service also builds as a no_std UEFI rlib.
These host checks do not substitute for the separate actual Win64 EFI suite.

The independent operation oracle captures real Arm execution under a QEMU
model. Captured nonzero BTYPE state is outside the current profile; an exact
replay must report that rejection rather than clearing bits. HCR_EL2.RW used
by the oracle harness is explicitly distinct from this provider's inactive
EL2/HCR=0 model. Normative PC-priority disagreements remain failed model
comparisons, never successes or reasons to change the implementation order.

## Primary sources and review decisions

BP32 independent actual-Arm review established a prior canonical permission
bug: AP=01 (EL0 writable) implicitly forbids privileged instruction execution,
even with PXN=0 and WXN=0. The existing PXN-only implementation accepted it.
The canonical fix applies to both legacy and strict-profile callers, including
cached translations. This is an architectural correction rather than a profile
rejection. Arm's [employee clarification](https://community.arm.com/forums/f/architectures-and-processors-forum/53584/cortex-a-permission-fault-due-to-code-region-mapped-as-read-write/178834)
identifies DDI0487I.a information statements WXKKQ/LJHZZ, and the independent
AP/PXN/UXN matrix captures PSTATE.PAN=0, WXN=0 and zero hierarchy restrictions.
Historical oracle expectations and new actual-Arm comparisons must distinguish
this intentional correction from the ABI-preserving source extraction.
The implicit-XN correction is evaluated specifically for EL1, not blindly for
every privileged level. Legacy EL2/EL3 calls retain the previous coarse PXN
result; they do not acquire a correctly modeled EL2/EL3 translation regime.
The new service rejects EL2/EL3 before translation. Cached entries retain AP
information and evaluate the requesting EL instead of baking an EL1 decision
into a bit later reused for unrelated privilege levels.

Arm DDI0487B.a TableD4-34 (D4-2077/2078) also specifies that EL0 instruction
execution uses UXN rather than the AP data-access restriction; AP00/AP10 with
UXN=0 can be executable at EL0. The canonical cold and cached paths previously
denied those fetches and are corrected together. Arm D1.13.3 (D1-1827) gives
PC alignment higher priority than instruction abort; D4.7.3 (D4-2110) puts
A=1 data alignment ahead of translation/access-flag/permission. The
[primary Arm manual, independently hosted](https://cs140e.sergio.bz/docs/ARMv8-Reference-Manual.pdf)
has SHA25624ebe2e085a4f9e6588ac8118e2b8747baec35184c44211e01751eb176c1d76e.
The QEMU overlap cases that report instruction abort ahead of PC alignment are
documented oracle limitations; they do not change the implementation priority.

The existing C/Rust register decoder also used CRm0 for MAIR_EL1. The actual
register uses S3_0_C10_C2_0, so the C key is0x4510 and the MRS/MSR base words are
0xd538a200/0xd518a200. Both canonical decoders are corrected; the old alias is
rejected. The [LLVM system-register definitions](https://raw.githubusercontent.com/llvm/llvm-project/main/llvm/lib/Target/AArch64/AArch64SystemOperands.td)
and independently assembled/disassembled `mrs x3, mair_el1` agree. The reference
test executes the corrected write/read pair, and the actual native-v2 fixture
reads the immutable MAIR while proving that its writes remain unsupported.

Arm's [Memory management guide](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/LearnTheArchitecture-MemoryManagement-101811_0100_00_en.pdf?revision=1fdc3375-d81c-4457-b786-04fb98557de0)
describes the two VA ranges, granules, walk controls and TLB roles (§§5–8).
Arm's [Address translation guide](https://documentation-service.arm.com/static/5efa1d23dbdee951c1ccdec5)
describes context-changing control writes and synchronization (§3.4), descriptor
attributes and permission hierarchy (§4.5), permissions (§7), and maintenance
after table changes (§10). These establish why a plain address-only callback
and a generic ISB no-op are insufficient. The restrictions above are our bounded
profile decisions, not claims that Arm requires every application to use them.

Arm's [Armv8.1 supplement](https://documentation-service.arm.com/static/5fb7d32fd77dd807b9a80c61)
§B3 distinguishes hierarchy-disabled control from hierarchy fields that must
apply; the proposed first profile does neither silently. The primary
[Trusted Firmware-A register definitions](https://raw.githubusercontent.com/ARM-software/arm-trusted-firmware/master/include/arch/aarch64/arch.h)
define the two0x4 Normal noncacheable MAIR nibbles and baseline SCTLR bits;
[its descriptor definitions](https://raw.githubusercontent.com/ARM-software/arm-trusted-firmware/master/include/lib/xlat_tables/xlat_tables_defs.h)
identify descriptor field positions. Full accepted fault rules also retain the
existing canonical walker's BP29 actual-abort oracle evidence and must receive
the operation-level checks listed above before publication.

Decision: root approved the immutable Normal-NC/A=1 scope, canonical source
sharing and 80/160/128/320 record sizes before implementation. Future controls,
profile expansion or boot claims require their own implementation evidence.

Historical BP29 `tools/mmu_fault_levels/compare_walker.py`, its README and
`results.json` describe their original source revision and remain unchanged.
Use `compare_current_walker.py` for the extracted canonical enum and current
walker; it records both current source hashes and never rewrites old receipts.
