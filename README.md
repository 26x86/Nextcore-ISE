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
currently supports the tested EL1, 48-bit, TBI-disabled address regime. Native
32/64-bit immediate arithmetic commits ARM NZCV flags, including all conditional
branches, and the boot bridge accepts explicit initial argument registers. Guest MMU
enablement is still a native-JIT boundary. Synthetic PAC execution and GOP
readback establish component behavior; they do not establish macOS boot or Metal
hardware acceleration. Previous release metadata is retained in `repository.json`.


Thread-pointer MRS/MSR executes natively for the documented baseline EL regime;
see `docs/EFI_THREAD_REGISTERS.md`. The reference MMU now decodes TG0/TG1
separately and preserves disabled-walk faults. `python3 tools/probe_mmu_granules.py`
checks independently authored 4 KiB and 16 KiB TTBR1 translations in QEMU's CPU
model. This does not enable native-JIT MMU translation.
