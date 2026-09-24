# Run-local provider native reuse

Current Status: Single-instruction native reuse applies to physical v1 and
immutable v2 memory-provider runs. It does not cache fetches, data, address
translations or replies. See [mapped PAC v2](MAPPED_PAUTH_V2.md) for the v2
control and callback contract.
Target State: Preserve complete architectural/provider outcomes while reducing
actual code generation and permission transitions on repeated instructions.

## Contract before implementation

Each run owns a fixed 64-entry stack metadata array and the caller's existing
code allocation. Each slot has 1024 bytes; usable count is capacity/1024 capped
at 64. Smaller allocations use the unchanged uncached translation path. Entries
are found by complete guest PC, freshly fetched instruction word and current EL.
Provider mode and one-instruction limit are fixed, never shared with direct RAM,
another provider run, dynamic v3 or another invocation. Round-robin replacement needs no allocation.

The current translator embeds guest PC and EL-dependent boundaries; operands,
flags and thread values are read from the live CPU. Provider memory instructions
emit dispatch stubs; the existing slow path obtains current registers and sends
fresh provider requests. Epoch zero is not a code-content generation. A fresh
fetch and complete reply validation precede every lookup, preserving self-modified
code, faults and provider effects. Interrupt polling and all existing status,
retirement, counter and slow-path processing are retained.

Misses invalidate the destination slot before writing. The full caller allocation
transitions RW/NX then RO/X; this maintains page granularity even with four slots
per host page. The synchronous owner contract forbids reentry, concurrent code
execution/mutation and code/RAM/metadata aliasing. A successful RX transition is
required before publishing metadata. Hits execute only previously sealed bytes.
Relative native branches and CPU-relative operands remain within their slot.

If translation exceeds one slot, all metadata is invalidated and the existing
full-buffer translation is retried. Its status and code capacity remain decisive;
this fallback is not published. Failed translation/protection ends the run and
publishes nothing. The caller's final protection restore remains unchanged.
Code storage has no lifetime beyond its caller, and metadata never survives a run.

`NEXTCORE_DISABLE_PROVIDER_CACHE` compiles the uncached comparison path.
`NEXTCORE_PROVIDER_CACHE_SLOT_BYTES` defaults to 1024; test builds may override it
(minimum 64) to exercise genuine slot overflow and full-buffer fallback. This
compile-time test control introduces no runtime ABI or CPU layout changes.

`compiled_blocks` retains its existing native-entry execution meaning, including
dispatch stubs. Independent tests must count actual protection calls and compare
CPU, RAM, requests, faults, self-modification, eviction and small-buffer behavior.
Only a measured same-input EFI replay can establish original-path speed changes;
test reuse counts cannot establish macOS boot progress or physical display.

## Initial v1 validation (2026-09-12)

On 2026-09-12 the existing `probe_efi_memory_provider.py` passed twenty generated
native/C-to-Rust provider tests and forty-four canonical memory-service tests;
its compiled provider-bypass negative control was detected and source hashes
were unchanged. Both cached and disabled `jit.c` compiled for the freestanding
Win64 UEFI target with Clang 18.1.3, `-O2 -Wall -Wextra -Werror`. A separate
read-only review found no required changes in key specialization, slot bounds,
fallback, permission publication or callback ownership. Independent cache
differential tests and root's actual EFI replay are separate acceptance evidence.

The independently authored `runtime/tests/verify_provider_cache.py` compares
thirteen cases across production cache, disabled cache and forced 64-byte slots.
CPU/result/RAM and ordered request/reply/callback-result bytes agree. Real
`mprotect` transitions are exclusively RW or RX: the loop falls from 512 calls
to 6, and self-modifying code from 512 to 8. A 65-PC ADR sequence plus its branch
forces eviction and retains 528 calls; reuse is not assumed for that working set.
Full-width BFM exercises genuine small-slot overflow: forced fallback retains
512 calls versus 4 for production slots. EL specialization, fresh-fetch failures,
malformed replies, data faults, undefined instructions, small caller buffers and
RW/RX failures retain their uncached outcomes. These are authored native results,
not original-input timing or operating-system progress.

The final independent proof also rejects five successfully compiled semantic
mutants: omitted PC, instruction-word or EL keys; reused overwritten entries;
and skipped hit execution counting. All tested source hashes remain unchanged.
