# NVRAM (Apple NVMe namespace-5 env bank) device layer

First-party stub covers the **Apple NVRAM env bank** carried as an NVMe
namespace child. Layout facts are extracted from qemu-t8030
`hw/nvram/apple_nvram.c` and `include/hw/nvram/apple_nvram.h` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference, behaviour-only).

## NVMe namespace identity (investigation values)

| Field | Value | Notes |
| --- | --- | --- |
| `nsid` | **5** | Golden Gate / t8030 reference command line |
| `nstype` | **5** | Apple NVRAM namespace type |
| QOM type | `apple-nvram` (`TYPE_APPLE_NVRAM`) | parent `TYPE_NVME_NS` |
| Max read length | `0x2000` | capped in qemu `apple_nvram_load()` |

Reference machine wiring (not implemented in stub):

```
apple-nvram,drive=nvram,bus=nvme-bus.0,nsid=5,nstype=5
```

## On-disk bank layout

The namespace image begins with a packed **Apple NVRAM header** (`0x20` bytes),
followed by a CHRP partition table starting at offset **`0x20`**.

### AppleNvramPartHdr (`#pragma pack(1)`)

| Offset | Field | Size | Notes |
| --- | --- | --- | --- |
| 0x00 | `chrp.signature` | 1 | `0x5a` on prepared banks |
| 0x01 | `chrp.checksum` | 1 | CHRP rolling checksum |
| 0x02 | `chrp.len` | 2 BE | header length / 16 (`0x0002`) |
| 0x04 | `chrp.name` | 12 | `"nvram"` |
| 0x10 | `adler` | 4 | adler32 over bytes `0x14..end` (init=1) |
| 0x14 | `generation` | 4 | monotonic generation counter |
| 0x18 | padding | 8 | reserved |

### CHRP child partitions (from offset 0x20)

Each partition header is 16 bytes (`ChrpNvramPartHdr`):

| Field | Notes |
| --- | --- |
| `signature` | `0x70` system (`common`), `0x7f` free terminator |
| `checksum` | CHRP checksum over header |
| `len` | big-endian, total partition size / 16 |
| `name` | 12-char partition name |

Partition data follows the 16-byte header. The **`common`** partition
(`sig=0x70`, default data length `0x7f0`) holds null-terminated `name=value\0`
env strings consumed by iBoot / XNU boot-args handoff.

Partitions named **`APL,OSXPani`** (truncated panic log) are skipped during
parse — behaviour preserved from reference.

## Env var API (adapted-port)

| API | Stub |
| --- | --- |
| `vf_nvram_env_get` | **nvram_v1.c** |
| `vf_nvram_env_set` | **nvram_v1.c** (name ≤ 63 chars) |
| `vf_nvram_env_unset` | **nvram_v1.c** |
| `vf_nvram_env_get_uint` | parsed via `strtoul` base 0 |
| `vf_nvram_env_get_bool` | `"true"` or non-zero uint |
| `vf_nvram_get_namespace` | returns nsid=5 / nstype=5 metadata |
| `vf_nvram_parse_bank` | CHRP + adler32 + partition walk |
| `vf_nvram_ingest_bank` | bounded on-disk bank ingest (≤ `0x2000`); fail-closed |
| `vf_nvram_load_from_blk` | clean-room NVMe-ns `getlength`+`pread` @0 (cap `0x2000`); fail-closed if no blk |
| `vf_nvram_save_to_blk` | serialize + `pwrite` @0; fail-closed if no `pwrite` |
| `vf_nvram_blk_attach_path` | research-image open (`O_RDWR`); fail-closed if path missing |
| `vf_nvram_blk_detach` | close attached research-image fd |
| `vf_nvram_boot_args_handoff` | copy `boot-args` to caller buffer (host/test surface) |
| `vf_nvram_serialize` | rebuild bank with free partition |
| Bridge `vf_m1_guest_nvram_ingest_bank` | load bank into graph-local stub |
| Bridge `vf_m1_guest_nvram_load_from_blk` | graph-local load via blk backend ops |
| Bridge `vf_m1_guest_nvram_attach_research_image` | attach path + load (BlockBackend-style) |
| Bridge `vf_m1_guest_nvram_save_to_attached` | pwrite attached research image |
| Bridge `vf_m1_guest_nvram_detach_blk` | detach research image |
| Bridge `vf_m1_guest_nvram_boot_args_handoff` | expose ingested/committed boot-args |

`boot-args` is the primary boot-unblock variable; the stub treats it like any
other env entry inside the `common` partition. Ingest accepts **synthetic /
fixture** banks built via `vf_nvram_serialize` (mark callers with
`SYNTHETIC_FIXTURE`). Do not invent or commit Apple proprietary NVRAM dumps.
`vf_nvram_load_from_blk` mirrors qemu-t8030 `apple_nvram_load` length-cap +
`blk_pread` behavior through a first-party ops table (no QEMU/GLib). Absent or
incomplete blk ops fail closed — callers may fall back to synthetic ingest.
`vf_nvram_blk_attach_path` is the research BlockBackend-style open/attach for a
host bank image (POSIX fd + `pread`/`pwrite`/`getlength`); missing paths fail
closed and must be scaffolded (auto-overcome), never invented. `save_to_blk`
mirrors `apple_nvram_save` `blk_pwrite`. This is **not** guest `apple-nvram`
NVMe namespace-5 QOM wiring (ANS-owned; deferred). Handoff proves host-visible
string availability only — not macOS UART consumption.

Access fails closed on:

- bank shorter than the Apple header
- CHRP checksum mismatch
- adler32 mismatch
- partition length out of range
- oversize env name/value during set
- malformed `common` env blob during parse
- missing blk backend / pread / getlength, non-positive length, or pread error
- missing research image path / open failure / non-positive file length
- save without `pwrite` or pwrite failure

## Implemented vs deferred

| Area | Status |
| --- | --- |
| Namespace-5 identity constants | **nvram_v1.h** |
| Env get/set/unset + boot-args | **nvram_v1.c** |
| Bank parse/serialize (CHRP + adler32) | **nvram_v1.c** (minimal first-party adler32) |
| On-disk bank ingest + boot-args handoff ABI | **nvram_v1.c** + `preos_bridge.c` (synthetic/fixture OK) |
| NVMe namespace block backend ops / `load_from_blk` | **nvram_v1.c** (clean-room; fail-closed if no blk) |
| Research-image BlockBackend-style attach + save | **nvram_v1.c** (`attach_path`/`save_to_blk`; host/test) |
| VenFire QEMU `blk_new_open` adapter | **in series** `0013-…` (0001–0013); durable QEMU SHA `6b03345a…`; host smoke `research-nvram-image` PASS |
| Skip `APL,OSXPanic` partition | **nvram_v1.c** |
| EFI image link (`nvram_v1.c`) | **linked** — `preos_bridge.c` dispatches graph-local NVRAM MMIO; SMC remains unit-only |
| Guest QEMU `apple-nvram` QOM / ANS NVMe ns5 drive | deferred (ANS-owned; do not deepen here) |
| Live macOS/iBoot boot-args UART trace | deferred |
| zlib compression paths | deferred (not used by env bank) |
| Full partition create beyond `common` | deferred |
| DT / ANS composite wiring | deferred (ANS owned elsewhere; do not deepen here) |
| macOS boot verification | **not claimed** (`macos_boot_verified=false`) |

No zlib, QEMU, or GLib imports. Checksum and adler32 are minimal first-party
implementations matching reference behaviour for the env-bank path.
