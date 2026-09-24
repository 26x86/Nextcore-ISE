# Native memory provider v1: translation disabled

This milestone connects a caller-owned Rust RAM service to the native x86
dispatcher. The product remains the EFI ARM64e compatibility JIT. ALU and
control instructions execute as generated x86; instruction fetch and every
supported unsigned-offset scalar and integer-pair transfer use the service.
No native MMU gate is removed, no second walker or persistent code cache is
introduced, and no service reply contains a host pointer. This contract is
the reviewed implementation boundary.

## Callback ABI

All fields have their stated fixed widths and little/native-endian C layout
on the x86_64 calling target. Records align to8. Both languages assert every
size/offset. The ABI version is1. Reserved fields and unknown enum values must
be rejected, not ignored. The callback uses the target's C ABI (Win64 for EFI,
SysV for the Linux proof); the generated entry's existing ms_abi is separate.

`vf_memory_request_v1` is80 bytes:

| Offset | Type | Field |
| --- | --- | --- |
| 0,4,8,12 | u32 each | abi_version, struct_size, operation, flags |
| 16,24,32,40,48,56 | u64 each | pc, address, value0, value1, sctlr, epoch |
| 64,68,72,76 | u32 each | width, count, current_el, reserved |

Operations are1 fetch,2 load,3 store. Flags/reserved/epoch are0 in v1. Width
is1/2/4/8 bytes; count is1 or2. Fetch requires width4/count1/address=pc and zero
values. Pair count2 requires width4/8. Read requests have zero values; stores
supply only the low element bits. Current EL is0/1, SCTLR.M=0 and applicable
data endianness is little. The C dispatcher rejects unsupported HCR/SCR and
execution modes before calling the service. The service independently rejects
unsupported request modes. Epoch0 makes the fixed initial profile explicit;
a future mapped provider needs its own version/transaction contract.

`vf_memory_reply_v1` is80 bytes:

| Offset | Type | Field |
| --- | --- | --- |
| 0,4,8,12 | u32 each | abi_version, struct_size, result, fault |
| 16,24,32,40,48 | u64 each | value0, value1, address, esr, epoch |
| 56 | u64[3] | reserved |

Result0 is success,1 an architectural fault,2 unsupported backing/profile,
3 an invalid host request. Fault0 means none; fault1 is PC alignment and
fault2 is data alignment. No other architectural fault is claimed in this
RAM-only M=0 service. A RAM miss is unsupported backing, not a fabricated
translation/external abort. Successful loads return zero-extended elements;
the C decoder owns signed/32-bit destination semantics. Stores return zero
values. Unused value1 is0. Error values are0, and ESR is0 except for an
architectural fault. Reply address is0 on success and invalid requests; on a
fault/unsupported span it identifies the actual guest failing address. Epoch
echoes the request's0. C validates these invariants before committing state.
For a partial RAM element or an element outside the modeled range, that
address is the first failing element's start (the effective address for a
scalar); for element2 it is address+width modulo64. It never reports a host
pointer or the last in-range byte.

PC alignment requires operation=fetch, address=pc with low2 bits nonzero,
fault1, and ESR0x8a000000 (EC0x22, IL1, ISS0). Data alignment requires a data
operation, naturally unaligned effective address, fault2, and ESR equal to
`((EL==0 ? 0x24 : 0x25)<<26) | (1<<25) | (store ? 64 : 0) | 0x21`.
C independently checks fault/operation/EL/address/ESR agreement. This new
provider fixes PC-alignment IL locally; it does not silently reuse the old
direct path's IL0 synthesis. A separate authored indirect branch to a
misaligned target must confirm that syndrome on the independent Arm oracle.
The primary register definition is Arm DDI0615 A2.1, ESR_EL1 IL and ISS:
[Arm Architecture Reference Manual Supplement, RME](https://documentation-service.arm.com/static/649ae5b238511951cb799288).
It specifies IL1 for PC alignment and a zero ISS for this exception class.
This citation concerns the standard AArch64 syndrome, not an RME capability
claim. `tools/probe_pc_alignment.py` executes a branch with target+2 and checks
the complete ESR, FAR and ELR; its IL0 expectation is a failing control.

The callback signature is:

```c
int32_t callback(void *owner, const vf_memory_request_v1 *request,
                 vf_memory_reply_v1 *reply);
```

Return0 means a complete reply; any other return is a provider host failure.
The owner pointer is trusted host context, never a guest value. Rust lends a
stable `MemoryService<'a>` owning the only mutable RAM slice for the synchronous
C call. The owner cannot move, be reentered, or be concurrently accessed during
that call. No Rust panic/unwind crosses C. The service is a separate no_std
rlib without a panic handler or allocator; EFI supplies its existing runtime.

## New entry and result, old ABI preserved

The new C entry is `vf_boot_run_memory_v1`. It takes guest RAM base/size metadata,
entry/arguments/stack, code buffer and W^X callback, initial x0-x3, existing PAC
callback, existing `vf_boot_options_v2`, memory callback/owner and a result.
It takes no guest-RAM host pointer. Existing boot entry functions, v2 options
64-byte layout, v2 result128-byte layout and enum numbers are unchanged.

```c
int vf_boot_run_memory_v1(uint64_t ram_base, uint64_t ram_size,
    uint64_t entry, uint64_t args, uint64_t stack,
    uint8_t *code, size_t code_bytes, uint64_t budget,
    vf_protect protect, void *protect_opaque,
    const uint64_t initial_x0_x3[4], vf_pauth_step pauth,
    const vf_boot_options_v2 *options,
    vf_memory_callback_v1 memory, void *owner,
    vf_memory_run_result_v1 *result);
```

Host code storage, owner, records and RAM borrow must be mutually valid for
the call, and writable code/result storage must not overlap borrowed RAM. C
cannot validate that last alias condition without a RAM pointer; the Rust
caller must establish it through independent allocations/borrows. The library
package is `nextcore-memory-service`, with a borrowing `MemoryService::new`
constructor and exported C-ABI `vf_memory_service_step` callback.
Constructor/entry reject empty/too-small memory, overflowing base+size and
null, misaligned or overflowing code/result/record ranges before execution.
Pointer lifetime, accessible allocation extent and hidden aliases remain host
preconditions; an integer address check cannot prove allocation validity.
Callback records remain
valid and nonoverlapping for the whole call; the service owns no pointer
into code or result storage. Future mapped execution will reuse the canonical
preos walker through a library dependency; this package contains no copied
walker. Its manifest/source must ship with standalone ISE packages.

`vf_memory_run_result_v1` is192 bytes, alignment8:

| Offset | Type | Field |
| --- | --- | --- |
| 0,4,8,12 | u32 each | abi_version, struct_size, provider_status, reserved0 |
| 16 | existing128-byte struct | execution (`vf_boot_result_v2`) |
| 144,152,160,168,176,184 | u64 each | guest_far, last_address, fetch_requests, data_requests, completed_data_operations, reserved1 |

Provider status0 means none,1 unsupported backing/profile,2 invalid request,
3 invalid reply,4 callback failure. These host/provider failures terminate with
the existing execution status VF_DATA_FAULT but do not create a guest exception
or decoded fault word. Guest alignment faults have provider status0 and their
existing architectural execution status. `guest_far` is the actual saved guest
bank; `last_address` is diagnostic request/failure context, including unsupported
backing, and must not be presented as an architectural FAR. Counters count
issued callbacks and successfully committed data instructions. Existing
compiled-block count remains actual generated-code invocations.

## Execution and commit order

Each provider iteration first applies the old SCTLR.M gate and pending IRQ/FIQ
boundary policy. It then fetches exactly one instruction through the callback,
so speculative future faults cannot precede committed instructions. Translation
uses the true guest PC for ADR/ADRP/branches and one fetched word, never a mapped
host address as the guest PC.

Memory decoders share their valid opcode/register/width definitions with the
existing direct native path. A valid memory instruction emits an internal
dispatcher return without retiring. The C helper checks supported regime and
original-SP SA/SA0 alignment before forming its modulo64 effective address and
request. Callback invocation occurs after generated code returns, so it needs
no new generated Win64 helper-call frame/shadow-space convention. This internal
request status cannot escape as a terminal success or a guest exception.

The Rust service applies MMU-off Device-nGnRnE element alignment regardless of
SCTLR.A, and validates every element's complete RAM span before any read/write.
Pair element2 failure cannot modify element1, destinations or writeback. Loads
gather values before C commits destinations; successful stores are followed by
C writeback, PC+4 and one retirement. NZCV is unchanged. C performs no direct
RAM access in provider mode. Failed callbacks/invalid replies cannot retire;
an invalid provider that modifies RAM contrary to its contract cannot be rolled
back by C, so service tests must prove that side of the transaction directly.

Exact alignment ESR comes from the validated provider reply and is committed
once with guest PC/FAR; the old coarse status synthesizer must not replace it.
PC alignment has no decoded instruction. Other unsupported guest instructions
and CPU/SP faults retain existing precise native semantics. IRQ delivery still
returns at the committed vector; this milestone does not execute handlers or
change ERET behavior.
The result exposes the existing saved FAR bank for every termination, but FAR
has architectural meaning only for exception classes that define it. The
legacy SP-alignment path records the original SP as a runtime diagnostic; this
is not a new architectural guarantee about FAR for SP-alignment exceptions.

## Required evidence

Tests must execute actual C-to-Rust service calls and native ALU/control blocks,
not only compare types. Cover fetch success/failure, every scalar width/opc,
all pair forms, nonzero guest base, SP/ZR, signed loads, two-element preflight,
alignment/unsupported addresses with preserved state, malformed request/reply,
callback failure, absent-provider refusal and SCTLR.M refusal. Independently
verify C/Rust field offsets and existing ABI regressions. Negative controls must
detect a skipped provider call or corrupted readback. EFI integration receives
this exact ABI after approval; private original execution stays separate from
usable macOS boot and guest Metal claims.

## Reproduction and verified scope

Run these commands from a standalone ISE checkout on Linux x86_64. Build and
receipt directories are caller-selected and may be outside the checkout.

```sh
python3 tools/probe_efi_memory_provider.py --work-dir /path/to/provider-build --output /path/to/provider.json
python3 tools/probe_pc_alignment.py --work-dir /path/to/pc-build --output /path/to/pc.json
python3 tools/probe_efi_native_pauth.py --output /path/to/legacy.json
cargo check --manifest-path runtime/memory-service/Cargo.toml --target x86_64-unknown-uefi
```

The native provider suite passes 11 tests, including the canonical v2 layout
receipt. These execute the actual C dispatcher, generated x86 entry and Rust
callback. The standalone service passes five tests. The native suite compares
every field offset of the 80/80/192-byte records and checks the unchanged
64/128-byte v2 records. A separate external copy with provider dispatch disabled
fails the mixed ALU/load/store test as required. The PC oracle passes exact
ESR/FAR/ELR checks; an IL0 expectation fails. Neither negative control mutates
the canonical runtime.

The existing native proof passes all ten programs, including 106,496 scalar,
3,072 pair, 26,544 logical and 4,608 shifted-ALU cases; its Rust reference suite
passes 83 tests on this milestone's base revision. The service builds as a
`no_std` rlib for `x86_64-unknown-uefi`. Package ownership is the existing ISE
repository at `runtime/memory-service`; it has a nested standalone workspace
and adds no eighth module repository. The EFI caller must use this canonical
package/source, not a copied service implementation.

These are host-native SysV callback tests with the existing generated Win64
entry convention. An actual EFI Win64 C-to-Rust callback run is a separate
integration requirement. Mapped execution, MMIO, handler execution and usable
macOS boot remain outside this M=0 service milestone.
