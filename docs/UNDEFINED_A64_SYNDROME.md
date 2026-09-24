# BP36 correction — A64 undefined exception syndrome

The first UBFM implementation deliberately preserved the existing undefined exception syndrome of zero. The independent Arm oracle's sixteen reserved encodings instead capture EC0, IL1, ISS0. Arm DDI0615 A.a, pages233 and287, "ISS encoding for exceptions with an unknown reason", requires IL=1 and ISS[24:0]=RES0 for EC=0. See the [official Arm publication](https://documentation-service.arm.com/static/60d32e78677cf7536a55bad7); the newer [DDI0615 A.d, pages182–183](https://documentation-service.arm.com/static/649ae5b238511951cb799288) retains the rule. This is an existing architectural encoding defect, separate from the new unsigned-bitfield arithmetic.

Root explicitly approves a narrow correction before final BP36 publication. C and Rust canonical undefined/privilege exception branches now emit 0x02000000, retaining their typed internal classifications. This also applies when an allocated but unsupported A64 instruction reaches the existing undefined diagnostic boundary; it does not implement that instruction's normal architectural behavior. In particular, no HVC dispatch to EL2 or valid normal SPTM handoff is implied.

Keep host/provider failures and no-fault results unchanged, including their ESR0 and nonresumable-host-error distinctions. Keep all supported memory/control profiles and C/Rust ABI layouts unchanged. Do not change the incoming saved PSTATE or reinterpret an architecturally invalid FAR as meaningful for an undefined exception.

The first runtime71 manifest10079a12f8aecaa6017d545bdfff051f38098988d42d97601182ee9b7550b95d, original native tests and initial oracle difference are historical. Preserve immutable ISE0d722886 and all BP34 captured evidence unchanged. Rebuild current native/provider/reference/EFI tests against a new final source manifest. Update only current assertions which represent a committed undefined/privilege exception; success, host failure and other exception syndromes must retain their existing requirements.

Final acceptance includes all10706 independent oracle cases, including exact0x02000000 for the sixteen reserved encodings, register/NZCV/SP preservation, and nonretirement/fault-PC/ELR agreement. No syndrome masking is allowed to manufacture a match. Existing semantic mutation tests and real EFI invalid cases must rerun with the corrected sources.

## Corrected evidence

All 10,706 actual Arm observations match both canonical engines after the correction. The 16 reserved instructions match exact ESR 0x02000000, saved PSTATE and the faulting instruction location without retiring the faulting instruction. The prior 16 native and 16 reference ESR-only mismatches remain a separate intentional failing historical receipt, with their original source identities retained.

The current full native, reference and callback suites pass with the corrected expectations. A portable comparison replay also passes, while replacing one captured expected syndrome with zero produces exactly two ESR-only mismatches, one per engine. No captured historical BP34 result was rewritten, and no host/provider failure was reclassified as a guest exception.
