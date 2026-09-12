# Immutable mapped PAC execution contract

The distinct `vf_boot_run_memory_pauth_v2` entry requires an architecture-only
PAC callback after the initial register array. The old v2 entry retains its
unsupported PAC boundary. Successful callbacks commit registers, keys, SP and
PC only after SCTLR, TCR, current EL and reserved fields remain unchanged.
Failure or an attempted immutable-control change commits no callback state.

Immutable v2 native reuse uses the existing provider cache policy: at most 64
1024-byte slots in the caller code allocation, run-local stack metadata, key
PC/fresh instruction/current EL with provider mode fixed to one. Every iteration
checks controls and fetches and validates the instruction. No mapping or data
result is cached. Whole-allocation RW/RX changes occur only without native code
running; callbacks must not reenter or alias/mutate the code allocation.
Slot overflow invalidates every slot before a full-buffer translation retry.
Only successful translation and RX transition publish an entry. Small buffers
bypass caching. NEXTCORE_DISABLE_PROVIDER_CACHE is the uncached control;
NEXTCORE_PROVIDER_CACHE_SLOT_BYTES is the existing test override (minimum 64).
Native execution counts remain execution counts. Dynamic v3 and v1 are unchanged.

The non-TBI, non-TBID, non-MTX baseline QARMA5 profile accepts TnSZ 16 or 17
(48 or 47 address bits). Pointer bit 55 chooses the range; PAC excludes that bit
and address bits below 64-TnSZ. XPAC strips even when address PAC is disabled;
disabled PAC/AUT leaves the pointer unchanged. PACGA uses Xn (ZR permitted),
Xm/SP and APGAKey, returning the MAC upper 32 bits with low 32 bits zero,
independent of address-PAC enable bits. Profile 3 controls are unchanged.

Primary source: [QEMU ae35f033](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/tcg/pauth_helper.c)
(`pauth_addpac`, `pauth_original_ptr`, `pauth_auth`, `HELPER(pacga)`),
[decoder](https://github.com/qemu/qemu/blob/ae35f033b874c627d81d51070187fbf55f0bf1a7/target/arm/tcg/translate-a64.c)
(data-processing two-source case 12), and `target/arm/internals.h` pointer mask.
This is independently authored code and authored-test coverage, not proof of a
private target entry ABI, successful initialization, or macOS boot readiness.

## Oracle feature boundary

The canonical runtime advertises baseline FEAT_PAuth (APA=1), not PAuth2 or
FPAC. QEMU 8.2 `max` advertises APA=5. Its PAuth2 signing XORs the pointer into
the PAC before insertion, so upper-address enabled signatures differ from the
baseline even with identical QARMA5 keys and TCR. For example, the authored
47-bit upper pointer `ffff800000000130` produces baseline
`ffde000000000130` versus QEMU APA=5 `00a1800000000130`; their XOR is exactly
the original pointer under the PAC mask, `ff7f800000000000`.

Raw QEMU observations must retain their feature identification. Lower-address
enabled signatures, stripping, and disabled address-PAC cases can be compared
where the contracts coincide. Upper-address enabled signing/authentication and
failure policy must not be presented as same-feature QEMU proof. This boundary
does not justify changing the advertised runtime to PAuth2 or enabling address
PAC in the fixed mapped profile.
