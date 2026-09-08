# One-way dynamic stage-1 enable

Status: BP34 implementation contract, approved scope; numeric ABI frozen before code. Base is ISE `720d7c61140e39da0f457192bd88138a52017e22`. This new opt-in profile executes authored M=0→M=1 transitions in the existing generated-x86 EFI JIT. Original v1/v2 discriminators, gates, layouts and behavior remain unchanged; `vf_cpu` remains 888 bytes. No second walker or interpreter is introduced.

## Profile and ownership

The profile is `nextcore-stage1-enable-nc-v1`, numeric profile 2 in a new data discriminator 3. Initial EL1t/EL1h controls are a complete BP32-valid seed except SCTLR.M=0; A=1, C=I=0, little endian, optional SA/SA0, fixed 4K/16K granule and IPS, MAIR=0x44, HCR=SCR=0, ASID=0. Architectural revision and effective epoch start at 1. PSTATE permits the BP32 NZCV/DAIF/mode subset and starts with I/F masked. BTYPE remains unsupported.

The service exclusively borrows mutable RAM and separately borrows an immutable table image. Both complete host/physical spans are validated and disjoint. The owner exposes no mutable RAM alias. Guest writes to the table image remain unsupported. Between the successful enable MSR and ISB, only the guarded ISB fetch/control operation is permitted: no store, load, alternate instruction, another control write, or host callback reentry can modify the code. The synchronous owner/lifetime contract excludes concurrent host mutation. This is the actual basis for transition-code immutability; there is no invented external reservation.

At PREPARE for SCTLR.M 0→1, the entire next four-byte instruction span must not wrap, must be owned physical RAM, must contain the exact allocated ISB SY word, and must translate under a candidate canonical walker to the same physical bytes with valid execute permission and supported attributes. Every byte is checked. Failure is an explicit unsupported-profile or unavailable-backing result before the MSR retires, not a fabricated guest abort. After enable COMMIT, the same old-context ISB bytes are fetched and checked again. The following fetch after successful ISB uses the new mapping and can fault precisely after the ISB has retired.

[Arm Address Translation §3.4](https://documentation-service.arm.com/static/5efa1d23dbdee951c1ccdec5) states that control effects become guaranteed after context synchronization and explains why the instruction after the SCTLR.M write must be safe under both mappings. Keeping an effective snapshot until ISB is this deterministic profile's choice, not a claim that hardware invariably waits. MMU-off data retains Device alignment semantics, independently of the mapped Normal-NC profile.

## Control state and supported operations

The C CPU bank and service architectural snapshot A change only after acknowledged MSR COMMIT. MRS reads A. Memory requests carry the separately maintained effective snapshot E and epoch. ISB applies pending A to E through the canonical walker and advances the epoch; no-pending ISB does not invent a mapping change. DSB SY acknowledges completion of this synchronous single-core memory service without invalidating the TLB. Local VMALLE1 actually calls canonical invalidate and advances a separate invalidation generation. Counter overflow rejects before effects.

While M=0, TTBR0/TTBR1/TCR/MAIR writes must produce another fully valid supported seed; granule, IPS, SCTLR attribute/alignment settings, MAIR, HCR/SCR stay within the initial profile. An earlier control update must be synchronized before enabling. Only SCTLR.M 0→1 is an accepted SCTLR change. M=1 translation-control writes and M clearing are unsupported. No new permission to accept arbitrary reset values, change tables, execute ERET, cache operations, PAC, other TLBI forms or unknown barriers is implied.

DAIF writes remain unsupported system boundaries in this bounded dispatcher; they never unmask I/F while A differs from E. Interrupt polling retains the existing return-at-vector mechanism, but this entry starts with I/F masked and adds no guest unmask or handler-dispatch support. Recording a synchronous abort at the fault PC is distinct from entering an exception handler; no unimplemented context synchronization is hidden in a diagnostic stop. A pending enable admits only the guarded ISB. Other unsupported instructions/modes stop before their effects.

## Fixed-width ABI

The new data request/reply use the existing 160/128-byte field layout, align 8, with discriminator 3; the embedded Controls80 uses discriminator 3/profile 2 and the current nonzero effective epoch. Request fields and raw width/sign conventions otherwise retain the v2 contract. New named aliases may share the canonical record definitions; old v2 validators still require discriminator 2/profile 1/epoch 1. No reserved field changes meaning for an old caller.

ControlSnapshot64 consists of eight u64: SCTLR, TTBR0, TTBR1, TCR, MAIR, HCR, SCR, reserved=0.

ControlRequest192, align 8:

* u32 offsets0..31: version=1, size=192, phase, operation, selector, current_el, flags=0, reserved=0.
* u64 offsets32/40/48/56: PC, operand-or-token, expected architectural revision, expected effective epoch.
* expected A at64; candidate A at128.

ControlReply192, align 8:

* u32 offsets0..31: version=1, size=192, result, detail, phase, operation, selector, state_tag.
* u64 offsets32/40/48/56: token, architectural revision, effective epoch, invalidation generation.
* A at64; E at128.

Closed numeric enums:

| Field | Values |
| --- | --- |
| phase | 0 final snapshot only; 1 PREPARE; 2 COMMIT; 3 CANCEL |
| operation | 0 none/final entry snapshot; 1 WRITE_CONTROL; 2 ISB_SY; 3 DSB_SY; 4 TLBI_VMALLE1 |
| selector | 0 none; 1 SCTLR; 2 TTBR0; 3 TTBR1; 4 TCR; 5 MAIR |
| reply result | 0 success; 1 unsupported profile; 2 invalid request; 3 unavailable backing |
| state_tag | 0 empty error record; 1 proposed postcommit state; 2 committed/known state; 3 host-uncertain final state only |
| detail | 0 none; 1 unsupported control; 2 pending-window violation; 3 transition span wrap; 4 transition mapping/permission/attribute failure; 5 wrong transition bytes; 6 stale context; 7 invalid/replayed token; 8 malformed request; 9 counter overflow; 10 missing backing |

Successful PREPARE returns state_tag=1, a nonzero one-shot token, and predicted postcommit A/E/counters. It does not change architectural state, effective state, live TLB or RAM. Successful COMMIT repeats that predicted state with state_tag=2/token=0 and applies it exactly once. Successful CANCEL reports the unchanged committed state with token=0. Requests bind owner, PC, operation, selector, old revision/epoch and complete candidate; a stale or double token cannot commit again. All failures use state_tag=0, token/counters/snapshots zero and a nonzero closed detail. No pointer is retained.

The C side validates the full proposed reply before COMMIT and the committed reply against that proposal before changing CPU registers, PC or retirement. A rejected guest control instruction does not retire. PREPARE and COMMIT are host protocol phases for one guest instruction, not two retirements. Canonical service rejection before COMMIT preserves architectural/effective state. A malformed or failed PREPARE response can leave an unconsumed service token, so the owner must be discarded rather than resumed even though the last acknowledged A/E is still known. A transport failure or malformed reply after COMMIT was sent can leave service-side state uncertain; it is host-fatal and never retried automatically.

The new run result has size512/alignment8 and discriminator3. It uses the existing 320-byte memory-result layout as its prefix and appends ControlReply192 at offset320 as a **final state record**, phase0/token0. state_tag=2 contains the last C-acknowledged committed A/E/counters; a prior uncommitted PREPARE proposal is never reported as final state. state_tag=3 marks post-COMMIT uncertainty and retains only the last C-acknowledged snapshot, which is not claimed to match the service. Invalid replies are never copied into that record. Initial state is phase0/op0/selector0/tag2/revision1/epoch1/invalidation0.

Existing provider status values retain their meaning: 0 okay,1 unsupported,2 invalid request,3 invalid reply,4 callback failure,5 unavailable. A control profile/host failure returns VF_DATA_FAULT(4), sets that independent provider status, leaves ESR/FAR unmanufactured and preserves fault PC/retirement. Proper later data/fetch aborts retain the v2 exact exception rules. Unsupported ISA outside the control protocol keeps its existing native status. Runs with any host-uncertain tag are not resumable precise guest-state results. As in v2, a broken trusted data callback can commit a store before corrupting its acknowledgement; completed_data_operations counts validated acknowledgements and does not promise RAM rollback after host failures. This constructor-style entry starts from a fresh owner and provides no resume API, including after a budget stop with known but unsynchronized controls.

## Implementation and acceptance

The new service reuses the canonical Rust walker and existing preflight/record validation logic. Shared internal helpers may be extracted without widening v1/v2 acceptance. A separate dynamic dispatcher supplies E to memory requests and handles the control protocol; generated x86 retains the existing ALU/branch/CCMP and scalar/pair decode/register semantics. No guest host pointer or new persistent code cache is introduced.

Required proofs: exact C/Rust offsets and actual callbacks; 4K/16K flat-guard enable followed by nonidentity native fetch/data; architectural MRS before effective ISB commit; full-span guard negatives; missing post-ISB mapping precise retirement; unsafe pending instruction and unmask rejection; stale/double/wrong-context token checks; actual TLB refill after VMALLE1 versus no implicit flush for DSB/ISB; postcommit malformed/transport uncertainty; all BP32/v1/native regression gates. Independent authored Arm sequences compare guaranteed post-ISB behavior, not an oracle's arbitrary pre-ISB choice. Actual x86 EFI wiring follows frozen ABI/source review.

[Arm's memory-management guide §8.1](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/LearnTheArchitecture-MemoryManagement-101811_0100_00_en.pdf?revision=01a01804-ca64-4e19-a55e-2af56afea5a5) and [barrier explanation](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/memory-access-ordering-part-3---memory-access-ordering-in-the-arm-architecture) ground the separate DSB/TLBI/ISB effects. Mutable tables, live M=1 root changes, SMP, handler execution and general macOS startup require later contracts. Metadata, index, pins, CI and publication remain root-owned.

## Reproduction and observed scope

`python3 tools/probe_dynamic_memory.py --work-dir /tmp/nextcore-dynamic --output /tmp/nextcore-dynamic.json` builds the real C JIT and canonical no_std Rust owner, executes 11 native tests, all 44 service tests and the unchanged 10-test v2 suite, then requires five deliberately broken implementations to fail their semantic expectations. Mutants bypass native memory dispatch, update the CPU bank before COMMIT acknowledgement, misacknowledge ISB's effective snapshot, omit actual TLB invalidation, or ignore guarded ISB bytes. Mutants exist only in the supplied work directory.

The native fixture validates both granules, exact fault PC/ESR/FAR and no retirement at failure, full RAM changes, discontiguous scalar/pair access, actual CPU control-bank preservation on malformed acknowledgements, complete C/Rust record layout, constructor overlap and range checks, known versus uncertain final state, and M=0 callback faults that are impossible in a physical regime. Existing v1/v2 proof tools retain their original expectations; their external mutation wrappers only gain paths to the new canonical child module. `cargo build --release --manifest-path runtime/memory-service/Cargo.toml --target x86_64-unknown-uefi` builds the allocation-free owner for the actual EFI target.

The separately authored Arm enable oracle tests two granules and success/missing-fetch/missing-data outcomes, plus real enable and leaf-omission negative binaries. Its pre-ISB timing is not an expected model choice. An oracle-only pass does not constitute a native-provider comparison, and this module does not claim real macOS boot completion.
