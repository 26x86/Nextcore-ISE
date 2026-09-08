//! Explicit AArch64 guest architectural state and a bounded reference core.
//!
//! EFI owns the outer lifecycle and the C JIT owns host code emission. This
//! module owns only guest architectural state. The reference core is used for
//! the privileged/MMU contract tests and for feature-requested runs; the
//! existing base JIT remains the fast path for the original diagnostic guest.
//! A step is committed only after decode, translation, and permission checks.

#![allow(dead_code)]

use super::mmu::{Access, Fault as MmuFault, VfMmu};
use super::pauth::{self, PauthContext, PauthState};

/// Guest physical memory boundary used by the reference execution core.
///
/// The bus is deliberately smaller than a general address-space framework: it
/// only supplies bounded reads/writes and a host-side code-load operation.  EFI
/// pointers never cross this trait.  A machine can add MMIO without teaching
/// the architectural decoder about a particular device.
pub(crate) trait GuestBus {
    /// Copy an immutable test image into guest RAM before the first fetch.
    /// This is a loader operation, not an emulated guest store, and therefore
    /// does not use guest translation or MMIO side effects.
    fn load_code(&mut self, code: &[u8]) -> bool;

    fn read(
        &mut self,
        address: u64,
        size: usize,
        access: Access,
    ) -> Result<u64, ExceptionKind>;

    fn write(
        &mut self,
        address: u64,
        size: usize,
        value: u64,
        access: Access,
    ) -> Result<(), ExceptionKind>;

    /// Pair operations require RAM preflight before either transfer. A bus
    /// with MMIO must explicitly opt in; scalar accesses are not a preflight.
    fn pair_ram(&mut self) -> Option<(&mut [u8], u64)> { None }

    /// Device graphs use this hook to observe an architecturally committed
    /// instruction.  The default RAM bus has no device clock to update.
    fn after_instruction(&mut self, _state: &mut GuestCpuState) {}
}

struct RamBus<'a> {
    ram: &'a mut [u8],
    base: u64,
}

impl<'a> RamBus<'a> {
    fn new(ram: &'a mut [u8]) -> Self {
        Self { ram, base: 0 }
    }
}

impl GuestBus for RamBus<'_> {
    fn pair_ram(&mut self) -> Option<(&mut [u8], u64)> { Some((self.ram, self.base)) }
    fn load_code(&mut self, code: &[u8]) -> bool {
        if code.len() > self.ram.len() {
            return false;
        }
        self.ram[..code.len()].copy_from_slice(code);
        true
    }

    fn read(
        &mut self,
        address: u64,
        size: usize,
        _access: Access,
    ) -> Result<u64, ExceptionKind> {
        let offset = address.checked_sub(self.base).ok_or(ExceptionKind::DataAbort)?;
        read_width(self.ram, usize::try_from(offset).unwrap_or(usize::MAX), size)
            .ok_or(ExceptionKind::DataAbort)
    }

    fn write(
        &mut self,
        address: u64,
        size: usize,
        value: u64,
        _access: Access,
    ) -> Result<(), ExceptionKind> {
        let offset = address.checked_sub(self.base).ok_or(ExceptionKind::DataAbort)?;
        let index = usize::try_from(offset).map_err(|_| ExceptionKind::DataAbort)?;
        write_width(self.ram, index, size, value)
            .then_some(())
            .ok_or(ExceptionKind::DataAbort)
    }
}

pub(crate) const PSTATE_N: u32 = 1 << 31;
pub(crate) const PSTATE_Z: u32 = 1 << 30;
pub(crate) const PSTATE_C: u32 = 1 << 29;
pub(crate) const PSTATE_V: u32 = 1 << 28;
pub(crate) const PSTATE_D: u32 = 1 << 9;
pub(crate) const PSTATE_A: u32 = 1 << 8;
pub(crate) const PSTATE_I: u32 = 1 << 7;
pub(crate) const PSTATE_F: u32 = 1 << 6;

const SCTLR_M: u64 = 1 << 0;
const SCTLR_A: u64 = 1 << 1;
const CNTP_CTL_ENABLE: u64 = 1 << 0;
const CNTP_CTL_IMASK: u64 = 1 << 1;
const CNTP_CTL_ISTATUS: u64 = 1 << 2;
const M1_COUNTER_FREQUENCY: u64 = 24_000_000;
const PSTATE_MODE_MASK: u32 = 0x0f;

fn mode_for_el(level: ExceptionLevel) -> u32 {
    match level {
        ExceptionLevel::El0 => 0,
        ExceptionLevel::El1 => 5,
        // AArch64 PSTATE.M[3:0]: EL1t/EL1h=4/5,
        // EL2t/EL2h=8/9, EL3t/EL3h=12/13.  The reference reset state uses
        // the h (current-EL stack) form at each privileged level.
        ExceptionLevel::El2 => 9,
        ExceptionLevel::El3 => 13,
    }
}

fn level_for_mode(mode: u32) -> Option<ExceptionLevel> {
    match mode & PSTATE_MODE_MASK {
        0 => Some(ExceptionLevel::El0),
        4 | 5 => Some(ExceptionLevel::El1),
        8 | 9 => Some(ExceptionLevel::El2),
        12 | 13 => Some(ExceptionLevel::El3),
        _ => None,
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub(crate) enum ExceptionKind {
    UndefinedInstruction = 1,
    PrivilegedInstruction = 2,
    InstructionAbort = 3,
    DataAbort = 4,
    TranslationFault = 5,
    PermissionFault = 6,
    AlignmentFault = 7,
    SystemRegisterTrap = 8,
    TimerInterrupt = 9,
    ExternalInterrupt = 10,
    GuestHalt = 11,
    SupervisorCall = 12,
    FiqInterrupt = 13,
    SpAlignmentFault = 14,
}

const ESR_EC_UNKNOWN: u64 = 0x00;
const ESR_EC_SVC64: u64 = 0x15;
const ESR_EC_SYSREG: u64 = 0x18;
const ESR_EC_IABT_LOWER: u64 = 0x20;
const ESR_EC_IABT_SAME: u64 = 0x21;
const ESR_EC_DABT_LOWER: u64 = 0x24;
const ESR_EC_DABT_SAME: u64 = 0x25;
const ESR_FSC_TRANSLATION_L3: u64 = 0x07;
const ESR_FSC_ACCESS_FLAG_L3: u64 = 0x09;
const ESR_FSC_PERMISSION_L3: u64 = 0x0d;
const ESR_FSC_ALIGNMENT: u64 = 0x21;
const ESR_ISS_WNR: u64 = 1 << 6;
const INTERNAL_ACCESS_WNR: u64 = 1 << 22;

fn encoded_ec(syndrome: u64) -> u64 {
    syndrome >> 26
}

fn expected_ec(kind: ExceptionKind, source_el: ExceptionLevel) -> Option<u64> {
    match kind {
        ExceptionKind::SpAlignmentFault => Some(0x26),
        ExceptionKind::SupervisorCall => Some(ESR_EC_SVC64),
        ExceptionKind::SystemRegisterTrap => Some(ESR_EC_SYSREG),
        ExceptionKind::InstructionAbort => Some(if source_el == ExceptionLevel::El0 {
            ESR_EC_IABT_LOWER
        } else {
            ESR_EC_IABT_SAME
        }),
        ExceptionKind::DataAbort
        | ExceptionKind::TranslationFault
        | ExceptionKind::PermissionFault
        | ExceptionKind::AlignmentFault => Some(if source_el == ExceptionLevel::El0 {
            ESR_EC_DABT_LOWER
        } else {
            ESR_EC_DABT_SAME
        }),
        _ => None,
    }
}

/// Convert the internal exception payload into the ESR_ELx value visible to a
/// guest handler.  Decoder call sites pass the raw instruction as a compact
/// ISS source; direct state-model callers may pass an already encoded ESR.
/// The latter is retained only when its EC agrees with the exception class.
fn exception_syndrome(
    kind: ExceptionKind,
    source_el: ExceptionLevel,
    supplied: u64,
) -> u64 {
    if let Some(ec) = expected_ec(kind, source_el) {
        if encoded_ec(supplied) == ec {
            return supplied;
        }
    }
    match kind {
        ExceptionKind::SpAlignmentFault => (0x26 << 26) | (1 << 25),
        ExceptionKind::SupervisorCall => {
            (ESR_EC_SVC64 << 26) | (supplied & 0xffff)
        }
        ExceptionKind::SystemRegisterTrap => {
            (ESR_EC_SYSREG << 26) | (supplied & 0x01ff_ffff)
        }
        ExceptionKind::InstructionAbort => {
            let ec = expected_ec(kind, source_el).unwrap_or(ESR_EC_IABT_SAME);
            (ec << 26) | ESR_FSC_TRANSLATION_L3
        }
        ExceptionKind::DataAbort
        | ExceptionKind::TranslationFault
        | ExceptionKind::PermissionFault
        | ExceptionKind::AlignmentFault => {
            let ec = expected_ec(kind, source_el).unwrap_or(ESR_EC_DABT_SAME);
            let fsc = match kind {
                ExceptionKind::PermissionFault => ESR_FSC_PERMISSION_L3,
                ExceptionKind::AlignmentFault => ESR_FSC_ALIGNMENT,
                // Access-flag faults are currently normalized by the MMU
                // adapter to TranslationFault.  Keep the distinct FSC here
                // for callers that provide that value as the low ISS.
                ExceptionKind::TranslationFault if supplied & 0x3f == ESR_FSC_ACCESS_FLAG_L3 => {
                    ESR_FSC_ACCESS_FLAG_L3
                }
                _ => ESR_FSC_TRANSLATION_L3,
            };
            let wnr = if supplied & (1 << 22) != 0 {
                ESR_ISS_WNR
            } else {
                0
            };
            (ec << 26) | wnr | fsc
        }
        // Undefined and privilege failures have no architecturally useful
        // ISS in this boundary.  Their internal ExceptionKind remains the
        // precise classification returned to the host.
        ExceptionKind::UndefinedInstruction | ExceptionKind::PrivilegedInstruction => {
            ESR_EC_UNKNOWN << 26
        }
        ExceptionKind::TimerInterrupt
        | ExceptionKind::ExternalInterrupt
        | ExceptionKind::FiqInterrupt
        | ExceptionKind::GuestHalt => 0,
    }
}

fn access_syndrome(access: Access) -> u64 {
    match access {
        // Decoder paths use bit 22 as a private write/read hint.  The public
        // ESR WnR bit is bit 6 and is emitted only by exception_syndrome(),
        // after the exception class and source EL are known.
        Access::Write => INTERNAL_ACCESS_WNR,
        Access::Read | Access::Execute => 0,
    }
}

fn logical_immediate(word: u32) -> Option<u64> {
    let width=if word>>31!=0 {64} else {32};
    let n=(word>>22)&1;let imms=(word>>10)&63;let immr=(word>>16)&63;
    if width==32 && n!=0 { return None; }
    let tag=(n<<6)|(!imms&63);
    if tag<2 { return None; }
    let len=31-tag.leading_zeros();let size=1u32<<len;let levels=size-1;
    let s=imms&levels;let r=immr&levels;
    if size>width || s==levels { return None; }
    let mut element=(1u64<<(s+1))-1;
    if r!=0 { element=(element>>r)|(element<<(size-r)); }
    if size<64 { element&=(1u64<<size)-1; }
    let mut mask=0;let mut at=0;
    while at<width {mask|=element<<at;at+=size;}
    Some(mask)
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct GuestException {
    pub(crate) kind: ExceptionKind,
    /// The raw A64 word that caused a synchronous exception.  Asynchronous
    /// exceptions and fetch faults have no decoded word and use zero.  Keep
    /// this separate from `syndrome`: ESR_ELx is the guest-visible encoding,
    /// while the ABI result's `fault_instruction` field must remain the
    /// actual faulting instruction.
    pub(crate) instruction: u32,
    pub(crate) syndrome: u64,
    pub(crate) far: u64,
    pub(crate) pc: u64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub(crate) enum ExceptionLevel {
    El0 = 0,
    El1 = 1,
    El2 = 2,
    El3 = 3,
}

impl ExceptionLevel {
    fn from_u8(value: u8) -> Option<Self> {
        match value {
            0 => Some(Self::El0),
            1 => Some(Self::El1),
            2 => Some(Self::El2),
            3 => Some(Self::El3),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum SysRegFault {
    Undefined,
    Privilege,
    Unknown,
    ReadOnly,
    InvalidValue,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub(crate) enum SystemRegister {
    SctlrEl1 = 1,
    Ttbr0El1 = 2,
    Ttbr1El1 = 3,
    TcrEl1 = 4,
    MairEl1 = 5,
    VbarEl1 = 6,
    EsrEl1 = 7,
    FarEl1 = 8,
    ElrEl1 = 9,
    SpsrEl1 = 10,
    CntfrqEl0 = 11,
    CntpctEl0 = 12,
    CntpCtlEl0 = 13,
    CntpCvalEl0 = 14,
    CurrentEl = 15,
    IdAa64Mmfr0El1 = 16,
    IdAa64Isar1El1 = 17,
    CntvctEl0 = 18,
    CntvCtlEl0 = 19,
    CntvCvalEl0 = 20,
    CntpTvalEl0 = 21,
    HcrEl2 = 22,
    CnthctlEl2 = 23,
    VbarEl2 = 24,
    EsrEl2 = 25,
    FarEl2 = 26,
    ElrEl2 = 27,
    SpsrEl2 = 28,
    ScrEl3 = 29,
    VbarEl3 = 30,
    EsrEl3 = 31,
    FarEl3 = 32,
    ElrEl3 = 33,
    SpsrEl3 = 34,
    TpidrEl0 = 35,
    TpidrroEl0 = 36,
    TpidrEl1 = 37,
    PlatformOverride = 38,
}

impl SystemRegister {
    /// Match the architectural encodings after the Rt field is cleared. The
    /// two opcodes for a register (MRS and MSR) are listed together because
    /// the access direction is decided by the instruction decoder.
    fn from_instruction(word: u32) -> Option<Self> {
        match word & !31 {
            0xd53d_f500 | 0xd51d_f500 => Some(Self::PlatformOverride),
            0xd53b_d040 | 0xd51b_d040 => Some(Self::TpidrEl0),
            0xd53b_d060 | 0xd51b_d060 => Some(Self::TpidrroEl0),
            0xd538_d080 | 0xd518_d080 => Some(Self::TpidrEl1),
            0xd538_1000 | 0xd518_1000 => Some(Self::SctlrEl1),
            0xd538_2000 | 0xd518_2000 => Some(Self::Ttbr0El1),
            0xd538_2020 | 0xd518_2020 => Some(Self::Ttbr1El1),
            0xd538_2040 | 0xd518_2040 => Some(Self::TcrEl1),
            0xd538_a000 | 0xd518_a000 => Some(Self::MairEl1),
            0xd538_c000 | 0xd518_c000 => Some(Self::VbarEl1),
            0xd538_5200 | 0xd518_5200 => Some(Self::EsrEl1),
            0xd538_6000 | 0xd518_6000 => Some(Self::FarEl1),
            0xd538_4020 | 0xd518_4020 => Some(Self::ElrEl1),
            0xd538_4000 | 0xd518_4000 => Some(Self::SpsrEl1),
            0xd53b_e000 | 0xd51b_e000 => Some(Self::CntfrqEl0),
            0xd53b_e020 | 0xd51b_e020 => Some(Self::CntpctEl0),
            0xd53b_e220 | 0xd51b_e220 => Some(Self::CntpCtlEl0),
            0xd53b_e240 | 0xd51b_e240 => Some(Self::CntpCvalEl0),
            0xd538_4240 => Some(Self::CurrentEl),
            0xd538_0700 | 0xd518_0700 => Some(Self::IdAa64Mmfr0El1),
            0xd538_0620 | 0xd518_0620 => Some(Self::IdAa64Isar1El1),
            0xd53b_e040 | 0xd51b_e040 => Some(Self::CntvctEl0),
            0xd53b_e320 | 0xd51b_e320 => Some(Self::CntvCtlEl0),
            0xd53b_e340 | 0xd51b_e340 => Some(Self::CntvCvalEl0),
            0xd53b_e200 | 0xd51b_e200 => Some(Self::CntpTvalEl0),
            0xd53c_1100 | 0xd51c_1100 => Some(Self::HcrEl2),
            0xd53c_e100 | 0xd51c_e100 => Some(Self::CnthctlEl2),
            0xd53c_c000 | 0xd51c_c000 => Some(Self::VbarEl2),
            0xd53c_5200 | 0xd51c_5200 => Some(Self::EsrEl2),
            0xd53c_6000 | 0xd51c_6000 => Some(Self::FarEl2),
            0xd53c_4020 | 0xd51c_4020 => Some(Self::ElrEl2),
            0xd53c_4000 | 0xd51c_4000 => Some(Self::SpsrEl2),
            0xd53e_1100 | 0xd51e_1100 => Some(Self::ScrEl3),
            0xd53e_c000 | 0xd51e_c000 => Some(Self::VbarEl3),
            0xd53e_5200 | 0xd51e_5200 => Some(Self::EsrEl3),
            0xd53e_6000 | 0xd51e_6000 => Some(Self::FarEl3),
            0xd53e_4020 | 0xd51e_4020 => Some(Self::ElrEl3),
            0xd53e_4000 | 0xd51e_4000 => Some(Self::SpsrEl3),
            _ => None,
        }
    }

    fn minimum_el(self) -> ExceptionLevel {
        match self {
            Self::TpidrEl0 | Self::TpidrroEl0 => ExceptionLevel::El0,
            Self::CntfrqEl0
            | Self::CntpctEl0
            | Self::CntpCtlEl0
            | Self::CntpCvalEl0
            | Self::CurrentEl
            | Self::CntvctEl0
            | Self::CntvCtlEl0
            | Self::CntvCvalEl0
            | Self::CntpTvalEl0 => ExceptionLevel::El0,
            Self::IdAa64Mmfr0El1 | Self::IdAa64Isar1El1 => ExceptionLevel::El1,
            Self::HcrEl2
            | Self::CnthctlEl2
            | Self::VbarEl2
            | Self::EsrEl2
            | Self::FarEl2
            | Self::ElrEl2
            | Self::SpsrEl2 => ExceptionLevel::El2,
            Self::ScrEl3
            | Self::VbarEl3
            | Self::EsrEl3
            | Self::FarEl3
            | Self::ElrEl3
            | Self::SpsrEl3 => ExceptionLevel::El3,
            _ => ExceptionLevel::El1,
        }
    }
}

#[derive(Clone, Copy)]
pub(crate) struct SystemRegisters {
    pub(crate) tpidr_el0: u64,
    pub(crate) tpidrro_el0: u64,
    pub(crate) tpidr_el1: u64,
    pub(crate) sctlr_el1: u64,
    pub(crate) ttbr0_el1: u64,
    pub(crate) ttbr1_el1: u64,
    pub(crate) tcr_el1: u64,
    pub(crate) mair_el1: u64,
    pub(crate) vbar_el1: u64,
    pub(crate) esr_el1: u64,
    pub(crate) far_el1: u64,
    pub(crate) elr_el1: u64,
    pub(crate) spsr_el1: u64,
    pub(crate) cntfrq_el0: u64,
    pub(crate) cntpct_el0: u64,
    pub(crate) cntp_ctl_el0: u64,
    pub(crate) cntp_cval_el0: u64,
    pub(crate) id_aa64mmfr0_el1: u64,
    pub(crate) id_aa64isar1_el1: u64,
    pub(crate) cntvct_el0: u64,
    pub(crate) cntv_ctl_el0: u64,
    pub(crate) cntv_cval_el0: u64,
    pub(crate) cntp_tval_el0: u64,
    pub(crate) hcr_el2: u64,
    pub(crate) cnthctl_el2: u64,
    pub(crate) vbar_el2: u64,
    pub(crate) esr_el2: u64,
    pub(crate) far_el2: u64,
    pub(crate) elr_el2: u64,
    pub(crate) spsr_el2: u64,
    pub(crate) scr_el3: u64,
    pub(crate) vbar_el3: u64,
    pub(crate) esr_el3: u64,
    pub(crate) far_el3: u64,
    pub(crate) elr_el3: u64,
    pub(crate) spsr_el3: u64,
}

impl SystemRegisters {
    pub(crate) const fn reset() -> Self {
        Self {
            // Architectural reset is UNKNOWN. Choose deterministic zero, as
            // in the C bank; these software values are not memory addresses
            // interpreted by the PE. FGT/AArch32 aliases are not implemented.
            tpidr_el0: 0,
            tpidrro_el0: 0,
            tpidr_el1: 0,
            sctlr_el1: 0,
            ttbr0_el1: 0,
            ttbr1_el1: 0,
            tcr_el1: 16,
            mair_el1: 0,
            vbar_el1: 0,
            esr_el1: 0,
            far_el1: 0,
            elr_el1: 0,
            spsr_el1: 0,
            cntfrq_el0: M1_COUNTER_FREQUENCY,
            cntpct_el0: 0,
            cntp_ctl_el0: 0,
            cntp_cval_el0: 0,
            // Bounded 4K/16K translation and baseline QARMA5 authentication.
            // Enhanced PAuth, FPAC and generic authentication remain absent.
            id_aa64mmfr0_el1: 0x0010_1122,
            id_aa64isar1_el1: 0x10,
            cntvct_el0: 0,
            cntv_ctl_el0: 0,
            cntv_cval_el0: 0,
            cntp_tval_el0: 0,
            hcr_el2: 0,
            cnthctl_el2: 0,
            vbar_el2: 0,
            esr_el2: 0,
            far_el2: 0,
            elr_el2: 0,
            spsr_el2: 0,
            scr_el3: 0,
            vbar_el3: 0,
            esr_el3: 0,
            far_el3: 0,
            elr_el3: 0,
            spsr_el3: 0,
        }
    }
}

#[derive(Clone, Copy)]
pub(crate) struct GenericTimer {
    pub(crate) frequency: u64,
    pub(crate) counter: u64,
    pub(crate) ctl: u64,
    pub(crate) cval: u64,
}

impl GenericTimer {
    pub(crate) const fn reset() -> Self {
        Self {
            frequency: M1_COUNTER_FREQUENCY,
            counter: 0,
            ctl: 0,
            cval: 0,
        }
    }

    pub(crate) fn pending(&self) -> bool {
        self.ctl & CNTP_CTL_ENABLE != 0
            && self.ctl & CNTP_CTL_IMASK == 0
            && self.counter >= self.cval
    }

    pub(crate) fn advance(&mut self, ticks: u64) {
        self.counter = self.counter.saturating_add(ticks);
        if self.pending() {
            self.ctl |= CNTP_CTL_ISTATUS;
        } else {
            self.ctl &= !CNTP_CTL_ISTATUS;
        }
    }
}

#[derive(Clone, Copy)]
pub(crate) struct SmpState {
    pub(crate) cpu_count: u32,
    pub(crate) online_mask: u64,
    pub(crate) external_pending: u64,
}

impl SmpState {
    pub(crate) const fn reset() -> Self {
        Self {
            cpu_count: 1,
            online_mask: 1,
            external_pending: 0,
        }
    }

    pub(crate) fn set_cpu_count(&mut self, count: u32) -> bool {
        if count == 0 || count > 64 {
            return false;
        }
        self.cpu_count = count;
        self.online_mask &= if count == 64 {
            u64::MAX
        } else {
            (1u64 << count) - 1
        };
        if self.online_mask == 0 {
            self.online_mask = 1;
        }
        true
    }

    pub(crate) fn bring_online(&mut self, affinity: u32) -> bool {
        if affinity >= self.cpu_count || affinity >= 64 {
            return false;
        }
        self.online_mask |= 1u64 << affinity;
        true
    }

    pub(crate) fn route_external(&mut self, target_mask: u64) {
        self.external_pending |= target_mask & self.online_mask;
    }
}

#[derive(Clone, Copy)]
pub(crate) struct ExclusiveMonitor {
    pub(crate) valid: bool,
    pub(crate) address: u64,
    pub(crate) size: u8,
    pub(crate) owner: u32,
}

impl ExclusiveMonitor {
    pub(crate) const fn reset() -> Self {
        Self {
            valid: false,
            address: 0,
            size: 0,
            owner: 0,
        }
    }

    pub(crate) fn reserve(&mut self, address: u64, size: u8, owner: u32) {
        self.valid = true;
        self.address = address;
        self.size = size;
        self.owner = owner;
    }

    pub(crate) fn succeeds(&self, address: u64, size: u8, owner: u32) -> bool {
        self.valid && self.address == address && self.size == size && self.owner == owner
    }

    pub(crate) fn clear_on_store(&mut self, address: u64, size: u8) {
        if self.valid
            && self.address < address.saturating_add(u64::from(size))
            && address < self.address.saturating_add(u64::from(self.size))
        {
            self.valid = false;
        }
    }
}

pub(crate) type PauthBoundary = PauthState;

#[derive(Clone, Copy)]
pub(crate) struct GuestCpuState {
    pub(crate) x: [u64; 31],
    pub(crate) sp: u64,
    pub(crate) sp_el: [u64; 4],
    pub(crate) pc: u64,
    pub(crate) pstate: u32,
    pub(crate) current_el: ExceptionLevel,
    pub(crate) sys: SystemRegisters,
    pub(crate) pending_exception: Option<GuestException>,
    pub(crate) exception_target: Option<ExceptionLevel>,
    pub(crate) exception_from_lower_el: bool,
    pub(crate) exception_vector: u64,
    pub(crate) affinity: u32,
    pub(crate) mmu: VfMmu,
    pub(crate) timer: GenericTimer,
    pub(crate) virtual_timer: GenericTimer,
    pub(crate) smp: SmpState,
    pub(crate) exclusive: ExclusiveMonitor,
    pub(crate) pauth: PauthBoundary,
    pub(crate) waiting: bool,
    pub(crate) retired: u64,
    pub(crate) platform: crate::platform::PlatformState,
}

impl GuestCpuState {
    pub(crate) fn reset(affinity: u32) -> Self {
        Self {
            x: [0; 31],
            sp: 0,
            sp_el: [0; 4],
            pc: 0,
            pstate: mode_for_el(ExceptionLevel::El1),
            current_el: ExceptionLevel::El1,
            sys: SystemRegisters::reset(),
            pending_exception: None,
            exception_target: None,
            exception_from_lower_el: false,
            exception_vector: 0,
            affinity,
            mmu: VfMmu::disabled(),
            timer: GenericTimer::reset(),
            virtual_timer: GenericTimer::reset(),
            smp: SmpState::reset(),
            exclusive: ExclusiveMonitor::reset(),
            pauth: PauthBoundary::reset(),
            waiting: false,
            retired: 0,
            platform: crate::platform::PlatformState::reset(),
        }
    }

    pub(crate) fn set_exception_level(&mut self, value: u8) -> bool {
        let Some(level) = ExceptionLevel::from_u8(value) else {
            return false;
        };
        self.sp_el[self.active_sp_index()] = self.sp;
        self.current_el = level;
        self.pstate = (self.pstate & !PSTATE_MODE_MASK) | mode_for_el(level);
        self.sp = self.sp_el[level as usize];
        true
    }

    pub(crate) fn raise(&mut self, exception: GuestException) {
        if self.pending_exception.is_some() {
            return;
        }
        let mut exception = exception;
        exception.syndrome = exception_syndrome(
            exception.kind,
            self.current_el,
            exception.syndrome,
        );
        let target = if self.current_el == ExceptionLevel::El0 {
            ExceptionLevel::El1
        } else {
            self.current_el
        };
        self.write_exception_bank(target, self.pstate, exception);
        self.exception_target = Some(target);
        self.exception_from_lower_el = self.current_el != target;
        self.pending_exception = Some(exception);
    }

    fn write_exception_bank(
        &mut self,
        target: ExceptionLevel,
        saved_pstate: u32,
        exception: GuestException,
    ) {
        let asynchronous=matches!(exception.kind, ExceptionKind::TimerInterrupt |
            ExceptionKind::ExternalInterrupt | ExceptionKind::FiqInterrupt);
        match target {
            ExceptionLevel::El0 => {}
            ExceptionLevel::El1 => {
                self.sys.spsr_el1 = u64::from(saved_pstate);
                if !asynchronous { self.sys.esr_el1 = exception.syndrome; self.sys.far_el1 = exception.far; }
                self.sys.elr_el1 = exception.pc;
            }
            ExceptionLevel::El2 => {
                self.sys.spsr_el2 = u64::from(saved_pstate);
                if !asynchronous { self.sys.esr_el2 = exception.syndrome; self.sys.far_el2 = exception.far; }
                self.sys.elr_el2 = exception.pc;
            }
            ExceptionLevel::El3 => {
                self.sys.spsr_el3 = u64::from(saved_pstate);
                if !asynchronous { self.sys.esr_el3 = exception.syndrome; self.sys.far_el3 = exception.far; }
                self.sys.elr_el3 = exception.pc;
            }
        }
    }

    fn exception_bank(&self, level: ExceptionLevel) -> (u64, u64, u64) {
        match level {
            ExceptionLevel::El0 => (0, 0, 0),
            ExceptionLevel::El1 => (
                self.sys.spsr_el1,
                self.sys.elr_el1,
                self.sys.vbar_el1,
            ),
            ExceptionLevel::El2 => (
                self.sys.spsr_el2,
                self.sys.elr_el2,
                self.sys.vbar_el2,
            ),
            ExceptionLevel::El3 => (
                self.sys.spsr_el3,
                self.sys.elr_el3,
                self.sys.vbar_el3,
            ),
        }
    }

    fn active_sp_index(&self) -> usize {
        if self.current_el == ExceptionLevel::El0 || self.pstate & 1 == 0 {
            ExceptionLevel::El0 as usize
        } else {
            self.current_el as usize
        }
    }

    fn exception_offset(kind: ExceptionKind, from: ExceptionLevel, saved_pstate: u32) -> u64 {
        let synchronous = if from == ExceptionLevel::El0 {
            0x400
        } else if saved_pstate & 1 != 0 {
            0x200
        } else {
            0
        };
        match kind {
            ExceptionKind::TimerInterrupt | ExceptionKind::ExternalInterrupt => synchronous + 0x80,
            ExceptionKind::FiqInterrupt => synchronous + 0x100,
            _ => synchronous,
        }
    }

    pub(crate) fn take_pending_exception(&mut self) -> Option<GuestException> {
        let exception = self.pending_exception?;
        if exception.kind == ExceptionKind::GuestHalt {
            return Some(exception);
        }
        let old_level = self.current_el;
        let target = self.exception_target.unwrap_or(if old_level == ExceptionLevel::El0 {
            ExceptionLevel::El1
        } else {
            old_level
        });
        let saved_pstate = self.pstate;
        self.sp_el[self.active_sp_index()] = self.sp;
        self.write_exception_bank(target, saved_pstate, exception);
        self.current_el = target;
        self.pstate = (saved_pstate & !PSTATE_MODE_MASK)
            | mode_for_el(target)
            | PSTATE_D
            | PSTATE_A
            | PSTATE_I
            | PSTATE_F;
        self.sp = self.sp_el[target as usize];
        let (_, _, vector_base) = self.exception_bank(target);
        self.exception_vector = (vector_base & !0x7ff)
            .wrapping_add(Self::exception_offset(exception.kind, old_level, saved_pstate));
        self.pc = self.exception_vector;
        self.pending_exception = None;
        self.exception_target = None;
        self.exception_from_lower_el = false;
        Some(exception)
    }

    pub(crate) fn state_valid(&self) -> bool {
        let mode_valid = match self.current_el {
            ExceptionLevel::El0 => self.pstate & PSTATE_MODE_MASK == 0,
            ExceptionLevel::El1 => matches!(self.pstate & PSTATE_MODE_MASK, 4 | 5),
            ExceptionLevel::El2 => matches!(self.pstate & PSTATE_MODE_MASK, 8 | 9),
            ExceptionLevel::El3 => matches!(self.pstate & PSTATE_MODE_MASK, 12 | 13),
        };
        mode_valid
            && self
                .exception_target
                .map_or(self.pending_exception.is_none(), |target| {
                    self.pending_exception.is_some() && target as u8 <= 3
                })
            && (!self.exception_from_lower_el
                || self
                    .exception_target
                    .is_some_and(|target| (self.current_el as u8) < target as u8))
    }

    pub(crate) fn read_sysreg(&mut self, reg: SystemRegister) -> Result<u64, SysRegFault> {
        if reg==SystemRegister::PlatformOverride {
            return if self.platform.profile==crate::platform::PROFILE_IRQ_COMPAT_V1 && self.current_el==ExceptionLevel::El1 && self.sys.hcr_el2==0 && self.sys.scr_el3==0 {
                Ok(self.platform.override_value)
            } else { Err(SysRegFault::Unknown) };
        }
        if reg == SystemRegister::TpidrEl1 && self.current_el == ExceptionLevel::El0 {
            return Err(SysRegFault::Undefined);
        }
        if (self.current_el as u8) < reg.minimum_el() as u8 {
            return Err(SysRegFault::Privilege);
        }
        match reg {
            SystemRegister::PlatformOverride => unreachable!(),
            SystemRegister::TpidrEl0 => Ok(self.sys.tpidr_el0),
            SystemRegister::TpidrroEl0 => Ok(self.sys.tpidrro_el0),
            SystemRegister::TpidrEl1 => Ok(self.sys.tpidr_el1),
            SystemRegister::SctlrEl1 => Ok(self.sys.sctlr_el1),
            SystemRegister::Ttbr0El1 => Ok(self.sys.ttbr0_el1),
            SystemRegister::Ttbr1El1 => Ok(self.sys.ttbr1_el1),
            SystemRegister::TcrEl1 => Ok(self.sys.tcr_el1),
            SystemRegister::MairEl1 => Ok(self.sys.mair_el1),
            SystemRegister::VbarEl1 => Ok(self.sys.vbar_el1),
            SystemRegister::EsrEl1 => Ok(self.sys.esr_el1),
            SystemRegister::FarEl1 => Ok(self.sys.far_el1),
            SystemRegister::ElrEl1 => Ok(self.sys.elr_el1),
            SystemRegister::SpsrEl1 => Ok(self.sys.spsr_el1),
            SystemRegister::CntfrqEl0 => Ok(self.timer.frequency),
            SystemRegister::CntpctEl0 => Ok(self.timer.counter),
            SystemRegister::CntpCtlEl0 => Ok(self.timer.ctl),
            SystemRegister::CntpCvalEl0 => Ok(self.timer.cval),
            SystemRegister::CurrentEl => Ok(u64::from(self.current_el as u8) << 2),
            SystemRegister::IdAa64Mmfr0El1 => Ok(self.sys.id_aa64mmfr0_el1),
            SystemRegister::IdAa64Isar1El1 => Ok(self.sys.id_aa64isar1_el1),
            SystemRegister::CntvctEl0 => Ok(self.virtual_timer.counter),
            SystemRegister::CntvCtlEl0 => Ok(self.virtual_timer.ctl),
            SystemRegister::CntvCvalEl0 => Ok(self.virtual_timer.cval),
            SystemRegister::CntpTvalEl0 => Ok(self.timer.cval.saturating_sub(self.timer.counter)),
            SystemRegister::HcrEl2 => Ok(self.sys.hcr_el2),
            SystemRegister::CnthctlEl2 => Ok(self.sys.cnthctl_el2),
            SystemRegister::VbarEl2 => Ok(self.sys.vbar_el2),
            SystemRegister::EsrEl2 => Ok(self.sys.esr_el2),
            SystemRegister::FarEl2 => Ok(self.sys.far_el2),
            SystemRegister::ElrEl2 => Ok(self.sys.elr_el2),
            SystemRegister::SpsrEl2 => Ok(self.sys.spsr_el2),
            SystemRegister::ScrEl3 => Ok(self.sys.scr_el3),
            SystemRegister::VbarEl3 => Ok(self.sys.vbar_el3),
            SystemRegister::EsrEl3 => Ok(self.sys.esr_el3),
            SystemRegister::FarEl3 => Ok(self.sys.far_el3),
            SystemRegister::ElrEl3 => Ok(self.sys.elr_el3),
            SystemRegister::SpsrEl3 => Ok(self.sys.spsr_el3),
        }
    }

    pub(crate) fn write_sysreg(
        &mut self,
        reg: SystemRegister,
        value: u64,
    ) -> Result<(), SysRegFault> {
        if reg==SystemRegister::PlatformOverride {
            if self.platform.profile!=crate::platform::PROFILE_IRQ_COMPAT_V1 || self.current_el!=ExceptionLevel::El1 || self.sys.hcr_el2!=0 || self.sys.scr_el3!=0 {
                return Err(SysRegFault::Unknown);
            }
            return if self.platform.configure(self.platform.profile,value) { Ok(()) }
                else { Err(SysRegFault::InvalidValue) };
        }
        if self.current_el == ExceptionLevel::El0
            && matches!(reg, SystemRegister::TpidrEl1 | SystemRegister::TpidrroEl0)
        {
            return Err(SysRegFault::Undefined);
        }
        if (self.current_el as u8) < reg.minimum_el() as u8 {
            return Err(SysRegFault::Privilege);
        }
        match reg {
            SystemRegister::PlatformOverride => unreachable!(),
            SystemRegister::TpidrEl0 => self.sys.tpidr_el0 = value,
            SystemRegister::TpidrroEl0 => self.sys.tpidrro_el0 = value,
            SystemRegister::TpidrEl1 => self.sys.tpidr_el1 = value,
            SystemRegister::SctlrEl1 => {
                let old = self.sys.sctlr_el1;
                self.sys.sctlr_el1 = value;
                if self.sync_mmu().is_err() {
                    self.sys.sctlr_el1 = old;
                    let _ = self.sync_mmu();
                    return Err(SysRegFault::InvalidValue);
                }
            }
            SystemRegister::Ttbr0El1 => {
                if value & 0xfff != 0 {
                    return Err(SysRegFault::InvalidValue);
                }
                let old = self.sys.ttbr0_el1;
                self.sys.ttbr0_el1 = value;
                if self.sys.sctlr_el1 & SCTLR_M != 0 {
                    if self.sync_mmu().is_err() {
                        self.sys.ttbr0_el1 = old;
                        let _ = self.sync_mmu();
                        return Err(SysRegFault::InvalidValue);
                    }
                }
            }
            SystemRegister::Ttbr1El1 => {
                if value & 0xfff != 0 {
                    return Err(SysRegFault::InvalidValue);
                }
                let old = self.sys.ttbr1_el1;
                self.sys.ttbr1_el1 = value;
                if self.sys.sctlr_el1 & SCTLR_M != 0 && self.sync_mmu().is_err() {
                    self.sys.ttbr1_el1 = old;
                    let _ = self.sync_mmu();
                    return Err(SysRegFault::InvalidValue);
                }
            }
            SystemRegister::TcrEl1 => {
                let old = self.sys.tcr_el1;
                self.sys.tcr_el1 = value;
                if self.sys.sctlr_el1 & SCTLR_M != 0 && self.sync_mmu().is_err() {
                    self.sys.tcr_el1 = old;
                    let _ = self.sync_mmu();
                    return Err(SysRegFault::InvalidValue);
                }
            }
            SystemRegister::MairEl1 => self.sys.mair_el1 = value,
            SystemRegister::VbarEl1 => self.sys.vbar_el1 = value & !0x7ff,
            SystemRegister::EsrEl1 => self.sys.esr_el1 = value,
            SystemRegister::FarEl1 => self.sys.far_el1 = value,
            SystemRegister::ElrEl1 => self.sys.elr_el1 = value,
            SystemRegister::SpsrEl1 => self.sys.spsr_el1 = value,
            SystemRegister::CntfrqEl0
            | SystemRegister::CntpctEl0
            | SystemRegister::CurrentEl
            | SystemRegister::IdAa64Mmfr0El1
            | SystemRegister::IdAa64Isar1El1
            | SystemRegister::CntvctEl0 => {
                return Err(SysRegFault::ReadOnly)
            }
            SystemRegister::CntpCtlEl0 => {
                // ISTATUS is read-only and is recomputed from counter/CVAL;
                // only ENABLE and IMASK are writable through this register.
                self.timer.ctl = value & (CNTP_CTL_ENABLE | CNTP_CTL_IMASK);
                if self.timer.pending() {
                    self.timer.ctl |= CNTP_CTL_ISTATUS;
                }
                self.sys.cntp_ctl_el0 = self.timer.ctl;
            }
            SystemRegister::CntpCvalEl0 => {
                self.timer.cval = value;
                self.sys.cntp_cval_el0 = value;
            }
            SystemRegister::CntvCtlEl0 => {
                self.virtual_timer.ctl = value & (CNTP_CTL_ENABLE | CNTP_CTL_IMASK);
                if self.virtual_timer.pending() {
                    self.virtual_timer.ctl |= CNTP_CTL_ISTATUS;
                }
                self.sys.cntv_ctl_el0 = self.virtual_timer.ctl;
            }
            SystemRegister::CntvCvalEl0 => {
                self.virtual_timer.cval = value;
                self.sys.cntv_cval_el0 = value;
            }
            SystemRegister::CntpTvalEl0 => {
                self.timer.cval = self.timer.counter.wrapping_add(value);
                self.sys.cntp_cval_el0 = self.timer.cval;
                self.sys.cntp_tval_el0 = value;
            }
            SystemRegister::HcrEl2 => self.sys.hcr_el2 = value,
            SystemRegister::CnthctlEl2 => self.sys.cnthctl_el2 = value,
            SystemRegister::VbarEl2 => self.sys.vbar_el2 = value & !0x7ff,
            SystemRegister::EsrEl2 => self.sys.esr_el2 = value,
            SystemRegister::FarEl2 => self.sys.far_el2 = value,
            SystemRegister::ElrEl2 => self.sys.elr_el2 = value,
            SystemRegister::SpsrEl2 => self.sys.spsr_el2 = value,
            SystemRegister::ScrEl3 => self.sys.scr_el3 = value,
            SystemRegister::VbarEl3 => self.sys.vbar_el3 = value & !0x7ff,
            SystemRegister::EsrEl3 => self.sys.esr_el3 = value,
            SystemRegister::FarEl3 => self.sys.far_el3 = value,
            SystemRegister::ElrEl3 => self.sys.elr_el3 = value,
            SystemRegister::SpsrEl3 => self.sys.spsr_el3 = value,
        }
        Ok(())
    }

    fn sync_mmu(&mut self) -> Result<(), ()> {
        if self.sys.sctlr_el1 & SCTLR_M == 0 {
            self.mmu.disable();
            return Ok(());
        }
        let asid = (self.sys.ttbr0_el1 >> 48) as u16;
        if !self.mmu.configure_tcr(
            self.sys.ttbr0_el1,
            self.sys.ttbr1_el1,
            self.sys.tcr_el1,
            asid,
        ) {
            return Err(());
        }
        Ok(())
    }

    pub(crate) fn advance_counter(&mut self, ticks: u64) {
        self.timer.advance(ticks);
        self.virtual_timer.advance(ticks);
        self.sys.cntpct_el0 = self.timer.counter;
        self.sys.cntp_ctl_el0 = self.timer.ctl;
        self.sys.cntvct_el0 = self.virtual_timer.counter;
        self.sys.cntv_ctl_el0 = self.virtual_timer.ctl;
        self.sys.cntp_tval_el0 = self.timer.cval.saturating_sub(self.timer.counter);
    }

    pub(crate) fn poll_interrupt(&mut self) -> bool {
        if self.pending_exception.is_some() {
            return false;
        }
        if (self.platform.irq || self.platform.fiq || self.timer.pending() || self.virtual_timer.pending() || self.smp.external_pending!=0)
            && (self.current_el as u8>1 || self.sys.hcr_el2&0x18!=0 || self.sys.scr_el3&6!=0) {
            self.raise(GuestException {kind:ExceptionKind::SystemRegisterTrap,instruction:0,
                syndrome:0,far:0,pc:self.pc});return true;
        }
        if self.platform.fiq && self.platform.fiq_enabled() && self.pstate & PSTATE_F == 0 {
            self.raise(GuestException { kind: ExceptionKind::FiqInterrupt,
                instruction:0,syndrome:0,far:0,pc:self.pc });return true;
        }
        if self.pstate & PSTATE_I != 0 || !self.platform.irq_enabled() { return false; }
        if self.timer.pending() || self.virtual_timer.pending() {
            self.raise(GuestException {
                kind: ExceptionKind::TimerInterrupt,
                instruction: 0,
                syndrome: 0,
                far: 0,
                pc: self.pc,
            });
            return true;
        }
        let bit = 1u64 << self.affinity;
        if self.platform.irq || self.smp.external_pending & bit != 0 {
            self.smp.external_pending &= !bit;
            self.raise(GuestException {
                kind: ExceptionKind::ExternalInterrupt,
                instruction: 0,
                syndrome: 0,
                far: 0,
                pc: self.pc,
            });
            return true;
        }
        false
    }

    /// Commit a latched asynchronous interrupt at the current instruction
    /// boundary.  `poll_interrupt` deliberately only records the source so
    /// callers can inspect it; this helper performs the architectural entry
    /// (SPSR/ELR save, target EL, interrupt masking, and vector selection).
    /// Synchronous exceptions and GuestHalt are left for the existing
    /// explicit `take_pending_exception` path.
    ///
    /// Design note: only asynchronous exceptions (timer/external) are
    /// auto-committed here.  Synchronous exceptions (DataAbort,
    /// TranslationFault, etc.) remain latched so the caller can inspect
    /// ESR/FAR/instruction before deciding whether to enter the vector.
    /// This is the correct boundary for a bounded diagnostic runner: the
    /// architectural state is frozen at the faulting instruction and the
    /// host can report the exact syndrome without the guest handler
    /// overwriting ESR/ELR.
    fn commit_pending_interrupt(&mut self) -> Option<GuestException> {
        match self.pending_exception.map(|exception| exception.kind) {
            Some(ExceptionKind::TimerInterrupt | ExceptionKind::ExternalInterrupt | ExceptionKind::FiqInterrupt) => {
                self.take_pending_exception()
            }
            _ => None,
        }
    }

    pub(crate) fn eret(&mut self) -> Result<(), ExceptionKind> {
        let level = self.current_el;
        if level == ExceptionLevel::El0 {
            return Err(ExceptionKind::PrivilegedInstruction);
        }
        let (saved_pstate, saved_pc, _) = self.exception_bank(level);
        let restored_pstate = saved_pstate as u32;
        let restored_el = level_for_mode(restored_pstate)
            .ok_or(ExceptionKind::UndefinedInstruction)?;
        self.sp_el[self.active_sp_index()] = self.sp;
        self.pstate = restored_pstate;
        self.pc = saved_pc;
        self.current_el = restored_el;
        self.sp = self.sp_el[restored_el as usize];
        Ok(())
    }

    fn mmu_address<B: GuestBus>(
        &mut self,
        bus: &mut B,
        address: u64,
        size: usize,
        access: Access,
    ) -> Result<u64, ExceptionKind> {
        if size > 1 && address & (size as u64 - 1) != 0 && self.sys.sctlr_el1 & SCTLR_A != 0 {
            return Err(ExceptionKind::AlignmentFault);
        }
        let translation = self
            .mmu
            .translate(address, access, self.current_el, |physical| {
                bus.read(physical, 8, Access::Read).ok()
            })
            .map_err(map_mmu_fault)?;
        Ok(translation.pa)
    }

    fn fetch32<B: GuestBus>(&mut self, bus: &mut B) -> Result<u32, ExceptionKind> {
        if self.pc & 3 != 0 {
            return Err(ExceptionKind::AlignmentFault);
        }
        let physical = self.mmu_address(bus, self.pc, 4, Access::Execute)?;
        bus.read(physical, 4, Access::Execute)
            .map(|value| value as u32)
            .map_err(|_| ExceptionKind::InstructionAbort)
    }

    fn pair_memory<B: GuestBus>(&mut self, bus: &mut B, word: u32)
        -> Result<(), (ExceptionKind, u64)> {
        let (opc, mode, load) = (word >> 30, (word >> 23) & 3, word & (1 << 22) != 0);
        let (rt, rn, rt2) = (word & 31, (word >> 5) & 31, (word >> 10) & 31);
        let wback = mode != 2;
        if word & (1 << 26) != 0 || !matches!(opc, 0 | 2) || mode == 0 ||
            (wback && rn != 31 && (rn == rt || rn == rt2)) || (load && rt == rt2) {
            return Err((ExceptionKind::UndefinedInstruction, self.pc));
        }
        let endian = if self.current_el == ExceptionLevel::El0 { 24 } else { 25 };
        if self.current_el as u8 > ExceptionLevel::El1 as u8 || self.mmu.enabled ||
            self.sys.sctlr_el1 & (SCTLR_M | (1 << endian)) != 0 ||
            self.sys.hcr_el2 != 0 || self.sys.scr_el3 != 0 {
            return Err((ExceptionKind::SystemRegisterTrap, self.pc));
        }
        let size = if opc == 2 { 8usize } else { 4 };
        let raw_base = self.read_reg(rn, true);
        let sa = if self.current_el == ExceptionLevel::El0 { 16 } else { 8 };
        if rn == 31 && self.sys.sctlr_el1 & sa != 0 && raw_base & 15 != 0 {
            return Err((ExceptionKind::SpAlignmentFault, raw_base));
        }
        let signed_imm = (((word >> 15) & 127) as i8) << 1 >> 1;
        let offset = (i64::from(signed_imm) * size as i64) as u64;
        let updated_base = raw_base.wrapping_add(offset);
        let address = if mode == 1 { raw_base } else { updated_base };
        if self.sys.sctlr_el1 & SCTLR_A != 0 && address & (size as u64 - 1) != 0 {
            return Err((ExceptionKind::AlignmentFault, address));
        }
        let source = [self.read_reg(rt, false), self.read_reg(rt2, false)];
        let (ram, base) = bus.pair_ram().ok_or((ExceptionKind::SystemRegisterTrap, self.pc))?;
        let mut indices = [0usize; 2];
        for (element, index) in indices.iter_mut().enumerate() {
            let at = address.checked_add((element * size) as u64)
                .ok_or((ExceptionKind::DataAbort, address.wrapping_add(size as u64)))?;
            *index = at.checked_sub(base).and_then(|v| usize::try_from(v).ok())
                .filter(|v| ram.len() >= size && *v <= ram.len() - size)
                .ok_or((ExceptionKind::DataAbort, at))?;
        }
        if load {
            let values = [read_width(ram, indices[0], size).unwrap(),
                          read_width(ram, indices[1], size).unwrap()];
            self.write_reg(rt, values[0], false, size == 8);
            self.write_reg(rt2, values[1], false, size == 8);
        } else {
            for element in 0..2 {
                let written = write_width(ram, indices[element], size, source[element]);
                debug_assert!(written);
                self.exclusive.clear_on_store(address + (element * size) as u64, size as u8);
            }
        }
        if wback { self.write_reg(rn, updated_base, true, true); }
        self.pc = self.pc.wrapping_add(4);
        Ok(())
    }

    fn read_reg(&self, index: u32, sp_allowed: bool) -> u64 {
        if index == 31 {
            if sp_allowed {
                self.sp
            } else {
                0
            }
        } else {
            self.x[index as usize]
        }
    }

    fn write_reg(&mut self, index: u32, value: u64, sp_allowed: bool, wide: bool) {
        let value = if wide { value } else { value as u32 as u64 };
        if index == 31 {
            if sp_allowed {
                self.sp = value;
            }
        } else {
            self.x[index as usize] = value;
        }
    }

    fn set_nzcv(&mut self, value: u64, wide: bool, carry: bool, overflow: bool) {
        let width = if wide { 64 } else { 32 };
        let mask = if wide { u64::MAX } else { u64::from(u32::MAX) };
        let value = value & mask;
        let sign = 1u64 << (width - 1);
        self.pstate &= !(PSTATE_N | PSTATE_Z | PSTATE_C | PSTATE_V);
        if value & sign != 0 {
            self.pstate |= PSTATE_N;
        }
        if value == 0 {
            self.pstate |= PSTATE_Z;
        }
        if carry {
            self.pstate |= PSTATE_C;
        }
        if overflow {
            self.pstate |= PSTATE_V;
        }
    }

    fn execute_one<B: GuestBus>(&mut self, bus: &mut B) -> StepResult {
        let pc = self.pc;
        let word = match self.fetch32(bus) {
            Ok(value) => value,
            Err(kind) => {
                self.raise(GuestException {
                    kind,
                    instruction: 0,
                    syndrome: 0,
                    far: pc,
                    pc,
                });
                return StepResult::Exception(kind);
            }
        };
        let rd = word & 31;
        let rn = (word >> 5) & 31;
        let wide = word & (1 << 31) != 0;

        // The same software PAC implementation serves the EFI native-code
        // dispatcher and this architectural path. Existing MMU/system-register
        // semantics below retain ownership of SCTLR/TCR/ID access here.
        let syskey = (word >> 5) & 0x7fff;
        if !matches!(syskey, 0x4080 | 0x4102 | 0x4031)
            || word & 0xffc00000 != 0xd5000000
        {
            let mut context = PauthContext::new(self.x, self.sp, self.pc,
                self.sys.sctlr_el1, self.sys.tcr_el1, self.pauth, self.current_el as u32);
            if let Some(result) = pauth::step(&mut context, word) {
                if result.is_ok() {
                    self.x = context.x; self.sp = context.sp; self.pc = context.pc;
                    self.pauth = context.state();
                    return StepResult::Continue;
                }
                self.raise(GuestException { kind: ExceptionKind::UndefinedInstruction,
                    instruction: word, syndrome: word as u64, far: pc, pc });
                return StepResult::Exception(ExceptionKind::UndefinedInstruction);
            }
        }

        if word == 0xd503201f {
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }
        if word & 0xffe0001f == 0xd4400000 {
            if (word >> 5) & 0xffff == 0 {
                self.pc = pc.wrapping_add(4);
                return StepResult::Halt;
            }
            self.raise(GuestException {
                kind: ExceptionKind::UndefinedInstruction,
                instruction: word,
                syndrome: word as u64,
                far: pc,
                pc,
            });
            return StepResult::Exception(ExceptionKind::UndefinedInstruction);
        }
        if word == 0xd503207f || word == 0xd503205f {
            if self.current_el == ExceptionLevel::El0 {
                self.raise(GuestException {
                    kind: ExceptionKind::PrivilegedInstruction,
                    instruction: word,
                    syndrome: word as u64,
                    far: pc,
                    pc,
                });
                return StepResult::Exception(ExceptionKind::PrivilegedInstruction);
            }
            self.pc = pc.wrapping_add(4);
            self.waiting = true;
            return StepResult::Wait;
        }
        // DAIF and SPSel are architectural state updates, not generic system
        // register accesses.  They are accepted at EL1+ and rejected at EL0.
        if word & 0xffff_f0ff == 0xd503_40df
            || word & 0xffff_f0ff == 0xd503_40ff
        {
            if self.current_el == ExceptionLevel::El0 {
                self.raise(GuestException {
                    kind: ExceptionKind::PrivilegedInstruction,
                    instruction: word,
                    syndrome: word as u64,
                    far: pc,
                    pc,
                });
                return StepResult::Exception(ExceptionKind::PrivilegedInstruction);
            }
            let mask = u32::from(((word >> 8) & 0xf) as u8) << 6;
            if word & 0xff == 0xdf {
                self.pstate |= mask;
            } else {
                self.pstate &= !mask;
            }
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }
        if word & !0x100 == 0xd500_40bf {
            if self.current_el == ExceptionLevel::El0 {
                self.raise(GuestException {
                    kind: ExceptionKind::PrivilegedInstruction,
                    instruction: word,
                    syndrome: word as u64,
                    far: pc,
                    pc,
                });
                return StepResult::Exception(ExceptionKind::PrivilegedInstruction);
            }
            self.sp_el[self.active_sp_index()] = self.sp;
            if word & 0x100 != 0 {
                self.pstate |= 1;
            } else {
                self.pstate &= !1;
            }
            self.sp = self.sp_el[self.active_sp_index()];
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }
        // The bounded TLB has no separate global/set-associative state, so
        // both VMALLE1 and ASIDE1 invalidate the complete local array.  This
        // preserves the architectural ordering boundary without pretending
        // to implement a host TLB flush.
        let tlbi_vmalle1 = word == 0xd508_871f;
        let tlbi_aside1 = word & 0xffff_fc1f == 0xd508_8740;
        if tlbi_vmalle1 || tlbi_aside1 {
            if self.current_el == ExceptionLevel::El0 {
                self.raise(GuestException {
                    kind: ExceptionKind::PrivilegedInstruction,
                    instruction: word,
                    syndrome: word as u64,
                    far: pc,
                    pc,
                });
                return StepResult::Exception(ExceptionKind::PrivilegedInstruction);
            }
            self.mmu.invalidate();
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }
        if word == 0xd69f03e0 {
            match self.eret() {
                Ok(()) => return StepResult::Continue,
                Err(kind) => {
                    self.raise(GuestException {
                        kind,
                        instruction: word,
                        syndrome: word as u64,
                        far: pc,
                        pc,
                    });
                    return StepResult::Exception(kind);
                }
            }
        }
        if word & 0xffe0001f == 0xd4000001 {
            self.raise(GuestException {
                kind: ExceptionKind::SupervisorCall,
                instruction: word,
                syndrome: word as u64,
                far: 0,
                pc,
            });
            return StepResult::Exception(ExceptionKind::SupervisorCall);
        }

        // Single-copy atomic instructions.  The reservation is recorded in
        // guest-physical space, after translation, so two virtual aliases of
        // one page cannot accidentally create two independent reservations.
        // Acquire/release variants share the same single-core ordering here;
        // DMB/DSB/ISB below are explicit architectural boundaries and retire
        // as ordered no-ops until the SMP scheduler supplies another core.
        let exclusive_class = word & 0x3fc0_7c00;
        let exclusive_load = exclusive_class == 0x0840_7c00;
        let exclusive_store = exclusive_class == 0x0800_7c00;
        if exclusive_load || exclusive_store {
            let size = if wide { 8usize } else { 4usize };
            let address = self.read_reg(rn, true);
            if address & (size as u64 - 1) != 0 {
                self.raise(GuestException {
                    kind: ExceptionKind::AlignmentFault,
                    instruction: word,
                    syndrome: access_syndrome(if exclusive_load {
                        Access::Read
                    } else {
                        Access::Write
                    }),
                    far: address,
                    pc,
                });
                return StepResult::Exception(ExceptionKind::AlignmentFault);
            }
            let physical = match self.mmu_address(
                bus,
                address,
                size,
                if exclusive_load { Access::Read } else { Access::Write },
            ) {
                Ok(value) => value,
                Err(kind) => {
                    self.raise(GuestException {
                        kind,
                        instruction: word,
                        syndrome: access_syndrome(if exclusive_load {
                            Access::Read
                        } else {
                            Access::Write
                        }),
                        far: address,
                        pc,
                    });
                    return StepResult::Exception(kind);
                }
            };
            if exclusive_load {
                let value = match bus.read(physical, size, Access::Read) {
                    Ok(value) => value,
                    Err(kind) => {
                        self.raise(GuestException {
                            kind,
                            instruction: word,
                            syndrome: access_syndrome(Access::Read),
                            far: address,
                            pc,
                        });
                        return StepResult::Exception(kind);
                    }
                };
                self.write_reg(rd, value, false, wide);
                self.exclusive
                    .reserve(physical, size as u8, self.affinity);
            } else {
                let status = (word >> 16) & 31;
                let value = self.read_reg(rd, false);
                let succeeds = self
                    .exclusive
                    .succeeds(physical, size as u8, self.affinity);
                if succeeds {
                    if let Err(kind) = bus.write(physical, size, value, Access::Write) {
                        self.raise(GuestException {
                            kind,
                            instruction: word,
                            syndrome: access_syndrome(Access::Write),
                            far: address,
                            pc,
                        });
                        return StepResult::Exception(kind);
                    }
                }
                // A store-exclusive consumes the reservation whether it
                // succeeds or fails.  A normal store below uses the same
                // physical-address invalidation rule.
                self.exclusive.valid = false;
                self.write_reg(status, if succeeds { 0 } else { 1 }, false, false);
            }
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }

        // The architectural barrier encodings have no observable effect in a
        // single-core reference run, but accepting them is important: kernel
        // atomics routinely bracket exclusive sequences with these barriers.
        // The option field is intentionally ignored; the instruction still
        // retires in program order and never crosses the host ABI boundary.
        if word & 0xffff_f0ff == 0xd503_30bf
            || word & 0xffff_f0ff == 0xd503_309f
            || word & 0xffff_f0ff == 0xd503_30df
        {
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }

        if word & 0xffff_f0ff == 0xd503_305f {
            self.exclusive.valid = false;
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }

        // MRS/MSR use the same system-register key. Unknown keys are a system
        // register trap, while EL0 access to an EL1 register is a privilege
        // fault.
        let mrs = word & 0xffe00000 == 0xd5200000;
        let msr = word & 0xffe00000 == 0xd5000000;
        if mrs || msr {
            let Some(reg) = SystemRegister::from_instruction(word) else {
                self.raise(GuestException {
                    kind: ExceptionKind::SystemRegisterTrap,
                    instruction: word,
                    syndrome: word as u64,
                    far: pc,
                    pc,
                });
                return StepResult::Exception(ExceptionKind::SystemRegisterTrap);
            };
            let result = if mrs {
                self.read_sysreg(reg).map(|value| {
                    self.write_reg(rd, value, false, true);
                })
            } else {
                let value = self.read_reg(rd, false);
                self.write_sysreg(reg, value)
            };
            if let Err(fault) = result {
                let kind = if fault == SysRegFault::Undefined {
                    ExceptionKind::UndefinedInstruction
                } else if fault == SysRegFault::Privilege {
                    ExceptionKind::PrivilegedInstruction
                } else {
                    ExceptionKind::SystemRegisterTrap
                };
                self.raise(GuestException {
                    kind,
                    instruction: word,
                    syndrome: word as u64,
                    far: pc,
                    pc,
                });
                return StepResult::Exception(kind);
            }
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }

        if word&0x1f800000==0x12000000 {
            let Some(mask)=logical_immediate(word) else {
                self.raise(GuestException {kind:ExceptionKind::UndefinedInstruction,
                    instruction:word,syndrome:word as u64,far:pc,pc});
                return StepResult::Exception(ExceptionKind::UndefinedInstruction);
            };
            let op=(word>>29)&3;let input=self.read_reg(rn,false);
            let mut result=match op {1=>input|mask,2=>input^mask,_=>input&mask};
            if !wide {result&=0xffff_ffff;}
            self.write_reg(rd,result,op!=3,wide);
            if op==3 {
                let n=((result>>(if wide {63} else {31}))&1) as u32;
                self.pstate=(self.pstate&!0xf0000000)|(n<<31)|((u32::from(result==0))<<30);
            }
            self.pc=pc.wrapping_add(4);return StepResult::Continue;
        }
        // MOVZ/MOVK, 32- and 64-bit forms.
        let move_op = word & 0x7f800000;
        if move_op == 0x52800000 || move_op == 0x72800000 {
            let shift = ((word >> 21) & 3) * 16;
            if !wide && shift >= 32 {
                self.raise(GuestException {
                    kind: ExceptionKind::UndefinedInstruction,
                    instruction: word,
                    syndrome: word as u64,
                    far: pc,
                    pc,
                });
                return StepResult::Exception(ExceptionKind::UndefinedInstruction);
            }
            let mask = u64::from(0xffffu32) << shift;
            let immediate = u64::from((word >> 5) & 0xffff) << shift;
            let value = if move_op == 0x72800000 {
                (self.read_reg(rd, false) & !mask) | immediate
            } else {
                immediate
            };
            self.write_reg(rd, value, false, wide);
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }

        // ADD/SUB immediate, including the SP form when S=0.
        if word & 0x1f000000 == 0x11000000 {
            let subtract = word & (1 << 30) != 0;
            let set_flags = word & (1 << 29) != 0;
            let shift = if word & (1 << 22) != 0 { 12 } else { 0 };
            let immediate = u64::from((word >> 10) & 0xfff) << shift;
            let left = self.read_reg(rn, true);
            let (value, carry, overflow) = add_sub(left, immediate, subtract, wide);
            self.write_reg(rd, value, !set_flags, wide);
            if set_flags {
                self.set_nzcv(value, wide, carry, overflow);
            }
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }

        // ADD/SUB shifted register, LSL/LSR/ASR at the operand width.
        if word & 0x1f200000 == 0x0b000000 {
            let subtract = word & (1 << 30) != 0;
            let set_flags = word & (1 << 29) != 0;
            let shift_type = (word >> 22) & 3;
            let amount = (word >> 10) & 0x3f;
            if shift_type == 3 || (!wide && amount >= 32) {
                self.raise(GuestException {
                    kind: ExceptionKind::UndefinedInstruction,
                    instruction: word,
                    syndrome: word as u64,
                    far: pc,
                    pc,
                });
                return StepResult::Exception(ExceptionKind::UndefinedInstruction);
            }
            let mut right=self.read_reg((word >> 16) & 31, false);
            if !wide {right&=0xffff_ffff;}
            right=match shift_type {
                0=>right<<amount,
                1=>right>>amount,
                _=>if wide {((right as i64)>>amount) as u64}
                    else {(((right as u32 as i32)>>amount) as u32) as u64},
            };
            let left = self.read_reg(rn, false);
            let (value, carry, overflow) = add_sub(left, right, subtract, wide);
            self.write_reg(rd, value, false, wide);
            if set_flags {
                self.set_nzcv(value, wide, carry, overflow);
            }
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }

        if word & 0x3a000000 == 0x28000000 {
            return match self.pair_memory(bus, word) {
                Ok(()) => StepResult::Continue,
                Err((kind, far)) => {
                    let mut syndrome = match kind {
                        ExceptionKind::DataAbort | ExceptionKind::AlignmentFault => {
                            let fsc = if kind == ExceptionKind::AlignmentFault { ESR_FSC_ALIGNMENT }
                                else { ESR_FSC_TRANSLATION_L3 };
                            (expected_ec(kind, self.current_el).unwrap() << 26) | (1 << 25) | fsc |
                                if word & (1 << 22) == 0 { ESR_ISS_WNR } else { 0 }
                        }
                        _ => word as u64,
                    };
                    if kind == ExceptionKind::SpAlignmentFault { syndrome = (0x26 << 26) | (1 << 25); }
                    self.raise(GuestException { kind, instruction: word, syndrome, far, pc });
                    StepResult::Exception(kind)
                }
            };
        }

        // Unsigned-immediate LDR/STR for byte, halfword, word, and X forms.
        if word & 0x3b000000 == 0x39000000 {
            let size_code = (word >> 30) & 3;
            let size = 1usize << size_code;
            let address = self
                .read_reg(rn, true)
                .checked_add(u64::from((word >> 10) & 0xfff) << size_code);
            let Some(address) = address else {
                self.raise(GuestException {
                    kind: ExceptionKind::DataAbort,
                    instruction: word,
                    syndrome: access_syndrome(if word & (1 << 22) != 0 {
                        Access::Read
                    } else {
                        Access::Write
                    }),
                    far: pc,
                    pc,
                });
                return StepResult::Exception(ExceptionKind::DataAbort);
            };
            let load = word & (1 << 22) != 0;
            let access = if load { Access::Read } else { Access::Write };
            let physical = match self.mmu_address(bus, address, size, access) {
                Ok(value) => value,
                Err(kind) => {
                    self.raise(GuestException {
                        kind,
                        instruction: word,
                        syndrome: access_syndrome(access),
                        far: address,
                        pc,
                    });
                    return StepResult::Exception(kind);
                }
            };
            if load {
                let value = match bus.read(physical, size, Access::Read) {
                    Ok(value) => value,
                    Err(kind) => {
                        self.raise(GuestException {
                            kind,
                            instruction: word,
                            syndrome: access_syndrome(Access::Read),
                            far: address,
                            pc,
                        });
                        return StepResult::Exception(kind);
                    }
                };
                self.write_reg(rd, value, false, size == 8);
            } else {
                let value = self.read_reg(rd, false);
                if let Err(kind) = bus.write(physical, size, value, Access::Write) {
                    self.raise(GuestException {
                        kind,
                        instruction: word,
                        syndrome: access_syndrome(Access::Write),
                        far: address,
                        pc,
                    });
                    return StepResult::Exception(kind);
                }
                self.exclusive.clear_on_store(physical, size as u8);
            }
            self.pc = pc.wrapping_add(4);
            return StepResult::Continue;
        }

        if word & 0xfc000000 == 0x14000000 {
            let offset = sign_extend(u64::from(word & 0x03ff_ffff), 26) << 2;
            self.pc = pc.wrapping_add(offset as u64);
            return StepResult::Continue;
        }

        // Unconditional branch (register): BR, BLR, RET.
        if (word & 0xFE1F_FC00) == 0xD61F_0000 {
            let rn_val = (word >> 5) & 31;
            let target = self.read_reg(rn_val, true);
            // BLR (opc=001) saves return address in X30.
            if word & 0x0020_0000 != 0 {
                self.x[30] = pc.wrapping_add(4);
            }
            self.pc = target;
            return StepResult::Continue;
        }

        if word & 0x7e000000 == 0x34000000 {
            let value = self.read_reg(rd, false);
            let nonzero = word & (1 << 24) != 0;
            let offset = sign_extend(u64::from((word >> 5) & 0x7ffff), 19) << 2;
            let take = if nonzero { value != 0 } else { value == 0 };
            self.pc = if take {
                pc.wrapping_add(offset as u64)
            } else {
                pc.wrapping_add(4)
            };
            return StepResult::Continue;
        }

        self.raise(GuestException {
            kind: ExceptionKind::UndefinedInstruction,
            instruction: word,
            syndrome: word as u64,
            far: pc,
            pc,
        });
        StepResult::Exception(ExceptionKind::UndefinedInstruction)
    }

    /// Execute code from a caller-owned buffer with a hard architectural
    /// instruction budget. Code is copied into guest RAM so that instruction
    /// fetches, page walks, and data accesses all use the same guest address
    /// space; the caller's original code buffer remains read-only.
    pub(crate) fn run_bounded(
        &mut self,
        code: &[u8],
        ram: &mut [u8],
        budget: u64,
    ) -> ArchRunResult {
        self.run_bounded_with_hook(code, ram, budget, |_| {})
    }

    /// Execute a bounded guest while notifying the enclosing machine after
    /// each committed instruction.  The hook observes architectural state
    /// only after the instruction and counter update, so a device graph can
    /// mirror timer/IRQ state without participating in decode or host-code
    /// emission.  It is intentionally synchronous and allocation-free.
    pub(crate) fn run_bounded_with_hook<F>(
        &mut self,
        code: &[u8],
        ram: &mut [u8],
        budget: u64,
        mut after_step: F,
    ) -> ArchRunResult
    where
        F: FnMut(&Self),
    {
        let mut bus = RamBus::new(ram);
        self.run_bounded_with_bus(code, &mut bus, budget, |state| after_step(state))
    }

    /// Execute through a machine-owned bus.  The decoder and architectural
    /// state are shared with the RAM-only reference path; only address-space
    /// ownership changes.  This is the integration seam for the native M1
    /// graph and later device implementations.
    pub(crate) fn run_bounded_with_bus<B, F>(
        &mut self,
        code: &[u8],
        bus: &mut B,
        budget: u64,
        after_step: F,
    ) -> ArchRunResult
    where
        B: GuestBus,
        F: FnMut(&Self),
    {
        if !self.state_valid()
            || code.len() < 4
            || code.len() & 3 != 0
            || budget == 0
            || !bus.load_code(code)
        {
            return ArchRunResult {
                status: ArchRunStatus::Input,
                exception: None,
                retired: self.retired,
                pc: self.pc,
            };
        }
        // `retired` is the consumption for this bounded invocation, not a
        // lifetime counter.  A caller may reuse the architectural state for
        // another bounded guest after a halt or exception; carrying the old
        // count into the new budget would skip execution or report a stale
        // result.
        self.retired = 0;
        self.pc = 0;
        self.pending_exception = None;
        self.exception_target = None;
        self.exception_from_lower_el = false;
        self.exception_vector = 0;
        self.waiting = false;
        self.run_loaded_with_bus(bus, budget, after_step)
    }

    /// Enter an already loaded physical image on the x86 EFI execution path.
    /// The RAM slice is guest backing, never a host-address identity mapping.
    pub(crate) fn run_boot_bounded(
        &mut self, ram: &mut [u8], ram_base: u64, entry: u64,
        args: u64, stack: u64, budget: u64,
    ) -> ArchRunResult {
        let end = ram_base.checked_add(ram.len() as u64);
        if budget == 0 || ram_base & 0x3fff != 0 || entry & 3 != 0
            || args & 7 != 0 || stack & 15 != 0
            || end.is_none_or(|end| entry < ram_base || entry.checked_add(4).is_none_or(|v| v > end)
                || args < ram_base || args >= end || stack <= ram_base || stack > end)
        {
            return ArchRunResult { status: ArchRunStatus::Input, exception: None,
                retired: 0, pc: self.pc };
        }
        *self = Self::reset(self.affinity);
        self.pc = entry;
        self.x[0] = args;
        self.sp = stack;
        self.sp_el[1] = stack;
        self.pstate |= PSTATE_D | PSTATE_A | PSTATE_I | PSTATE_F;
        let mut bus = RamBus { ram, base: ram_base };
        self.run_loaded_with_bus(&mut bus, budget, |_| {})
    }

    fn run_loaded_with_bus<B: GuestBus, F: FnMut(&Self)>(
        &mut self, bus: &mut B, budget: u64, mut after_step: F,
    ) -> ArchRunResult {
        while self.retired < budget {
            if self.poll_interrupt() {
                let pending = self.pending_exception;
                let exception = self.commit_pending_interrupt().or(pending);
                return ArchRunResult {
                    status: ArchRunStatus::Exception,
                    exception,
                    retired: self.retired,
                    pc: self.pc,
                };
            }
            let result = self.execute_one(bus);
            match result {
                StepResult::Continue => {
                    self.retired = self.retired.saturating_add(1);
                    self.advance_counter(1);
                    bus.after_instruction(self);
                    after_step(self);
                }
                StepResult::Halt => {
                    self.retired = self.retired.saturating_add(1);
                    self.advance_counter(1);
                    bus.after_instruction(self);
                    after_step(self);
                    return ArchRunResult {
                        status: ArchRunStatus::Halt,
                        exception: None,
                        retired: self.retired,
                        pc: self.pc,
                    };
                }
                StepResult::Wait => {
                    self.retired = self.retired.saturating_add(1);
                    self.advance_counter(1);
                    bus.after_instruction(self);
                    after_step(self);
                    return ArchRunResult {
                        status: ArchRunStatus::Wait,
                        exception: None,
                        retired: self.retired,
                        pc: self.pc,
                    };
                }
                StepResult::Exception(kind) => {
                    return ArchRunResult {
                        status: ArchRunStatus::Exception,
                        exception: self.pending_exception.or(Some(GuestException {
                            kind,
                            instruction: 0,
                            syndrome: 0,
                            far: self.pc,
                            pc: self.pc,
                        })),
                        retired: self.retired,
                        pc: self.pc,
                    };
                }
            }
        }
        ArchRunResult {
            status: ArchRunStatus::Budget,
            exception: None,
            retired: self.retired,
            pc: self.pc,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum StepResult {
    Continue,
    Halt,
    Wait,
    Exception(ExceptionKind),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub(crate) enum ArchRunStatus {
    Halt = 1,
    Budget = 2,
    Exception = 3,
    Wait = 4,
    Input = 5,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct ArchRunResult {
    pub(crate) status: ArchRunStatus,
    pub(crate) exception: Option<GuestException>,
    pub(crate) retired: u64,
    pub(crate) pc: u64,
}

fn map_mmu_fault(fault: MmuFault) -> ExceptionKind {
    match fault {
        MmuFault::AddressSize | MmuFault::Translation | MmuFault::AccessFlag => {
            ExceptionKind::TranslationFault
        }
        MmuFault::Permission => ExceptionKind::PermissionFault,
        MmuFault::Alignment => ExceptionKind::AlignmentFault,
    }
}

fn add_sub(left: u64, right: u64, subtract: bool, wide: bool) -> (u64, bool, bool) {
    let mask = if wide { u64::MAX } else { u64::from(u32::MAX) };
    let left = left & mask;
    let right = right & mask;
    if subtract {
        let value = left.wrapping_sub(right) & mask;
        let carry = left >= right;
        let overflow =
            ((left ^ right) & (left ^ value) & (1u64 << if wide { 63 } else { 31 })) != 0;
        (value, carry, overflow)
    } else {
        let wide_sum = (left as u128) + (right as u128);
        let value = (wide_sum as u64) & mask;
        let carry = (wide_sum >> if wide { 64 } else { 32 }) != 0;
        let sign = 1u64 << if wide { 63 } else { 31 };
        let overflow = (!(left ^ right) & (left ^ value) & sign) != 0;
        (value, carry, overflow)
    }
}

fn sign_extend(value: u64, bits: u32) -> i64 {
    let shift = 64 - bits;
    ((value << shift) as i64) >> shift
}

fn read_u32(memory: &[u8], address: u64) -> Option<u32> {
    let index = usize::try_from(address).ok()?;
    let bytes = memory.get(index..index.checked_add(4)?)?;
    Some(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
}

fn read_u64(memory: &[u8], address: u64) -> Option<u64> {
    let index = usize::try_from(address).ok()?;
    let bytes = memory.get(index..index.checked_add(8)?)?;
    Some(u64::from_le_bytes([
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
    ]))
}

fn read_width(memory: &[u8], index: usize, width: usize) -> Option<u64> {
    let bytes = memory.get(index..index.checked_add(width)?)?;
    let mut value = 0u64;
    for (shift, byte) in bytes.iter().enumerate() {
        value |= u64::from(*byte) << (shift * 8);
    }
    Some(value)
}

fn write_width(memory: &mut [u8], index: usize, width: usize, value: u64) -> bool {
    let Some(bytes) = memory.get_mut(index..index.saturating_add(width)) else {
        return false;
    };
    for (shift, byte) in bytes.iter_mut().enumerate() {
        *byte = (value >> (shift * 8)) as u8;
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pair_word(bytes: usize, read: bool, mode: u32, displacement: i32,
                 rn: u32, rt: u32, rt2: u32) -> u32 {
        0x28000000 | if bytes == 8 { 1 << 31 } else { 0 } | (mode << 23) |
            (u32::from(read) << 22) | ((displacement as u32 & 127) << 15) |
            (rt2 << 10) | (rn << 5) | rt
    }

    #[test]
    fn integer_pair_modes_match_independent_byte_memory() {
        let base = 0x80000000u64;
        for bytes in [4usize,8] { for read in [false,true] { for mode in 1..=3 {
        for displacement in -64..64 { for rn in [3,31] {
            let mut ram=[0u8;4096];
            for (at, byte) in ram.iter_mut().enumerate() { *byte=(at*37+19) as u8; }
            let mut cpu=GuestCpuState::reset(0);
            cpu.pc=base;cpu.sp=base+1024;cpu.x[3]=base+1024;cpu.pstate=0xb00003c5;
            cpu.x[4]=0xfedcba9876543210;cpu.x[5]=0x89abcdef12345678;
            let source=[cpu.x[4],cpu.x[5]];
            let address=(1024i64+if mode==1 {0} else {i64::from(displacement)*bytes as i64}) as usize;
            let mut expected=ram;
            let mut values=[0u64;2];
            for element in 0..2 { for byte in 0..bytes {
                if read { values[element] |= u64::from(expected[address+element*bytes+byte]) << (byte*8); }
                else { expected[address+element*bytes+byte]=(source[element] >> (byte*8)) as u8; }
            }}
            let word=pair_word(bytes,read,mode,displacement,rn,4,5);
            ram[..4].copy_from_slice(&word.to_le_bytes());expected[..4].copy_from_slice(&word.to_le_bytes());
            let result=cpu.execute_one(&mut RamBus{ram:&mut ram,base});
            assert_eq!(result,StepResult::Continue);
            assert_eq!(ram,expected);
            assert_eq!([cpu.x[4],cpu.x[5]],if read {values} else {source});
            let updated=(base+1024).wrapping_add(if mode==2 {0} else {(i64::from(displacement)*bytes as i64) as u64});
            assert_eq!(cpu.sp,if rn==31 {updated} else {base+1024});
            assert_eq!(cpu.x[3],if rn==3 {updated} else {base+1024});
            assert_eq!(cpu.pc,base+4);assert_eq!(cpu.pstate,0xb00003c5);
        }}}}}
    }

    #[test]
    fn pair_faults_preserve_memory_writeback_and_encode_alignment() {
        for read in [false,true] { for mode in 1..=3 { for address in [248u64,252,u64::MAX-7] {
            let mut ram=[0xa5u8;256];let mut cpu=GuestCpuState::reset(0);
            cpu.x[3]=address;cpu.x[4]=0x1111;cpu.x[5]=0x2222;
            let word=pair_word(8,read,mode,0,3,4,5);ram[..4].copy_from_slice(&word.to_le_bytes());
            let before=ram;
            assert_eq!(cpu.execute_one(&mut RamBus::new(&mut ram)),StepResult::Exception(ExceptionKind::DataAbort));
            assert_eq!(ram,before);assert_eq!(cpu.pc,0);
            assert_eq!([cpu.x[3],cpu.x[4],cpu.x[5]],[address,0x1111,0x2222]);
            assert_eq!(cpu.pending_exception.unwrap().far,if address==248 {256} else {address});
            assert_eq!(cpu.sys.esr_el1,(0x25<<26)|(1<<25)|if read {0} else {64}|7);
        }}}
        for el in [ExceptionLevel::El0,ExceptionLevel::El1] { for sa in [false,true] { for a in [false,true] {
            let mut ram=[0u8;256];let mut cpu=GuestCpuState::reset(0);
            cpu.current_el=el;cpu.pstate=if el==ExceptionLevel::El0 {0} else {5};cpu.sp=129;
            cpu.sys.sctlr_el1=if sa {if el==ExceptionLevel::El0 {16} else {8}} else {0} | if a {2} else {0};
            let word=pair_word(8,false,2,0,31,4,5);ram[..4].copy_from_slice(&word.to_le_bytes());
            let before=ram;
            let result=cpu.execute_one(&mut RamBus::new(&mut ram));
            if sa || a {
                assert_eq!(result,StepResult::Exception(if sa {ExceptionKind::SpAlignmentFault} else {ExceptionKind::AlignmentFault}));
                assert_eq!(ram,before);assert_eq!(cpu.sp,129);assert_eq!(cpu.pc,0);
                if sa {assert_eq!(cpu.sys.esr_el1,(0x26<<26)|(1<<25));}
            } else {assert_eq!(result,StepResult::Continue);}
        }}}
    }

    #[test]
    fn pair_rejects_unmodeled_bus_mmu_and_overlaps_without_side_effects() {
        struct DeviceBus {word:u32,reads:usize}
        impl GuestBus for DeviceBus {
            fn load_code(&mut self,_:&[u8])->bool {false}
            fn read(&mut self,_:u64,_:usize,access:Access)->Result<u64,ExceptionKind>{
                assert_eq!(access,Access::Execute);self.reads+=1;Ok(u64::from(self.word))
            }
            fn write(&mut self,_:u64,_:usize,_:u64,_:Access)->Result<(),ExceptionKind>{panic!("pair touched MMIO")}
        }
        let word=pair_word(8,false,3,1,3,4,5);
        let mut device=DeviceBus{word,reads:0};let mut cpu=GuestCpuState::reset(0);cpu.x[3]=128;
        assert_eq!(cpu.execute_one(&mut device),StepResult::Exception(ExceptionKind::SystemRegisterTrap));
        assert_eq!(device.reads,1);assert_eq!(cpu.x[3],128);assert_eq!(cpu.pc,0);
        for regime in 0..6 {
            let mut cpu=GuestCpuState::reset(0);let mut ram=[0x55;256];let before=ram;
            match regime {0=>cpu.mmu.enabled=true,1=>cpu.sys.sctlr_el1=1,2=>cpu.sys.sctlr_el1=1<<25,
                3=>cpu.sys.hcr_el2=8,4=>cpu.sys.scr_el3=2,_=>cpu.current_el=ExceptionLevel::El2}
            assert_eq!(cpu.pair_memory(&mut RamBus::new(&mut ram),word),Err((ExceptionKind::SystemRegisterTrap,0)));
            assert_eq!(ram,before);
        }
        for invalid in [pair_word(8,false,3,1,4,4,5),pair_word(8,true,1,1,4,4,5),
            pair_word(8,true,2,0,3,4,4),pair_word(8,true,0,0,3,4,5),
            pair_word(8,true,2,0,3,4,5)|(1<<26),pair_word(4,true,2,0,3,4,5)|(1<<30)] {
            let mut cpu=GuestCpuState::reset(0);let mut ram=[0x55;256];
            ram[..4].copy_from_slice(&invalid.to_le_bytes());let before=ram;
            assert_eq!(cpu.pair_memory(&mut RamBus::new(&mut ram),invalid),Err((ExceptionKind::UndefinedInstruction,0)));
            assert_eq!(ram,before);
            assert_eq!(cpu.execute_one(&mut RamBus::new(&mut ram)),StepResult::Exception(ExceptionKind::UndefinedInstruction));
            assert_eq!(cpu.sys.esr_el1,0);
            assert_eq!(cpu.pending_exception.unwrap().instruction,invalid);
            assert_eq!(ram,before);assert_eq!(cpu.pc,0);
        }
        assert_eq!(exception_syndrome(ExceptionKind::InstructionAbort,ExceptionLevel::El1,
            u64::from(pair_word(8,false,2,0,3,4,5))),(ESR_EC_IABT_SAME<<26)|7);
        let mut cpu=GuestCpuState::reset(0);let mut ram=[0u8;256];cpu.x[3]=128;cpu.x[4]=0x42;
        cpu.exclusive.reserve(136,8,0);
        assert_eq!(cpu.pair_memory(&mut RamBus::new(&mut ram),pair_word(8,false,2,0,3,4,4)),Ok(()));
        assert!(!cpu.exclusive.valid);assert_eq!(&ram[128..136],&ram[136..144]);
        cpu.pc=0;cpu.sp=0x9870;
        assert_eq!(cpu.pair_memory(&mut RamBus::new(&mut ram),pair_word(8,true,2,0,3,3,31)),Ok(()));
        assert_eq!(cpu.x[3],0x42);assert_eq!(cpu.sp,0x9870);
    }

    #[test]
    fn shifted_arithmetic_matches_unsigned_wide_math_at_every_shift_count() {
        let pairs=[(0,1),(u64::MAX,u64::MAX),(0x7fffffff7fffffff,1),
            (0x8000000080000000,0xffffffff80000001)];
        for width in [32u32,64] {for shift in 0..3 {for amount in 0..width {
        for sub in 0..2 {for flags in 0..2 {for (raw_a,raw_b) in pairs {
            let mask=if width==64 {u64::MAX} else {0xffffffff};let sign=1u64<<(width-1);
            let a=raw_a&mask;let mut b=raw_b&mask;
            if shift==0 {b=(b<<amount)&mask;} else {
                let neg=shift==2 && b&sign!=0;b>>=amount;
                if neg && amount!=0 {b|=mask^(mask>>amount);}
            }
            let result=if sub!=0 {a.wrapping_sub(b)} else {a.wrapping_add(b)}&mask;
            let carry=if sub!=0 {a>=b} else {(u128::from(a)+u128::from(b))>>width!=0};
            let overflow=(a^result)&(if sub!=0 {a^b} else {!(a^b)})&sign!=0;
            let nzcv=if flags!=0 {(u32::from(result&sign!=0)<<3)|(u32::from(result==0)<<2)|
                (u32::from(carry)<<1)|u32::from(overflow)} else {15};
            let word=0x0b000002|(u32::from(width==64)<<31)|(sub<<30)|(flags<<29)|
                (shift<<22)|(1<<16)|(amount<<10);
            let mut cpu=GuestCpuState::reset(0);cpu.x[0]=raw_a;cpu.x[1]=raw_b;cpu.sp=0x9876;
            cpu.pstate|=0xf00003c0;let mut bytes=word.to_le_bytes();
            assert_eq!(cpu.execute_one(&mut RamBus::new(&mut bytes)),StepResult::Continue);
            assert_eq!(cpu.x[2],result);assert_eq!(cpu.sp,0x9876);assert_eq!(cpu.pstate>>28,nzcv);
        }}}}}}
        for word in [0x0bc00000u32,0x8bc00000,0x0b008000,0x2b00fc00] {
            let mut cpu=GuestCpuState::reset(0);cpu.x[0]=u64::MAX;
            let mut bytes=word.to_le_bytes();
            assert_eq!(cpu.execute_one(&mut RamBus::new(&mut bytes)),StepResult::Exception(ExceptionKind::UndefinedInstruction));
            assert_eq!(cpu.x[0],u64::MAX);assert_eq!(cpu.pc,0);
        }
    }

    #[test]
    fn logical_masks_and_native_class_semantics_match_circular_bit_runs() {
        for width in [32u32,64] {for size in [2u32,4,8,16,32,64] {
            if size>width {continue;}
            for ones in 1..size {for rot in 0..size {for op in 0..4 {
                let imms=((!(size-1)<<1)&63)|(ones-1);
                let word=0x12000000|(u32::from(width==64)<<31)|(op<<29)|
                    (u32::from(size==64)<<22)|(rot<<16)|(imms<<10)|1;
                let mut mask=0u64;
                for bit in 0..width {if (bit+rot)%size<ones {mask|=1u64<<bit;}}
                assert_eq!(logical_immediate(word),Some(mask));
                let mut cpu=GuestCpuState::reset(0);cpu.x[0]=0x91a2_b3c4_d5e6_f780;
                cpu.pstate|=0xf0000000;
                let mut expected=match op {1=>cpu.x[0]|mask,2=>cpu.x[0]^mask,_=>cpu.x[0]&mask};
                if width==32 {expected&=0xffffffff;}
                let mut bytes=word.to_le_bytes();
                assert_eq!(cpu.execute_one(&mut RamBus::new(&mut bytes)),StepResult::Continue);
                assert_eq!(cpu.x[1],expected);assert_eq!(cpu.pc,4);
                let flags=if op==3 {(((expected>>(width-1))as u32)<<3)|(u32::from(expected==0)<<2)} else {15};
                assert_eq!(cpu.pstate>>28,flags);
            }}}
        }}
        for word in [0x12400000u32,0x9240fc00,0x9200fc00,0x9200f800] {
            assert_eq!(logical_immediate(word),None);
            let mut cpu=GuestCpuState::reset(0);cpu.x[0]=u64::MAX;
            let mut bytes=word.to_le_bytes();
            assert_eq!(cpu.execute_one(&mut RamBus::new(&mut bytes)),
                StepResult::Exception(ExceptionKind::UndefinedInstruction));
            assert_eq!(cpu.pc,0);assert_eq!(cpu.x[0],u64::MAX);
        }
    }

    #[test]
    fn irq_fiq_levels_obey_override_and_pstate_at_each_vector_group() {
        for mode in 0..3 { for daif in 0..4 { for ov in 0..4 { for lines in 0..4 {
            let mut cpu=GuestCpuState::reset(0);
            assert!(cpu.set_exception_level(if mode==0 {0} else {1}));
            cpu.pstate=(if mode==0 {0} else if mode==1 {4} else {5})|(daif<<6)|0xa0000000;
            let saved=cpu.pstate;
            cpu.sp=0x1230;cpu.sp_el[1]=0x3000;cpu.sys.vbar_el1=0x2000;
            cpu.sys.esr_el1=0xabcdef;cpu.sys.far_el1=0x6789;
            let value=(if ov&1!=0 {2<<22} else {0})|(if ov&2!=0 {2<<20} else {0});
            assert!(cpu.platform.configure(1,value));
            cpu.platform.irq=lines&1!=0;cpu.platform.fiq=lines&2!=0;
            let fiq=lines&2!=0 && daif&1==0 && ov&2==0;
            let irq=lines&1!=0 && daif&2==0 && ov&1==0;
            let mut halt=0xd4400000u32.to_le_bytes();
            let result=cpu.run_loaded_with_bus(&mut RamBus::new(&mut halt),2, |_| {});
            assert_eq!(cpu.sys.esr_el1,0xabcdef);assert_eq!(cpu.sys.far_el1,0x6789);
            assert_eq!(cpu.platform.irq,lines&1!=0);assert_eq!(cpu.platform.fiq,lines&2!=0);
            if fiq || irq {
                let kind=if fiq {ExceptionKind::FiqInterrupt} else {ExceptionKind::ExternalInterrupt};
                assert_eq!(result.exception.unwrap().kind,kind);
                let offset=(if mode==0 {0x400} else if mode==1 {0} else {0x200})+
                    if fiq {0x100} else {0x80};
                assert_eq!(cpu.pc,0x2000+offset);assert_eq!(cpu.exception_vector,cpu.pc);
                assert_eq!(cpu.sys.elr_el1,0);assert_eq!(cpu.sys.spsr_el1,u64::from(saved));
                assert_eq!(cpu.retired,0);assert_eq!(cpu.pstate,0xa00003c5);
                assert_eq!(cpu.sp,if mode==2 {0x1230} else {0x3000});
                cpu.pstate=(cpu.pstate&!0xc0)|(saved&0xc0);
                assert!(cpu.poll_interrupt());assert_eq!(cpu.pending_exception.unwrap().kind,kind);
                cpu.take_pending_exception();cpu.platform.irq=false;cpu.platform.fiq=false;
                cpu.pstate&=!0xc0;assert!(!cpu.poll_interrupt());
            } else { assert_eq!(result.status,ArchRunStatus::Halt);assert_eq!(cpu.retired,1); }
        }}}}
    }

    #[test]
    fn platform_write_unmasks_a_pending_level_without_consuming_it() {
        let mut cpu=GuestCpuState::reset(0);cpu.sys.vbar_el1=0x2000;
        assert!(cpu.platform.configure(1,0xa00000));cpu.platform.irq=true;
        let words=[0xd2800000u32,0xd51df500,0xd4400000];
        let mut ram=[0u8;12];for (slot,word) in ram.chunks_exact_mut(4).zip(words) {slot.copy_from_slice(&word.to_le_bytes());}
        let result=cpu.run_loaded_with_bus(&mut RamBus::new(&mut ram),8, |_| {});
        assert_eq!(result.exception.unwrap().kind,ExceptionKind::ExternalInterrupt);
        assert_eq!(cpu.retired,2);assert_eq!(cpu.sys.elr_el1,8);
        assert_eq!(cpu.platform.override_value,0);assert!(cpu.platform.irq);
        for value in [1,1<<20,3<<22,1<<24,u64::MAX] {
            let mut cpu=GuestCpuState::reset(0);assert!(cpu.platform.configure(1,0xa00000));cpu.x[0]=value;
            let mut word=0xd51df500u32.to_le_bytes();
            assert_eq!(cpu.execute_one(&mut RamBus::new(&mut word)),StepResult::Exception(ExceptionKind::SystemRegisterTrap));
            assert_eq!(cpu.pc,0);assert_eq!(cpu.platform.override_value,0xa00000);
        }
        let mut cpu=GuestCpuState::reset(0);
        assert_eq!(cpu.read_sysreg(SystemRegister::PlatformOverride),Err(SysRegFault::Unknown));
        assert!(cpu.platform.configure(1,0));assert!(cpu.set_exception_level(0));
        assert_eq!(cpu.read_sysreg(SystemRegister::PlatformOverride),Err(SysRegFault::Unknown));
    }

    fn thread_instruction(read: bool, op1: u32, op2: u32, rt: u32) -> u32 {
        (if read { 0xd5200000 } else { 0xd5000000 })
            | (3 << 19) | (op1 << 16) | (13 << 12) | (op2 << 5) | rt
    }

    #[test]
    fn thread_register_access_matrix_matches_baseline_aarch64() {
        let regs = [(SystemRegister::TpidrEl0, 3, 2),
                    (SystemRegister::TpidrroEl0, 3, 3),
                    (SystemRegister::TpidrEl1, 0, 4)];
        for el in 0..4 {
            for (reg, op1, op2) in regs {
                let mut cpu = GuestCpuState::reset(0);
                assert_eq!(cpu.read_sysreg(reg), Ok(0));
                assert!(cpu.set_exception_level(el));
                let readable = el != 0 || reg != SystemRegister::TpidrEl1;
                let writable = el != 0 || reg == SystemRegister::TpidrEl0;
                for value in [0, 1, u64::MAX, 1 << 63, 0xfedc_ba98_7654_3210] {
                    let result = cpu.write_sysreg(reg, value);
                    assert_eq!(result, if writable { Ok(()) } else { Err(SysRegFault::Undefined) });
                    assert_eq!(cpu.read_sysreg(reg), if readable {
                        Ok(if writable { value } else { 0 })
                    } else { Err(SysRegFault::Undefined) });
                }
                for read in [false, true] {
                    let mut cpu = GuestCpuState::reset(0);
                    cpu.write_sysreg(reg, 0x8877_6655_4433_2211).unwrap();
                    assert!(cpu.set_exception_level(el));
                    cpu.x[7] = 0xfedc_ba98_7654_3210;
                    cpu.sp = 0x9876;
                    cpu.pstate |= 0xa00003c0;
                    let pstate = cpu.pstate;
                    let word = thread_instruction(read, op1, op2, 7);
                    assert_eq!(SystemRegister::from_instruction(word), Some(reg));
                    let mut bytes = word.to_le_bytes();
                    let allowed = if read { readable } else { writable };
                    let result = cpu.execute_one(&mut RamBus::new(&mut bytes));
                    assert_eq!(result, if allowed { StepResult::Continue }
                        else { StepResult::Exception(ExceptionKind::UndefinedInstruction) });
                    assert_eq!(cpu.pc, if allowed { 4 } else { 0 });
                    assert_eq!(cpu.sp, 0x9876);
                    assert_eq!(cpu.pstate, pstate);
                    if allowed && read { assert_eq!(cpu.x[7], 0x8877_6655_4433_2211); }
                    if allowed && !read { assert_eq!(cpu.read_sysreg(reg), Ok(cpu.x[7])); }
                    if !allowed {
                        assert_eq!(cpu.pending_exception.unwrap().kind, ExceptionKind::UndefinedInstruction);
                    }
                }
            }
        }
    }

    #[test]
    fn thread_values_survive_el_change_and_xzr_never_aliases_sp() {
        let mut cpu = GuestCpuState::reset(0);
        cpu.sp = 0x9876;
        cpu.write_sysreg(SystemRegister::TpidrroEl0, u64::MAX).unwrap();
        assert!(cpu.set_exception_level(0));
        assert_eq!(cpu.read_sysreg(SystemRegister::TpidrroEl0), Ok(u64::MAX));
        assert!(cpu.set_exception_level(1));
        let saved_sp = cpu.sp;
        let mut discard = thread_instruction(true, 3, 3, 31).to_le_bytes();
        assert_eq!(cpu.execute_one(&mut RamBus::new(&mut discard)), StepResult::Continue);
        assert_eq!(cpu.read_sysreg(SystemRegister::TpidrroEl0), Ok(u64::MAX));
        cpu.pc = 0;
        let mut zero = thread_instruction(false, 3, 3, 31).to_le_bytes();
        assert_eq!(cpu.execute_one(&mut RamBus::new(&mut zero)), StepResult::Continue);
        assert_eq!(cpu.read_sysreg(SystemRegister::TpidrroEl0), Ok(0));
        assert_eq!(cpu.sp, saved_sp);
        assert_eq!(GuestCpuState::reset(1).sys.tpidrro_el0, 0);
        for (op1, op2) in [(0, 1), (3, 5), (0, 7)] {
            cpu.pc = 0;
            let mut unknown = thread_instruction(true, op1, op2, 0).to_le_bytes();
            assert_eq!(cpu.execute_one(&mut RamBus::new(&mut unknown)),
                StepResult::Exception(ExceptionKind::SystemRegisterTrap));
            assert_eq!(cpu.pc, 0);
        }
    }

    fn movz(rd: u32, immediate: u32) -> u32 {
        0xd2800000 | ((immediate & 0xffff) << 5) | rd
    }

    fn mrs(rd: u32, reg: SystemRegister) -> u32 {
        let base = match reg {
            SystemRegister::CntpctEl0 => 0xd53b_e020,
            SystemRegister::CntvctEl0 => 0xd53b_e040,
            SystemRegister::CntpCtlEl0 => 0xd53b_e220,
            SystemRegister::CntpCvalEl0 => 0xd53b_e240,
            SystemRegister::CntpTvalEl0 => 0xd53b_e200,
            SystemRegister::CntvCtlEl0 => 0xd53b_e320,
            SystemRegister::CntvCvalEl0 => 0xd53b_e340,
            SystemRegister::CurrentEl => 0xd538_4240,
            SystemRegister::IdAa64Mmfr0El1 => 0xd538_0700,
            SystemRegister::IdAa64Isar1El1 => 0xd538_0620,
            _ => 0xd53b_e000,
        };
        base | rd
    }

    #[test]
    fn reset_and_exception_commit_are_explicit() {
        let mut cpu = GuestCpuState::reset(3);
        assert_eq!(cpu.current_el, ExceptionLevel::El1);
        assert_eq!(cpu.pstate & PSTATE_MODE_MASK, 5);
        assert!(cpu.state_valid());
        assert_eq!(cpu.affinity, 3);
        assert_eq!(cpu.sys.cntfrq_el0, M1_COUNTER_FREQUENCY);
        cpu.raise(GuestException {
            kind: ExceptionKind::DataAbort,
            instruction: 0,
            syndrome: 0x25,
            far: 0x4000,
            pc: 0x1000,
        });
        assert_eq!(
            cpu.pending_exception.unwrap().kind,
            ExceptionKind::DataAbort
        );
        assert_eq!(cpu.sys.far_el1, 0x4000);
        cpu.sys.vbar_el1 = 0x8000;
        cpu.pc = 0x1000;
        assert_eq!(
            cpu.take_pending_exception().unwrap().kind,
            ExceptionKind::DataAbort
        );
        assert_eq!(cpu.pc, 0x8200);
        assert_eq!(cpu.current_el, ExceptionLevel::El1);
        assert_eq!(cpu.exception_vector, 0x8200);
        assert!(cpu.state_valid());
    }

    #[test]
    fn system_register_bank_enforces_el_and_timer_contract() {
        let mut cpu = GuestCpuState::reset(0);
        assert!(cpu.write_sysreg(SystemRegister::CntpCvalEl0, 3).is_ok());
        assert!(cpu.write_sysreg(SystemRegister::CntpCtlEl0, 1).is_ok());
        cpu.advance_counter(3);
        assert_eq!(cpu.read_sysreg(SystemRegister::CntpctEl0).unwrap(), 3);
        assert!(cpu.timer.pending());
        assert!(cpu.set_exception_level(ExceptionLevel::El0 as u8));
        assert_eq!(
            cpu.read_sysreg(SystemRegister::SctlrEl1),
            Err(SysRegFault::Privilege)
        );
    }

    #[test]
    fn el2_and_el3_register_banks_and_vectors_are_not_el1_aliases() {
        let mut cpu = GuestCpuState::reset(0);
        assert_eq!(
            cpu.read_sysreg(SystemRegister::HcrEl2),
            Err(SysRegFault::Privilege)
        );
        assert!(cpu.set_exception_level(ExceptionLevel::El2 as u8));
        assert!(cpu.write_sysreg(SystemRegister::HcrEl2, 1 << 27).is_ok());
        assert_eq!(cpu.read_sysreg(SystemRegister::HcrEl2).unwrap(), 1 << 27);
        assert!(cpu.write_sysreg(SystemRegister::VbarEl2, 0x8000).is_ok());
        cpu.pc = 0x1234;
        cpu.raise(GuestException {
            kind: ExceptionKind::DataAbort,
            instruction: 0,
            syndrome: 0x25,
            far: 0x4000,
            pc: cpu.pc,
        });
        assert_eq!(cpu.sys.elr_el2, 0x1234);
        assert_eq!(cpu.sys.esr_el2, (ESR_EC_DABT_SAME << 26) | ESR_FSC_TRANSLATION_L3);
        assert_eq!(cpu.take_pending_exception().unwrap().kind, ExceptionKind::DataAbort);
        assert_eq!(cpu.current_el, ExceptionLevel::El2);
        assert_eq!(cpu.pc, 0x8200);
        assert!(cpu.eret().is_ok());
        assert_eq!(cpu.current_el, ExceptionLevel::El2);
        assert_eq!(cpu.pc, 0x1234);

        assert_eq!(
            cpu.read_sysreg(SystemRegister::ScrEl3),
            Err(SysRegFault::Privilege)
        );
        assert!(cpu.set_exception_level(ExceptionLevel::El3 as u8));
        assert!(cpu.write_sysreg(SystemRegister::ScrEl3, 1).is_ok());
        assert_eq!(cpu.read_sysreg(SystemRegister::ScrEl3).unwrap(), 1);
    }

    #[test]
    fn smp_exclusive_and_pauth_boundaries_are_explicit() {
        let mut cpu = GuestCpuState::reset(1);
        assert!(cpu.smp.set_cpu_count(4));
        assert!(cpu.smp.bring_online(1));
        cpu.smp.route_external(1 << 1);
        assert!(cpu.poll_interrupt());
        assert_eq!(
            cpu.pending_exception.unwrap().kind,
            ExceptionKind::ExternalInterrupt
        );
        cpu.pending_exception = None;
        cpu.exclusive.reserve(0x1000, 8, 1);
        assert!(cpu.exclusive.succeeds(0x1000, 8, 1));
        cpu.exclusive.clear_on_store(0x1004, 4);
        assert!(!cpu.exclusive.valid);
        let signed = cpu.pauth.sign(1, 2, 0, 16).unwrap();
        assert_eq!(cpu.pauth.authenticate(signed, 2, 0, 16).unwrap(), 1);
    }

    #[test]
    fn reference_core_executes_guest_and_commits_halt() {
        let code = [movz(0, 7).to_le_bytes(), 0xd4400000u32.to_le_bytes()];
        let mut guest = [0u8; 8];
        guest[..4].copy_from_slice(&code[0]);
        guest[4..].copy_from_slice(&code[1]);
        let mut ram = [0u8; 4096];
        let mut cpu = GuestCpuState::reset(0);
        let result = cpu.run_bounded(&guest, &mut ram, 4);
        assert_eq!(result.status, ArchRunStatus::Halt);
        assert_eq!(cpu.x[0], 7);
        assert_eq!(result.retired, 2);
        assert_eq!(result.pc, 8);
    }

    #[test]
    fn reference_core_budget_is_per_bounded_invocation() {
        let code = [movz(0, 9).to_le_bytes(), 0xd4400000u32.to_le_bytes()];
        let mut guest = [0u8; 8];
        guest[..4].copy_from_slice(&code[0]);
        guest[4..].copy_from_slice(&code[1]);
        let mut ram = [0u8; 4096];
        let mut cpu = GuestCpuState::reset(0);

        let first = cpu.run_bounded(&guest, &mut ram, 4);
        let second = cpu.run_bounded(&guest, &mut ram, 4);

        assert_eq!(first.status, ArchRunStatus::Halt);
        assert_eq!(second.status, ArchRunStatus::Halt);
        assert_eq!(first.retired, 2);
        assert_eq!(second.retired, 2);
        assert_eq!(cpu.x[0], 9);
    }

    #[test]
    fn reference_core_records_exception_before_explicit_vector_take() {
        let code = 0xffff_ffffu32.to_le_bytes();
        let mut ram = [0u8; 4096];
        let mut cpu = GuestCpuState::reset(0);
        assert!(cpu.set_exception_level(ExceptionLevel::El0 as u8));
        let result = cpu.run_bounded(&code, &mut ram, 4);
        assert_eq!(result.status, ArchRunStatus::Exception);
        assert_eq!(
            result.exception.unwrap().kind,
            ExceptionKind::UndefinedInstruction
        );
        assert_eq!(cpu.current_el, ExceptionLevel::El0);
        assert_eq!(cpu.exception_target, Some(ExceptionLevel::El1));
        assert!(cpu.exception_from_lower_el);
        assert!(cpu.pending_exception.is_some());
        assert!(cpu.state_valid());
        assert_eq!(
            cpu.take_pending_exception().unwrap().kind,
            ExceptionKind::UndefinedInstruction
        );
        assert_eq!(cpu.current_el, ExceptionLevel::El1);
        assert_eq!(cpu.pc, 0x400);
        assert!(cpu.pending_exception.is_none());
        assert!(cpu.state_valid());
    }

    #[test]
    fn reference_core_keeps_fault_instruction_distinct_from_esr() {
        let word = 0xffff_ffffu32;
        let code = word.to_le_bytes();
        let mut ram = [0u8; 4096];
        let mut cpu = GuestCpuState::reset(0);
        let result = cpu.run_bounded(&code, &mut ram, 1);
        let exception = result.exception.unwrap();

        assert_eq!(exception.kind, ExceptionKind::UndefinedInstruction);
        assert_eq!(exception.instruction, word);
        assert_eq!(exception.syndrome, ESR_EC_UNKNOWN << 26);
        assert_ne!(u64::from(exception.instruction), exception.syndrome);
    }

    #[test]
    fn nonzero_hlt_traps_without_committing_a_next_pc() {
        let code = 0xd440_0020u32.to_le_bytes(); // HLT #1 is not the test halt.
        let mut ram = [0u8; 64];
        let mut cpu = GuestCpuState::reset(0);
        let result = cpu.run_bounded(&code, &mut ram, 1);

        assert_eq!(result.status, ArchRunStatus::Exception);
        assert_eq!(result.pc, 0);
        let exception = result.exception.unwrap();
        assert_eq!(exception.kind, ExceptionKind::UndefinedInstruction);
        assert_eq!(exception.pc, 0);
        assert_eq!(cpu.pc, 0);
        assert_eq!(cpu.sys.elr_el1, 0);
    }

    #[test]
    fn data_abort_syndrome_preserves_store_direction() {
        // The disabled-MMU RAM bus rejects this high physical address.  The
        // architectural ESR must classify it as a same-EL data abort and
        // set WnR; the raw STR encoding itself must not be used as the ISS.
        let code_words = [0xd2a2_0001u32, 0xf900_0020u32];
        let mut guest = [0u8; 8];
        guest[..4].copy_from_slice(&code_words[0].to_le_bytes());
        guest[4..].copy_from_slice(&code_words[1].to_le_bytes());
        let mut ram = [0u8; 64];
        let mut cpu = GuestCpuState::reset(0);
        let result = cpu.run_bounded(&guest, &mut ram, 4);

        assert_eq!(result.status, ArchRunStatus::Exception);
        assert_eq!(result.exception.unwrap().kind, ExceptionKind::DataAbort);
        assert_eq!(cpu.sys.far_el1, 0x1000_0000);
        assert_eq!(
            cpu.sys.esr_el1,
            (ESR_EC_DABT_SAME << 26) | ESR_ISS_WNR | ESR_FSC_TRANSLATION_L3
        );
    }

    #[test]
    fn reference_core_reads_counter_through_mrs() {
        let code = [
            mrs(0, SystemRegister::CntpctEl0).to_le_bytes(),
            0xd4400000u32.to_le_bytes(),
        ];
        let mut guest = [0u8; 8];
        guest[..4].copy_from_slice(&code[0]);
        guest[4..].copy_from_slice(&code[1]);
        let mut ram = [0u8; 4096];
        let mut cpu = GuestCpuState::reset(0);
        cpu.advance_counter(9);
        let result = cpu.run_bounded(&guest, &mut ram, 4);
        assert_eq!(result.status, ArchRunStatus::Halt);
        assert_eq!(cpu.x[0], 9);
    }

    #[test]
    fn reference_core_exposes_current_el_identity_and_correct_timer_encodings() {
        let code_words = [
            mrs(0, SystemRegister::CurrentEl),
            mrs(1, SystemRegister::IdAa64Mmfr0El1),
            mrs(2, SystemRegister::IdAa64Isar1El1),
            mrs(3, SystemRegister::CntpctEl0),
            0xd440_0000,
        ];
        let mut code = [0u8; 20];
        for (index, word) in code_words.iter().enumerate() {
            code[index * 4..index * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
        let mut ram = [0u8; 4096];
        let mut cpu = GuestCpuState::reset(0);
        cpu.advance_counter(10);
        let result = cpu.run_bounded(&code, &mut ram, 16);
        assert_eq!(result.status, ArchRunStatus::Halt);
        assert_eq!(cpu.x[0], 4); // CurrentEL: EL1 encoded in bits [3:2].
        assert_eq!((cpu.x[1] >> 20) & 0xf, 1); // TGran16 supported.
        assert_eq!((cpu.x[1] >> 28) & 0xf, 0); // TGran4 supported (v8.0 encoding).
        assert_eq!(cpu.x[2] & 0xf0, 0x10); // Baseline QARMA5 APA1, with API absent.
        assert_eq!(cpu.x[3], 13); // MRS observes counter before its retire.
    }

    #[test]
    fn reference_core_accepts_daif_spsel_and_tlbi_boundaries() {
        let words: [u32; 7] = [
            0xd503_42df, // msr daifset, #2 (mask IRQ)
            0xd503_42ff, // msr daifclr, #2
            0xd500_41bf, // msr spsel, #1
            0xd508_871f, // tlbi vmalle1
            0xd503_3f9f, // dsb sy
            0xd503_3fdf, // isb
            0xd440_0000,
        ];
        let mut code = [0u8; 28];
        for (index, word) in words.iter().enumerate() {
            code[index * 4..index * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
        let mut ram = [0u8; 4096];
        let mut cpu = GuestCpuState::reset(0);
        let result = cpu.run_bounded(&code, &mut ram, 16);
        assert_eq!(result.status, ArchRunStatus::Halt);
        assert_ne!(cpu.pstate & 1, 0);
        assert_eq!(cpu.pstate & PSTATE_I, 0);
    }

    #[test]
    fn pending_timer_interrupt_commits_exception_entry_at_instruction_boundary() {
        // nop; nop.  The first instruction retires and advances CNTPCT to
        // CNTP_CVAL; the next boundary must enter the EL1 IRQ vector before
        // the second instruction is fetched.
        let words = [0xd503_201f_u32, 0xd503_201f_u32];
        let mut guest = [0u8; 8];
        for (index, word) in words.iter().enumerate() {
            guest[index * 4..index * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
        let mut ram = [0u8; 4096];
        let mut cpu = GuestCpuState::reset(0);
        cpu.sys.vbar_el1 = 0x4000;
        assert!(cpu.write_sysreg(SystemRegister::CntpCvalEl0, 1).is_ok());
        assert!(cpu.write_sysreg(SystemRegister::CntpCtlEl0, CNTP_CTL_ENABLE).is_ok());

        let result = cpu.run_bounded(&guest, &mut ram, 4);

        assert_eq!(result.status, ArchRunStatus::Exception);
        let exception = result.exception.unwrap();
        assert_eq!(exception.kind, ExceptionKind::TimerInterrupt);
        assert_eq!(exception.pc, 4);
        assert_eq!(result.retired, 1);
        assert_eq!(result.pc, 0x4280); // VBAR_EL1 + current-ELh IRQ slot.
        assert_eq!(cpu.current_el, ExceptionLevel::El1);
        assert_eq!(cpu.sys.elr_el1, 4);
        assert_eq!(cpu.sys.spsr_el1 as u32 & PSTATE_MODE_MASK, 5);
        assert_ne!(cpu.pstate & PSTATE_I, 0);
        assert!(cpu.pending_exception.is_none());
        assert!(cpu.state_valid());
        assert!(cpu.timer.pending());
    }

    #[test]
    fn reference_core_executes_acquire_release_exclusive_sequence() {
        // mov x1, #0x100; ldaxr x0, [x1]; add x0, x0, #1;
        // dmb ish; stlxr w2, w0, [x1]; halt.
        let words = [
            movz(1, 0x100),
            0xc85f_fc20,
            0x9100_0400,
            0xd503_3bbf,
            0xc802_fc20,
            0xd440_0000,
        ];
        let mut guest = [0u8; 24];
        for (index, word) in words.iter().enumerate() {
            guest[index * 4..index * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
        let mut ram = [0u8; 4096];
        ram[0x100..0x108].copy_from_slice(&41u64.to_le_bytes());
        let mut cpu = GuestCpuState::reset(0);
        let result = cpu.run_bounded(&guest, &mut ram, 16);
        assert_eq!(result.status, ArchRunStatus::Halt);
        assert_eq!(u64::from_le_bytes(ram[0x100..0x108].try_into().unwrap()), 42);
        assert_eq!(cpu.x[2], 0);
        assert!(!cpu.exclusive.valid);
    }

    #[test]
    fn failed_store_exclusive_reports_failure_and_clrex_clears_reservation() {
        // mov x1, #0x100; ldxr x0, [x1]; clrex; stxr w2, x0, [x1]; halt.
        let words = [
            movz(1, 0x100),
            0xc85f_7c20,
            0xd503_3f5f,
            0xc802_7c20,
            0xd440_0000,
        ];
        let mut guest = [0u8; 20];
        for (index, word) in words.iter().enumerate() {
            guest[index * 4..index * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
        let mut ram = [0u8; 4096];
        ram[0x100..0x108].copy_from_slice(&41u64.to_le_bytes());
        let mut cpu = GuestCpuState::reset(0);
        let result = cpu.run_bounded(&guest, &mut ram, 16);
        assert_eq!(result.status, ArchRunStatus::Halt);
        assert_eq!(u64::from_le_bytes(ram[0x100..0x108].try_into().unwrap()), 41);
        assert_eq!(cpu.x[2], 1);
        assert!(!cpu.exclusive.valid);
    }

    #[test]
    fn reference_core_dispatches_guest_mmio_through_the_m1_bus() {
        // mov x1, #M1_GUEST_MMIO_BASE; add x1, x1, #TIMER << 12;
        // mov x0, #7; str x0, [x1, #8]; mov w0, #1;
        // str w0, [x1, #0x10]; ldr x0, [x1]; hlt.
        let words = [
            0xd2a2_0001,
            0x9140_0821,
            movz(0, 7),
            0xf900_0420,
            0x5280_0020,
            0xb900_1020,
            0xf940_0020,
            0xd440_0000,
        ];
        let mut code = [0u8; 32];
        for (index, word) in words.iter().enumerate() {
            code[index * 4..index * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
        let mut ram = [0u8; 65_536];
        let mut graph = crate::m1::M1MachineGraph::new(ram.len() as u64);
        assert!(graph.activate_runtime());
        let mut cpu = GuestCpuState::reset(0);
        let result = {
            let mut bus = crate::m1::M1GuestBus::new(&mut graph, &mut ram);
            cpu.run_bounded_with_bus(&code, &mut bus, 32, |_| {})
        };

        assert_eq!(result.status, ArchRunStatus::Exception, "result={:?}", result);
        assert_eq!(result.exception.unwrap().kind, ExceptionKind::TimerInterrupt);
        assert_eq!(result.retired, 7);
        assert_eq!(cpu.x[0], 6);
        assert_eq!(graph.timer.compare, 7);
        assert!(graph.timer.enabled);
        assert_eq!(graph.take_interrupt(0), Some(0));
        assert_eq!(graph.mmio_read(crate::m1::M1_LOGICAL_TIMER_BASE + 0x00, 64), Ok(7));
    }

    #[test]
    fn reference_core_converts_non_timer_m1_irq_into_external_exception() {
        // The display graph raises its source on a control write.  The bus
        // mirrors that source into the CPU's external-pending state only
        // after the store retires; the next boundary therefore returns an
        // ExternalInterrupt instead of consuming a half-committed store.
        let words = [
            0xd2a2_0001, // mov x1, #M1_GUEST_MMIO_BASE
            0x9140_1821, // add x1, x1, #0x6000
            movz(0, 1),
            0xb900_0020, // str w0, [x1]
            0xd440_0000,
        ];
        let mut code = [0u8; 20];
        for (index, word) in words.iter().enumerate() {
            code[index * 4..index * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
        let mut ram = [0u8; 65_536];
        let mut graph = crate::m1::M1MachineGraph::new(ram.len() as u64);
        assert!(graph.configure_display(
            0x1000,
            0x2000,
            2,
            2,
            8,
            crate::m1::M1DisplayFormat::Xrgb8888,
        ).is_ok());
        assert!(graph.activate_runtime());
        let mut cpu = GuestCpuState::reset(0);
        let result = {
            let mut bus = crate::m1::M1GuestBus::new(&mut graph, &mut ram);
            cpu.run_bounded_with_bus(&code, &mut bus, 16, |_| {})
        };
        assert_eq!(result.status, ArchRunStatus::Exception);
        assert_eq!(result.exception.unwrap().kind, ExceptionKind::ExternalInterrupt);
        assert_eq!(result.retired, 4);
        assert_eq!(cpu.pc, 0x280); // VBAR_EL1 reset + current-ELh IRQ slot.
        assert!(graph.take_interrupt(0).is_some());
    }

    #[test]
    fn iboot_panic_range_data_abort_preserves_wnr_and_far() {
        // Simulate iBoot probing an unmapped MMIO address (e.g. device
        // register at 0x2001_0000) via store.  The disabled-MMU RAM bus
        // rejects the high address as a data abort.  The ESR must encode
        // DABT_SAME + WnR + translation fault, and FAR must be the
        // faulting VA so the host can classify the device map.
        let code_words = [
            0xd2a4_0021u32, // movz x1, #0x2001, lsl #16
            0xb900_0020u32, // str w0, [x1]
            0xd440_0000u32, // hlt (should not reach)
        ];
        let mut guest = [0u8; 12];
        for (index, word) in code_words.iter().enumerate() {
            guest[index * 4..index * 4 + 4].copy_from_slice(&word.to_le_bytes());
        }
        let mut ram = [0u8; 65_536];
        let mut cpu = GuestCpuState::reset(0);
        let result = cpu.run_bounded(&guest, &mut ram, 8);

        assert_eq!(result.status, ArchRunStatus::Exception);
        let exception = result.exception.unwrap();
        assert_eq!(exception.kind, ExceptionKind::DataAbort);
        // FAR must record the store target, not the PC.
        assert_eq!(cpu.sys.far_el1, 0x2001_0000);
        // ESR must be DABT_SAME with WnR set (store direction).
        assert_eq!(
            cpu.sys.esr_el1,
            (ESR_EC_DABT_SAME << 26) | ESR_ISS_WNR | ESR_FSC_TRANSLATION_L3
        );
        // The pending exception is latched, not committed: EL is unchanged.
        assert_eq!(cpu.current_el, ExceptionLevel::El1);
        assert!(cpu.pending_exception.is_some());
        // After explicit take, the architectural entry completes.
        cpu.sys.vbar_el1 = 0x8000;
        let taken = cpu.take_pending_exception().unwrap();
        assert_eq!(taken.kind, ExceptionKind::DataAbort);
        assert_eq!(cpu.pc, 0x8200); // VBAR + current-ELh sync slot.
        assert!(cpu.pending_exception.is_none());
        assert!(cpu.state_valid());
    }

    #[test]
    fn iboot_panic_range_instruction_abort_classifies_fetch_fault() {
        // Simulate iBoot branching to an unmapped address past the end of
        // RAM.  The disabled-MMU pass-through translates to the same
        // physical address, which is outside the 64 KiB RAM, causing an
        // InstructionAbort on the fetch.  ESR must be IABT_SAME with
        // translation fault FSC.
        //
        // B #65536 from PC=0: imm26 = 65536 >> 2 = 0x4000.
        // Encoding: 0b00_0101 imm26 = 0x1400_0000 | 0x4000 = 0x1400_4000.
        let code = 0x1400_4000u32.to_le_bytes();
        let mut ram = [0u8; 65_536];
        let mut cpu = GuestCpuState::reset(0);
        let result = cpu.run_bounded(&code, &mut ram, 4);

        assert_eq!(result.status, ArchRunStatus::Exception);
        let exception = result.exception.unwrap();
        assert_eq!(exception.kind, ExceptionKind::InstructionAbort);
        // ESR must be IABT_SAME with translation fault FSC.
        assert_eq!(
            cpu.sys.esr_el1,
            (ESR_EC_IABT_SAME << 26) | ESR_FSC_TRANSLATION_L3
        );
        // FAR is the faulting fetch address (past 64 KiB RAM).
        assert_eq!(cpu.sys.far_el1, 0x1_0000);
        // Exception is latched, not committed.
        assert_eq!(cpu.current_el, ExceptionLevel::El1);
        assert!(cpu.pending_exception.is_some());
    }
}
