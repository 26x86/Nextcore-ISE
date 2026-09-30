# SMC (Apple System Management Controller IOP) device layer

First-party stub covers **SMC key-table reads** and mailbox command framing for
the qemu-t8030 IOP model. Facts are extracted from `hw/misc/apple_smc.c` at
commit `fd4b0f790903044d90b8a35fcf03758401252063` (GPL reference,
behaviour-only).

## QOM and composite layout

| Field | Value |
| --- | --- |
| QOM type | `apple.smc` (`TYPE_APPLE_SMC_IOP`) |
| Description | Apple SMC IOP (mailbox + SRAM) |
| Child nub | `iop-smc-nub` (`pre-loaded=1`, `running=1`) |

`apple_smc_create()` maps three MMIO regions from DT `reg` tuples:

| Index | Region | qemu name | Access | Stub |
| --- | --- | --- | --- | --- |
| 0 | `reg[0]`..`reg[1]` | AppleA7IOP akfRegMap (mbox) | mailbox u64 | deferred |
| 1 | `reg[2]`..`reg[3]` | AppleASCWrapV2 coreRegisterMap | 8-byte RO/WO → 0 | deferred |
| 2 | DT `sram-addr` | `.sram` | 0x4000 RAM | **apple_smc_v1.c** |

IRQ is passed through from the embedded `AppleMboxState`. SMC key traffic uses
mailbox **endpoint 1** (`kSMCKeyEndpoint`).

## Mailbox key protocol

Inbound message is a packed `key_message` (little-endian, 64-bit mbox word):

| Field | Size | Notes |
| --- | --- | --- |
| `cmd` | 1 | command opcode |
| `ui8TagAndId` | 1 | echoed in response |
| `length` | 1 | write payload / response length hint |
| `payload_length` | 1 | read payload size |
| `key` | 4 | four-char SMC key (`SMC_MAKE_IDENTIFIER`) |

Response is packed `key_response` (64-bit):

| Field | Size | Notes |
| --- | --- | --- |
| `status` | 1 | `kSMCSuccess` (0) or error code |
| `ui8TagAndId` | 1 | echoed |
| `length` | 1 | key data length on success |
| `unk3` | 1 | reserved |
| `response[4]` | 4 | inline data when `info.size ≤ 4` |

Keys larger than four bytes copy through the 16 KiB SRAM window instead of
`response[]`.

### Command opcodes

| Cmd | Name | Stub |
| --- | --- | --- |
| 0x10 | `SMC_READ_KEY` | **apple_smc_v1.c** |
| 0x11 | `SMC_WRITE_KEY` | store / reject per key |
| 0x12 | `SMC_GET_KEY_BY_INDEX` | index walk |
| 0x13 | `SMC_GET_KEY_INFO` | size/type/attr |
| 0x17 | `SMC_GET_SRAM_ADDR` | returns pinned `sram_addr` |
| 0x18 | `SMC_NOTIFICATION` | deferred (mbox notify) |
| 0x20 | `SMC_READ_KEY_PAYLOAD` | same read path as 0x10 |

Common result codes: `kSMCSuccess=0`, `kSMCKeyNotFound=0x84`,
`kSMCKeyNotReadable=0x85`, `kSMCKeyNotWritable=0x86`, `kSMCBadCommand=0x82`.

## Boot-probe keys (realize defaults)

| Key | FourCC | Type | Size | Reset / behaviour | Stub |
| --- | --- | --- | --- | --- | --- |
| `#KEY` | `0x234B4559` | `ui32` | 4 | dynamic key count | **read** |
| `CLKH` | `0x434C4B48` | `{clh` | 8 | `{00,00,70,80,00,01,19,40}` | **read** |
| `RGEN` | `0x5247454E` | `ui8 ` | 1 | `3` | **read** |
| `aDC#` | `0x61444323` | `ui32` | 4 | `0` | **read** |
| `MBSE` | `0x4D425345` | `hex_` | 4 | write-only power verbs | write-only |
| `NESN` | `0x4E45534E` | `hex_` | 4 | write-only | write-only |

`MBSE` write values include `offw`, `off1`, `susp`, `rest`, `slpw`, `panb`,
`pane` (power/panic notify via mbox — deferred in stub).

## Graph-local preOS window (`M1_LOGICAL_SMC_BASE` @ `0xa000`)

`preos/src/m1.rs` and `preos_bridge.c` expose a bounded mailbox shim (not Apple
physical mbox MMIO):

| Offset | Width | Access | Semantics |
| --- | --- | --- | --- |
| `0x000` | 64 | RO | Last `key_response` / GET_SRAM_ADDR word (`MSG_OUT`) |
| `0x008` | 32 | RO | Live `#KEY` count |
| `0x010` | 64 | RO | Pinned placeholder `sram_addr` |
| `0x018` | 64 | WO | Submit 64-bit `key_message` (`MSG_IN`) |

Bridge window id: `VF_M1_MMIO_WINDOW_SMC` (`0xa`). EFI images built with
`VF_EFI_BUILD` omit `apple_smc_v1.c` but expose a **read-only** boot key count
at offset `0x008` via `VF_SMC_BOOT_KEY_COUNT` (`6`, matches seeded table size).
MSG_IN/MSG_OUT/SRAM_ADDR remain `VF_M1_MMIO_UNAVAILABLE` until firmware handoff
needs live mailbox dispatch.

## Implemented stub (`apple_smc_v1.c`)

- Static key table with boot defaults above.
- `vf_smc_handle_msg()` — READ_KEY, GET_KEY_INFO, GET_KEY_BY_INDEX,
  GET_SRAM_ADDR, bounded WRITE_KEY.
- `#KEY` read returns live key count.
- Inline response for keys ≤ 4 bytes; larger keys use caller-provided SRAM
  buffer (16 KiB in struct).
- Unknown commands return `kSMCBadCommand`; missing keys return
  `kSMCKeyNotFound`.

## Deferred

Full AppleMbox endpoint graph, ASCWrapV2 8-byte core map, panic notify
(`panb`/`pane` → `SMC_NOTIFICATION`), MBSE shutdown/suspend side effects,
dynamic key creation on generic WRITE, LGPB/LGPE flag keys, DT `sram-addr`
physical pin, M1/t8103 base, and macOS/iBoot SMC driver traces. The legacy
x86 `isa-applesmc` model (`hw/misc/applesmc.c`) is a separate ISA device and
not this IOP path.
