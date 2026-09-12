# Nextcore-ISE

Instruction policy and the freestanding ARM64e-to-x86_64 JIT runtime used by
Nextcore-EFI. The physical execution target is x86 EFI. WSL2/Linux is a build
and test environment; the product runtime has no host operating-system dependency.

`src/` contains the Cargo instruction policy library. `runtime/` owns the C native
JIT, boot bridge, EFI ABI and GOP transfer helpers. `runtime/preos/` contains the
no_std Rust reference core and software QARMA5/PAC provider. `devices/` contains the
bounded interrupt-controller model. EFI locates these sources through the public
`EFI_RUNTIME_DIR` build helper and compiles them into its own EFI image.

```sh
cargo test --all-targets
cargo test --manifest-path runtime/preos/Cargo.toml
python3 tools/probe_efi_native_pauth.py
clang -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined \
  runtime/gop_scanout.c runtime/test_gop_scanout.c -o /tmp/gop-test
/tmp/gop-test
```

Native execution is tested on Linux x86_64 with clang and rustc. The PAC provider
supports tested EL1, 47/48-bit, TBI-disabled address regimes. Native
32/64-bit immediate arithmetic commits ARM NZCV flags, including all conditional
branches, and the boot bridge accepts explicit initial argument registers. The native immutable stage-1 provider supports the documented mapped profiles. Synthetic PAC execution and GOP
readback establish component behavior; they do not establish macOS boot or Metal
hardware acceleration. Previous release metadata is retained in `repository.json`.


Thread-pointer MRS/MSR executes natively for the documented baseline EL regime;
see `docs/EFI_THREAD_REGISTERS.md`. The reference MMU now decodes TG0/TG1
separately and preserves disabled-walk faults. `python3 tools/probe_mmu_granules.py`
checks independently authored 4 KiB and 16 KiB TTBR1 translations in QEMU's CPU
model. See [stage-1 memory service](docs/NATIVE_MMU_PROVIDER_V2.md) for the separate native provider contract.

## Current bounded CPU validation

Exact [ISAR2 scalar reads](docs/ISAR2_SCALAR_PROFILE.md) retain live EL1 and
HCR/SCR gates. The [PAC address-selection contract](docs/MAPPED_PAUTH_V2.md)
separates signing bit 63 from authentication/stripping bit 55. Its adapted APA1
QEMU oracle is explicitly distinct from identical-state hardware execution.
The fixed mapped profile keeps address PAC disabled; active XPAC and cache
regressions do not prove enabled mapped signing or a complete CPU feature model.

```sh
python3 runtime/tests/verify_isar2_native.py --output /tmp/isar2-native
```

The PAC oracle runner requires an explicitly supplied, separately built test
QEMU, its build receipt and the frozen pre-fix source for its negative control;
see `python3 runtime/tests/verify_pac_address_selection.py --help`.
These authored component checks do not establish physical or macOS boot.
