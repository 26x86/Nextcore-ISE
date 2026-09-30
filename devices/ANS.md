# ANS (Apple NVMe) device layer

First-party stub covers the **vendor MMIO window** inside the qemu-t8030 ANS2
composite block. Register facts are extracted from qemu-t8030
`hw/block/apple_ans.c` at commit
`fd4b0f790903044d90b8a35fcf03758401252063` and cross-checked against Inferno
`hw/block/apple-silicon/ans.c` (GPL reference, behaviour-only). NVMe CAP/VS
defaults match qemu-t8030 `hw/nvme/ctrl.c` `nvme_init_ctrl` (same revision).
INTMS/INTMC share one interrupt mask (set OR / clear AND~). Identify CQ
completion sets `irq_status` bit0; `vf_ans_irq_check` reports pending when
unmasked (INTMS bit0 fail-closed masks). When `ans_pci_v1` is bound, that
pending mirrors into PCI INTx via `vf_ans_pci_set_irq` / `vf_ans_pci_irq_pending`
(pin level), into `vf_ans_pci_msi_pending` when MSI Enable is set, or into
`vf_ans_pci_msix_pending` when MSI-X Enable is set (MSI-X > MSI > INTx).
Optional `vf_ans_pci_bind_msi_message` may deliver prepared Message
Address/Data through a host/test DMA callback when MSI pending asserts
(fail-closed if unbound; **message write ≠ AIC by itself**). Optional
`vf_ans_pci_bind_aic` may mirror INTx|MSI|MSI-X delivery pending onto graph
STORAGE AIC line **1** (fail-closed unbound; **≠ STORAGE window 0x4
ownership / Apple DT IRQ / guest acceptance**). `preos_bridge` auto-binds
that sink on `vf_m1_guest_mmio_reset`.
CC.EN → CSTS.RDY honesty follows the NVMe controller enable contract without
MSI message delivery or full command DMA. SQ/CQ doorbells (admin + I/O, DSTRD=0) store/read
low-16 values; admin SQTDBL advance with an armed Identify CNS=CTRL|NS simplified
SQE drives host/test Identify DMA fill; when a guest-visible ASQ page is bound
(`vf_ans_bind_admin_asq`), the path fetches the 64-byte SQE from ASQ[old Tail]
and fail-closes on SGL (`PSDT!=0`) or zero PRP1 (CNS=NS also requires SQE
NSID=`1`); when a guest-visible Identify
PRP1 page is also bound (`vf_ans_bind_identify_prp1`), page-aligned PRP1
requires `PRP2==0` and on DMA success copies the 4096-byte Identify payload
into that host stand-in; when PRP2 is also bound (`vf_ans_bind_identify_prp2`),
unaligned PRP1 plus page-aligned non-zero PRP2 splits the payload across the
PRP1 remainder and PRP2 second data page (PRP list entries are never walked;
unaligned PRP2 fail-closed); on DMA success a simplified admin CQE is written into
the armed cq_buf and, when a guest-visible ACQ page is bound
(`vf_ans_bind_admin_acq`), also into ACQ[CQ Tail] (fail-closed on short page /
ACQ base both zero); CQ Tail advances (CQHDBL remains host drain; irq_check
may sync bound PCI INTx; no MSI message / AIC / PRP list). Identify CNS=CTRL metadata
(SQES/CQES/MDTS/NN/ver) follows the qemu-t8030 `is-apple-ans` / `apple_ans`
path; Identify CNS=NS is a zero-capacity stub NSID=`1` (no block backend);
host/test DMA fill places that metadata into a 4096-byte Identify buffer.
Unmodeled vendor offsets use a sparse `vendor_reg[]` store/read table;
unmodeled NVMe alias gaps remain RO-zero.

## DT / composite layout (reference-only)

`apple_ans_create()` maps five `reg` tuples from the device tree:

| Index | Region | qemu name | Access | Stub |
| --- | --- | --- | --- | --- |
| 0 | `reg[0]`..`reg[1]` | AppleA7IOP akfRegMap (mbox) | mailbox | **ans_mbox_v1** (see `ANS_MBOX.md`) |
| 1 | `reg[2]`..`reg[3]` | AppleASCWrapV2 coreRegisterMap | 8-byte RO/WO → 0 | **ascwrap_v1** (see `ASCWRAP.md`) |
| 2 | `reg[4]`..`reg[5]` | AppleA7IOP autoBootRegMap | native RO/WO → 0 | **ans_autoboot_v1** (see `ANS_AUTOBOOT.md`) |
| 3 | PCI host | ans_pci_mmio / ans_pci_ioport | PCIe | **ans_pci_v1** (see `ANS_PCI.md`) |
| 4 | `reg[6]`..`reg[7]` | vendor MMIO + NVMe alias | u32 | **ans_v1.c** |

The vendor window (`reg[7]` bytes) contains:

- **0x0000..0x11FF** — alias of standard NVMe controller BAR (`nvme.iomem`)
- **0x1200..0x5FFFF** — Apple vendor registers (`vendor_reg[]`, 0x60000 total)

Child node `iop-ans-nub` receives DT props `pre-loaded=1`, `running=1`.

## Implemented vendor MMIO (adapted-port)

| Offset | Name | Read | Write | Notes |
| --- | --- | --- | --- | --- |
| 0x0000 | CAP[31:0] | `0x0f0107ff` **sticky RO** | accept | MQES=`0x7ff`, CQR, TO=`0xf` |
| 0x0004 | CAP[63:32] | `0x00401820` **sticky RO** | accept | CSS=`NVM\|CSI\|ADMIN_ONLY`, MPSMAX=4; CMBS/PMRS=0 |
| 0x0008 | VS | `0x00010400` **sticky RO** | accept | NVMe 1.4 (`NVME_SPEC_VER`) |
| 0x000C | INTMS | shared `intm` | OR into `intm` | Interrupt Mask Set; masks irq_check |
| 0x0010 | INTMC | shared `intm` | AND~ into `intm` | Interrupt Mask Clear; alias of INTMS |
| 0x0014 | CC | stored | store | bit0 `CC.EN`; other CC fields stored |
| 0x001C | CSTS | derived | accept/ignore | `CSTS.RDY` tracks `CC.EN`; no SHN/CFS |
| 0x0024 | AQA | stored | store | Admin Queue Attributes |
| 0x0028 | ASQ[31:0] | stored | store | Admin SQ base low |
| 0x002C | ASQ[63:32] | stored | store | Admin SQ base high |
| 0x0030 | ACQ[31:0] | stored | store | Admin CQ base low |
| 0x0034 | ACQ[63:32] | stored | store | Admin CQ base high |
| 0x1000 | SQTDBL (admin) | stored low-16 | store low-16; may one-shot armed Identify (+optional ASQ/PRP1/PRP2/ACQ) | DSTRD=0; qid0 SQ; no MSI/AIC/PRP list |
| 0x1004 | CQHDBL (admin) | stored low-16 | store low-16; host CQ drain | DSTRD=0; qid0 CQ; pending=(tail−CQH); may clear irq |
| 0x1008..0x11FC | SQTDBL/CQHDBL (I/O) | stored low-16 | store low-16 | qid1..63; stride 4; no DMA |
| 0x1210 | MAX_PEND_CMDS | `(64<<16)\|64` **sticky** | accept | capacity probe; write-immune read |
| 0x1300 | BOOT_STATUS | `0xde71ce55` **sticky** | accept | **boot-unblock magic**; write-immune |
| 0x1304 | MODESEL | stored | store | R/W |
| 0x1308 | BASE_CMD_ID | `0x6000` **sticky** | store | write accepted; read always `0x6000` |
| 0x24908 | LINEAR_SQ_CTRL | stored | store | bit0 `LINEAR_SQ_CTRL_EN` |
| other vendor u32 (≥0x1200) | sparse `vendor_reg` | stored | store | bounded sparse table (`VF_ANS_VENDOR_SPARSE_CAP`); full → fail |
| other alias u32 (<0x1200) | — | 0 | accept/ignore | **RO-zero** unmodeled NVMe alias gaps |

Access is exactly aligned little-endian **u32**. Offsets ≥ `0x60000` or
non-u32 widths fail closed.

**Status honesty (2026-09-18):** Inferno/t8030 vendor reads override
`MAX_PEND_CMDS`, `BOOT_STATUS`, and `BASE_CMD_ID` with fixed constants after
any write (writes to sticky vendor offs also land in sparse backing). NVMe
alias CAP/VS match t8030 `nvme_init_ctrl` RO defaults and stay write-immune.
INTMS (`0x000C`) ORs into a shared mask; INTMC (`0x0010`) clears bits; both
read the same mask (qemu-t8030 `bar.intms`/`intmc` mirror). Identify CQ
completion sets `irq_status` bit0; `vf_ans_irq_check` returns pending when
`(irq_status & ~intm & bit0) != 0` — INTMS bit0 fail-closed masks without
clearing status; bound PCI mirrors that pending into INTx
(`vf_ans_pci_irq_pending`) without MSI message delivery or AIC. CC stores; `CSTS.RDY` follows `CC.EN`;
AQA/ASQ/ACQ store. Doorbells `0x1000..0x11FF` (DSTRD=0: per-qid SQ then CQ)
store low-16 values; admin SQTDBL advance with an armed Identify CNS=CTRL|NS
simplified SQE one-shots the host/test DMA fill; when `vf_ans_bind_admin_asq`
binds a guest-visible ASQ page, the path fetches SQE from ASQ[old Tail] and
fail-closes on SGL / zero PRP1 / shape mismatch / ASQ base both zero / (CNS=NS)
SQE NSID != stub `1`; when
`vf_ans_bind_identify_prp1` also binds an Identify data page, page-aligned
PRP1 requires `PRP2==0` and on DMA success copies the Identify payload into
that host stand-in; when `vf_ans_bind_identify_prp2` also binds a second data
page, unaligned PRP1 + page-aligned non-zero PRP2 splits across PRP1 remainder
+ PRP2 (PRP list never walked; unaligned PRP2 fail-closed); when
`vf_ans_bind_admin_acq` binds a
guest-visible ACQ page, the path requires a writable slot at ACQ[CQ Tail]
(fail-closed on short page / ACQ base both zero) before DMA and, on success,
copies the simplified 16-byte admin CQE into that slot as well as cq_buf; CQ
Tail advances and `irq_status` bit0 is set (CQHDBL is host drain via
`vf_ans_admin_cq_pending`; drain clears `irq_status` and bound PCI INTx);
`CC.EN` clear resets all
doorbells, `intm`, `irq_status`, CQ Tail/phase, and any armed Identify to 0 —
ASQ/ACQ/PRP1/PRP2 binds persist; **no** MSI message / AIC or PRP list walk; sparse vendor slots
are **not** cleared. Unmodeled vendor
offsets (`≥0x1200`, outside named regs) store/read via sparse `vendor_reg[]`
(capacity 48; table-full fails closed). Unmodeled NVMe alias gaps stay RO-zero.
`is_apple_ans` defaults true; `vf_ans_identify_sqes` returns `0x76` (Apple) or
`0x66` (std); `vf_ans_identify_cns_ctrl` fills CQES/`mdts`/NN/ver — host/test
metadata; `vf_ans_identify_cns_ctrl_dma` zeroes a 4096-byte buffer and writes
those fields at NVMe `id_ctrl` offsets; `vf_ans_identify_cns_ns` /
`vf_ans_identify_cns_ns_dma` fill zero-capacity stub NSID=`1` `id_ns`
(fail-closed without `CC.EN`; when PCI is bound via `vf_ans_bind_pci`, also
without BusMaster; when mbox is bound via `vf_ans_bind_mbox`, also without
STARTED; when ASCWrap is bound via `vf_ans_bind_ascwrap`, also without READY;
when autoboot is bound via `vf_ans_bind_autoboot`, also without ARMED).
`vf_ans_arm_admin_identify` arms one Identify CNS=CTRL|NS submit plus a ≥16-byte
CQ buffer; admin SQTDBL advance drives optional ASQ fetch + optional PRP1/PRP2
translate + optional ACQ CQE write + DMA fill and, on success, CQ completion +
irq_check (+ bound PCI INTx sync; still no MSI message / AIC / PRP list). NextCore `ans_v1` matches that
contract; no MSI message / DT IRQ / AIC / full command-path claims. ASCWrapV2 core map is a
separate stub (`ascwrap_v1` / `ASCWRAP.md`): present/bootstrap + 8-byte RO/WO → 0
+ optional mbox STARTED→READY bind. ANS mailbox akfRegMap is a separate stub
(`ans_mbox_v1` / `ANS_MBOX.md`): present/bootstrap + A2I EMPTY + I2A bootstrap
endpoint handshake + start/wakeup `FLAG_STARTED` (+ optional PCI Memory|BusMaster
latch; no IRQ/ACTIVE/DMA/AIC). IOP autoBootRegMap is a separate stub
(`ans_autoboot_v1` / `ANS_AUTOBOOT.md`): present/bootstrap + RO/WO → 0 +
optional ASCWrap READY→ARMED bind (bound map WO fail-closed without READY;
no firmware load). PCIe host containers are a separate stub (`ans_pci_v1` /
`ANS_PCI.md`): present/bootstrap + ioport/MMCFG size honesty + COMMAND
Memory|BusMaster store/read + INTx pending latch + MSI capability/pending +
bound MSI message write (fail-closed unbound; message write ≠ AIC alone) +
bound AIC STORAGE line pending mirror (fail-closed unbound; ≠ window 0x4 /
Apple DT IRQ; no ECAM / command DMA).

## Identify CNS=CTRL|NS metadata + DMA fill (`is-apple-ans`)

Host/test surface. Mirrors qemu-t8030 `hw/nvme/ctrl.c` `nvme_init_ctrl` plus
`hw/block/apple_ans.c` (`mdts=8`) when `params.is_apple_ans` is set. CNS=`0x01`
(Identify Controller) and CNS=`0x00` (Namespace, zero-capacity stub NSID=`1`)
have metadata and DMA fill; CNS lists and other CNS values remain deferred.
Admin SQ doorbell may drive the DMA fill when a simplified Identify
CNS=CTRL|NS SQE is armed; optional bound ASQ fetch + PRP/SGL honesty,
optional PRP1/PRP2 GPA→buffer translate, and optional bound ACQ CQE write
apply around DMA; on DMA success a simplified admin CQE is posted
(cq_buf + ACQ when bound) and irq_check may assert (+ bound PCI INTx or MSI
pending when MSI Enable is set; optional bound MSI message write ≠ AIC alone;
optional bound AIC STORAGE-line mirror; no PRP list walk / block backend).

| API / constant | Value | Notes |
| --- | --- | --- |
| `VF_ANS_ID_CNS_CTRL` | `0x01` | Identify Controller CNS |
| `VF_ANS_ID_CNS_NS` | `0x00` | Namespace — zero-capacity stub NSID=1 |
| `VF_ANS_ID_NS_STUB_NSID` | `1` | Only active NSID for CNS=NS |
| `VF_ANS_OPC_IDENTIFY` | `0x06` | NVMe admin Identify opcode |
| `VF_ANS_ID_CTRL_SIZE` | `4096` | NVMe Identify Controller payload |
| `VF_ANS_ID_NS_SIZE` | `4096` | NVMe Identify Namespace payload |
| `VF_ANS_SQE_SIZE` | `64` | NVMe Submission Queue Entry |
| `VF_ANS_SQE_OFF_NSID` | `4` | SQE NSID (LE32); CNS=NS requires stub |
| `VF_ANS_ID_OFF_MDTS` | `77` | `id_ctrl.mdts` |
| `VF_ANS_ID_OFF_VER` | `80` | `id_ctrl.ver` (LE32) |
| `VF_ANS_ID_OFF_CNTRLTYPE` | `111` | `id_ctrl.cntrltype` |
| `VF_ANS_ID_OFF_SQES` | `512` | `id_ctrl.sqes` |
| `VF_ANS_ID_OFF_CQES` | `513` | `id_ctrl.cqes` |
| `VF_ANS_ID_OFF_NN` | `516` | `id_ctrl.nn` (LE32) |
| `VF_ANS_ID_NS_OFF_NSZE` | `0` | `id_ns.nsze` (LE64; stub 0) |
| `VF_ANS_ID_NS_OFF_NCAP` | `8` | `id_ns.ncap` (LE64; stub 0) |
| `VF_ANS_ID_NS_OFF_NUSE` | `16` | `id_ns.nuse` (LE64; stub 0) |
| `VF_ANS_ID_NS_OFF_NLBAF` | `25` | `id_ns.nlbaf` (stub 0) |
| `VF_ANS_ID_NS_OFF_FLBAS` | `26` | `id_ns.flbas` (stub 0) |
| `is_apple_ans` (default) | 1 | ANS stub defaults to Apple Identify |
| `VF_ANS_ID_SQES_APPLE` | `(0x7<<4)\|0x6` = `0x76` | max IOSQES=7, required=6 |
| `VF_ANS_ID_SQES_STD` | `(0x6<<4)\|0x6` = `0x66` | both 6 (non-Apple path) |
| `VF_ANS_ID_CQES` | `(0x4<<4)\|0x4` = `0x44` | fixed in `nvme_init_ctrl` |
| `VF_ANS_ID_MDTS_APPLE` | `8` | `apple_ans.c` override |
| `VF_ANS_ID_MDTS_STD` | `7` | NVMe prop default |
| `VF_ANS_ID_NN` | `16` | `NVME_MAX_NAMESPACES` (max; only NSID=1 stub) |
| `VF_ANS_ID_CNTRLTYPE` | `0x1` | controller type |
| `VF_ANS_ID_VER` | `0x00010400` | same as VS (NVMe 1.4) |
| `VF_ANS_CQE_SIZE` | `16` | NVMe Completion Queue Entry |
| `vf_ans_set_is_apple_ans` | store 0/1 | fail-closed on NULL |
| `vf_ans_get_is_apple_ans` | 0/1 | fail-closed on NULL → -1 |
| `vf_ans_bind_pci` | optional | NULL unbinds; DMA gate when linked |
| `vf_ans_bind_mbox` | optional | NULL unbinds; Identify DMA requires STARTED |
| `vf_ans_bind_ascwrap` | optional | NULL unbinds; Identify DMA requires READY |
| `vf_ans_bind_autoboot` | optional | NULL unbinds; Identify DMA requires ARMED |
| `vf_ans_bind_admin_asq` | optional | guest-visible ASQ page; NULL unbinds |
| `vf_ans_admin_asq_bound` | 0 / 1 | host/test probe |
| `vf_ans_bind_admin_acq` | optional | guest-visible ACQ page; NULL unbinds |
| `vf_ans_admin_acq_bound` | 0 / 1 | host/test probe |
| `vf_ans_bind_identify_prp1` | optional | Identify data page (PRP1 GPA stand-in) |
| `vf_ans_identify_prp1_bound` | 0 / 1 | host/test probe |
| `vf_ans_bind_identify_prp2` | optional | Identify 2nd data page (PRP2 GPA stand-in; never list) |
| `vf_ans_identify_prp2_bound` | 0 / 1 | host/test probe |
| `vf_ans_identify_sqes` | fills `uint8_t` | no opcode, PRP, or DMA |
| `vf_ans_identify_cqes` | fills `uint8_t` | always `0x44` |
| `vf_ans_identify_cns_supported` | 0 / -1 | 0 for CNS=CTRL or CNS=NS |
| `vf_ans_identify_cns_ctrl` | fills `vf_ans_id_ctrl_meta` | SQES/CQES/MDTS/NN/ver |
| `vf_ans_identify_cns_ctrl_dma` | fills 4096-byte buf | metadata at NVMe offsets |
| `vf_ans_identify_cns_ns` | fills `vf_ans_id_ns_meta` | zero-cap stub NSID=1 |
| `vf_ans_identify_cns_ns_dma` | fills 4096-byte buf | zero `id_ns` stub |
| `vf_ans_arm_admin_identify` | arm SQE + CQ buf | Identify + CNS=CTRL\|NS + id + cq |
| `vf_ans_admin_identify_armed` | 0 / 1 | host/test probe |
| `vf_ans_disarm_admin_identify` | clear arm | no DMA / CQ |
| `vf_ans_admin_cq_tail` | low-16 | controller CQ Tail |
| `vf_ans_admin_cq_pending` | low-16 | `(tail − CQHDBL) & 0xffff` |
| `vf_ans_irq_check` | 0 / 1 | `(irq_status & ~intm & bit0)`; may sync PCI INTx |

### DMA fill honesty

1. CNS=CTRL: zeroes `VF_ANS_ID_CTRL_SIZE` bytes, then writes known metadata
   fields only (other `id_ctrl` bytes stay 0 — no invented VID/SN/MN).
   CNS=NS: zeroes `VF_ANS_ID_NS_SIZE` bytes and writes zero-capacity stub
   fields (`nsze`/`ncap`/`nuse`/`nlbaf`/`flbas` = 0 — no invented disk / LBA
   formats / block backend).
2. Requires `CC.EN` (and thus `CSTS.RDY`).
3. When `vf_ans_bind_pci` links `ans_pci_v1`, also requires BusMaster; clearing
   BusMaster fails closed. Unbound skips the BusMaster gate.
4. When `vf_ans_bind_mbox` links `ans_mbox_v1`, also requires
   `vf_ans_mbox_started`; clearing STARTED (or never starting) fails closed.
   Unbound skips the mbox gate.
5. When `vf_ans_bind_ascwrap` links `ascwrap_v1`, also requires
   `vf_ascwrap_ready` (READY mirrors bound mbox STARTED via
   `vf_ascwrap_bind_mbox`); clearing READY fails closed. Unbound skips the
   ASCWrap gate.
6. When `vf_ans_bind_autoboot` links `ans_autoboot_v1`, also requires
   `vf_ans_autoboot_armed` (ARMED mirrors bound ASCWrap READY via
   `vf_ans_autoboot_bind_ascwrap`; READY←mbox STARTED); clearing ARMED fails
   closed. Unbound skips the autoboot gate.
7. Wrong CNS, short buffer, or NULL args fail closed.
8. **No** MSI message / AIC or PRP list walk (doorbell path may post a simplified admin
   CQE + irq_check + bound PCI INTx; optional ASQ fetch + PRP1 non-zero / SGL fail-closed
   honesty; optional page-aligned PRP1 or unaligned PRP1+PRP2 GPA→buffer
   translate when bound; optional ACQ CQE write when bound).

### Admin SQ doorbell → Identify submit + CQ completion

1. `vf_ans_arm_admin_identify(opcode=Identify, cns=CTRL|NS, buf, len≥4096,
   cq_buf, cq_len≥16)` arms one host/test simplified SQE + CQ destination.
   Wrong opcode/CNS/short buf fail closed.
2. Optional `vf_ans_bind_admin_asq(mem, len≥64)` binds a guest-visible ASQ
   page. When bound, SQTDBL advance fetches SQE from ASQ[old Tail] and
   fail-closes on `PSDT!=0` (SGL), `PRP1==0`, opcode/CNS mismatch, short
   page, ASQ base registers both zero, or (CNS=NS) SQE NSID != stub `1`.
   Unbound keeps arm-only path.
3. Optional `vf_ans_bind_identify_prp1(mem, len≥4096)` binds an Identify data
   page (host stand-in for PRP1 GPA). When bound with ASQ, page-aligned PRP1
   requires `PRP2==0` and on DMA success copies the full 4096-byte payload into
   `mem`. Optional `vf_ans_bind_identify_prp2(mem, len≥4096)` binds a second
   data page; unaligned PRP1 + page-aligned non-zero PRP2 splits across PRP1
   remainder + PRP2 (PRP list never walked; unaligned PRP2 / missing PRP2 bind
   / aligned+PRP2!=0 fail-closed). Unbound PRP1 keeps armed id_buf-only DMA
   (PRP1 non-zero honesty only).
4. Optional `vf_ans_bind_admin_acq(mem, len≥16)` binds a guest-visible ACQ
   page. When bound, SQTDBL advance requires a writable slot at
   ACQ[`admin_cq_tail`] (fail-closed on short page or ACQ base both zero)
   before DMA; on success the CQE is also copied into that slot. Unbound
   keeps cq_buf-only legacy path.
5. Writing admin SQTDBL (`0x1000`) with a **new** low-16 value (advance) one-shots
   CNS-matched Identify DMA (`vf_ans_identify_cns_ctrl_dma` or
   `vf_ans_identify_cns_ns_dma`) into the armed id buffer (+ PRP1/PRP2 pages
   when bound), then clears the arm.
6. On **DMA success**: writes a simplified 16-byte admin CQE (DW0/DW1=0;
   SQHD=current admin SQTDBL; SQID/CID=0; Status=0; Phase Tag toggles from 1)
   into `cq_buf` (+ ACQ[Tail] when bound), advances `admin_cq_tail`, and sets
   `irq_status` bit0. CQHDBL remains the host drain pointer
   (`vf_ans_admin_cq_pending = (tail − CQH) & 0xffff`). `vf_ans_irq_check` is
   1 while bit0 is set and INTMS bit0 is clear. When `ans_pci_v1` is bound,
   the same pending mirrors into `vf_ans_pci_irq_pending` (INTx latch),
   `vf_ans_pci_msi_pending` when MSI Enable is set, or
   `vf_ans_pci_msix_pending` when MSI-X Enable is set (qemu `nvme_irq_check` →
   `pci_irq_assert` / `apple_ans_set_irq` / MSI / MSI-X pending honesty;
   MSI-X > MSI > INTx). Optional bound MSI message write and optional bound
   AIC STORAGE-line mirror are separate binds (fail-closed unbound).
   **No PRP list walk / real GPA walk of ASQ/ACQ bases.**
7. On DMA, ASQ-honesty, PRP-honesty, or ACQ-honesty failure: id/CQ/ACQ/PRP
   buffers untouched; CQ Tail / irq_status unchanged (arm still cleared).
8. Same-value rewrite, admin CQHDBL, and I/O doorbells do **not** consume the arm.
   Admin CQHDBL drain that catches Tail clears `irq_status` bit0 (+ PCI INTx /
   MSI / MSI-X pending / bound AIC deassert).
9. Doorbell store always succeeds; DMA fill fail-closes without `CC.EN` / bound
   BusMaster / bound mbox STARTED / bound ASCWrap READY / bound autoboot ARMED.
10. `CC.EN` clear and `vf_ans_disarm_admin_identify` clear the arm without fill;
   `CC.EN` clear also resets CQ Tail/phase / `irq_status` (ASQ/ACQ/PRP1/PRP2
   binds persist; bound PCI INTx / MSI / MSI-X pending deasserted; bound AIC
   syncs).
11. INTMS bit0 masks `vf_ans_irq_check` and bound PCI INTx / MSI / MSI-X pending
   fail-closed without clearing `irq_status`; INTMC bit0 unmask re-exposes
   while CQ still pending (bound AIC re-syncs).
12. **No** PRP list walk or real GPA translate of ASQ/ACQ bases (MSI message
    write and AIC STORAGE-line mirror require explicit binds).

Namespaces beyond stub NSID=`1`, other CNS lists, PRP list / SGL walk, and dense
`vendor_reg[0x60000/4]` remain deferred. Optional MSI message write and
optional AIC STORAGE-line bind are in `ans_pci_v1`. PCI INTx + MSI pending
latches are in `ans_pci_v1`. COMMAND
Memory|BusMaster store/read is in `ans_pci_v1`. Mailbox start/wakeup →
`FLAG_STARTED` + bootstrap endpoint handshake (+ optional BusMaster latch) is
in `ans_mbox_v1` (still no ACTIVE); `vf_ans_bind_mbox`
gates Identify DMA on STARTED.
ASCWrap `vf_ascwrap_bind_mbox` mirrors STARTED→READY; `vf_ans_bind_ascwrap`
gates Identify DMA on READY. Autoboot `vf_ans_autoboot_bind_ascwrap` mirrors
READY→ARMED; `vf_ans_bind_autoboot` gates Identify DMA on ARMED.

## Deferred (see M1_SOURCE_PORT_MANIFEST.md)

Full AppleMbox endpoint graph beyond graph-local bootstrap EP 255, real PCIe
ECAM / real guest GPA MSI walk, PRP list walk, block backend, Apple DT IRQ
numbers / guest acceptance (`preos_bridge` ANS→STORAGE AIC auto-wire present;
window `0x4` separate), additional namespace creation, and M1/t8103 physical
base addresses. Identify CNS=CTRL|NS host/test DMA fill + admin SQ doorbell
submit + optional guest ASQ fetch / PRP/SGL honesty + optional PRP1/PRP2
GPA→buffer translate + optional guest ACQ CQE MMIO + simplified admin CQ
completion / CQH drain + `vf_ans_irq_check` / INTMS mask + bound PCI
INTx/MSI pending sync + bound mbox STARTED Identify DMA gate + bound ASCWrap
READY Identify DMA gate + bound autoboot ARMED Identify DMA gate (metadata →
4096 buffer; CQE → 16-byte buffer + ACQ when bound; fail-closed without `CC.EN`
/ bound BusMaster / bound mbox STARTED / bound ASCWrap READY / bound autoboot
ARMED / SGL / zero PRP1 /
unaligned-without-PRP2 / aligned+PRP2!=0 / unaligned-PRP2 / invalid ACQ /
CNS=NS NSID!=1; optional bound MSI message write ≠ AIC alone; optional bound
AIC STORAGE-line mirror ≠ window 0x4; no PRP list / block backend) is in
`ans_v1`. ASCWrapV2 8-byte core present/bootstrap + optional mbox
STARTED→READY bind is in `ascwrap_v1` (`ASCWRAP.md`); akfRegMap
present/bootstrap + A2I EMPTY + I2A bootstrap endpoint handshake +
start/wakeup `FLAG_STARTED` (+ optional PCI Memory|BusMaster latch) is in
`ans_mbox_v1` (`ANS_MBOX.md`); autoBootRegMap present/bootstrap + optional
ASCWrap READY→ARMED bind is in `ans_autoboot_v1` (`ANS_AUTOBOOT.md`);
PCIe host present/bootstrap + container/MMCFG + COMMAND Memory|BusMaster
store/read + INTx pending latch + MSI capability/pending + MSI-X Enable/
Table BIR pending stub + bound MSI GPA translate (`vf_ans_pci_bind_msi_gpa`;
fail-closed OOB; preferred over DMA callback) + bound MSI message write
fallback (fail-closed unbound; message write ≠ AIC alone) + bound AIC
STORAGE line pending mirror (fail-closed unbound; ≠ window 0x4 / Apple DT
IRQ) is in `ans_pci_v1` (`ANS_PCI.md`);
`preos_bridge` auto-binds bridge `ans_v1` → `ans_pci` (Identify CQ irq_check →
PCI delivery_pending), that AIC sink, and a research MSI GPA window
(`vf_ans_pci_bind_msi_gpa`; base `0xfee00000`) on mmio_reset; host/test
`vf_m1_guest_ans_pci_prepare_msix` + `vf_m1_guest_ans_pci_msix_pending`
observe Identify → msix_pending / delivery_pending (fail-closed; **≠
PBA/table MMIO**). Next auto step: MSI-X table/PBA MMIO deepen if a guest
probes it. Avoid GXF / NVRAM / SoftMMU / `verify_boot_runtime` (owned
elsewhere). Deeper
ASC/AKF physical maps, IOP firmware load, and the full AppleMbox endpoint
graph remain deferred. Not sufficient to boot macOS storage stack. No
generic AHCI/NVMe substitute without ANS vendor semantics.
