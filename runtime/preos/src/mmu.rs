//! Fixed-storage AArch64 stage-1 translation primitives.
//!
//! This is guest MMU state. It is not an EFI page-table helper and it never
//! follows a host pointer supplied by firmware. The implementation is small
//! but uses the translation parameters that matter to an arm64e guest:
//! 4 KiB/16 KiB granules, TTBR0/TTBR1, dynamic starting levels, and a fixed
//! TLB. Descriptor checks are explicit so translation, permission, and
//! address faults remain distinct.

#![allow(dead_code)]

use super::arch::ExceptionLevel;

pub(crate) const PAGE_SHIFT: u64 = 12;
pub(crate) const PAGE_SIZE: u64 = 1 << PAGE_SHIFT;
pub(crate) const PAGE_SHIFT_16K: u64 = 14;
pub(crate) const PAGE_SIZE_16K: u64 = 1 << PAGE_SHIFT_16K;
const MAX_LEVEL: usize = 3;
const TLB_ENTRIES: usize = 16;
const PHYSICAL_ADDRESS_MASK: u64 = 0x0000_ffff_ffff_ffff;
const DESCRIPTOR_TYPE_MASK: u64 = 0x3;
const AF_BIT: u64 = 1 << 10;
const PXN_BIT: u64 = 53;
const UXN_BIT: u64 = 54;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Granule {
    FourKiB,
    SixteenKiB,
}

impl Granule {
    fn page_shift(self) -> u64 {
        match self {
            Self::FourKiB => PAGE_SHIFT,
            Self::SixteenKiB => PAGE_SHIFT_16K,
        }
    }

    fn index_bits(self) -> u64 {
        match self {
            Self::FourKiB => 9,
            Self::SixteenKiB => 11,
        }
    }

    fn tcr_encoding(self) -> u64 {
        match self {
            Self::FourKiB => 0,
            // TCR_EL1.TG0 == 0b10 selects a 16 KiB granule.
            Self::SixteenKiB => 2,
        }
    }

    fn valid_tsz(self, tsz: u8) -> bool {
        match self {
            // The bounded 4 KiB path retains the original 25..48 VA-bit
            // contract (T0SZ 39..16).
            Self::FourKiB => (16..=39).contains(&tsz),
            // M1 arm64e uses the 16 KiB translation regime.  T0SZ 17..47
            // covers the architectural 17..47 VA-bit range supported here.
            Self::SixteenKiB => (17..=47).contains(&tsz),
        }
    }

    fn start_level(self, tsz: u8) -> Option<usize> {
        if !self.valid_tsz(tsz) {
            return None;
        }
        let va_bits = 64u64 - u64::from(tsz);
        let covered = va_bits.saturating_sub(self.page_shift());
        let levels = usize::try_from(
            covered
                .saturating_add(self.index_bits() - 1)
                / self.index_bits(),
        )
        .ok()?;
        if levels == 0 || levels > MAX_LEVEL + 1 {
            return None;
        }
        Some(MAX_LEVEL + 1 - levels)
    }

    fn root_alignment(self) -> u64 {
        1u64 << self.page_shift()
    }

    fn address_mask(self) -> u64 {
        PHYSICAL_ADDRESS_MASK & !(self.root_alignment() - 1)
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u8)]
pub(crate) enum Access {
    Read = 0,
    Write = 1,
    Execute = 2,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub(crate) enum Fault {
    AddressSize = 1,
    Translation = 2,
    Permission = 3,
    Alignment = 4,
    AccessFlag = 5,
}

/// A physical descriptor reader reports backing separately from descriptor bits.
/// Unavailable backing is not proof of a guest architectural external abort.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum TableReadError {
    Unavailable,
    ExternalAbort,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum TranslationFailureKind {
    Architectural(Fault),
    TableRead(TableReadError),
}

/// Context of the failed check, never the architectural ESR.S1PTW bit.
/// S1PTW describes a stage-2 fault; this walker implements stage 1 only.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum FaultContext {
    Input,
    Walk,
    Leaf,
    CachedLeaf,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct TranslationFailure {
    pub(crate) kind: TranslationFailureKind,
    pub(crate) level: Option<u8>,
    pub(crate) context: FaultContext,
    /// On CachedLeaf this is cached provenance, not a read at fault time.
    pub(crate) descriptor_pa: Option<u64>,
    pub(crate) output_pa: Option<u64>,
}

impl TranslationFailure {
    fn architectural(fault: Fault, level: Option<u8>, context: FaultContext) -> Self {
        Self { kind: TranslationFailureKind::Architectural(fault), level, context,
               descriptor_pa: None, output_pa: None }
    }

    fn at_descriptor(mut self, pa: u64) -> Self { self.descriptor_pa = Some(pa); self }
    fn with_output(mut self, pa: u64) -> Self { self.output_pa = Some(pa); self }

    /// Preserve the documented coarse legacy Option-reader API.
    fn legacy_fault(self) -> Fault {
        match self.kind {
            TranslationFailureKind::Architectural(fault) => fault,
            TranslationFailureKind::TableRead(_) => Fault::Translation,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct Translation {
    pub(crate) pa: u64,
    pub(crate) writable: bool,
    pub(crate) executable: bool,
    pub(crate) page_shift: u8,
}

#[derive(Clone, Copy)]
struct TlbEntry {
    valid: bool,
    asid: u16,
    va_tag: u64,
    pa_base: u64,
    page_shift: u8,
    writable: bool,
    user_accessible: bool,
    executable_el0: bool,
    executable_privileged: bool,
    level: u8,
    descriptor_pa: u64,
}

const TLB_ENTRY: TlbEntry = TlbEntry {
    valid: false,
    asid: 0,
    va_tag: 0,
    pa_base: 0,
    page_shift: PAGE_SHIFT as u8,
    writable: false,
    user_accessible: false,
    executable_el0: false,
    executable_privileged: false,
    level: 3,
    descriptor_pa: 0,
};

#[derive(Clone, Copy)]
pub(crate) struct VfMmu {
    pub(crate) enabled: bool,
    pub(crate) ttbr0: u64,
    pub(crate) ttbr1: u64,
    pub(crate) tcr_t0sz: u8,
    pub(crate) tcr_t1sz: u8,
    pub(crate) asid: u16,
    pub(crate) granule: Granule,
    walk_disabled: [bool; 2],
    physical_address_mask: u64,
    tlb: [TlbEntry; TLB_ENTRIES],
    next: usize,
}

impl VfMmu {
    pub(crate) const fn disabled() -> Self {
        Self {
            enabled: false,
            ttbr0: 0,
            ttbr1: 0,
            tcr_t0sz: 16,
            tcr_t1sz: 0,
            asid: 0,
            granule: Granule::FourKiB,
            walk_disabled: [false; 2],
            physical_address_mask: PHYSICAL_ADDRESS_MASK,
            tlb: [TLB_ENTRY; TLB_ENTRIES],
            next: 0,
        }
    }

    /// Configure the compatibility TTBR0/4-KiB regime used by the small
    /// unit-test callers.  Full guest configuration should use `configure_tcr`
    /// so TG0/T1SZ/TTBR1 are preserved.
    pub(crate) fn configure(&mut self, ttbr0: u64, t0sz: u8, asid: u16) -> bool {
        self.configure_tcr(ttbr0, 0, t0sz as u64, asid)
    }

    /// Configure stage-1 translation from the architecturally visible TCR.
    ///
    /// `tcr` is intentionally passed as a value rather than exposing the
    /// complete control register to this module.  The bounded implementation
    /// consumes TG0, T0SZ, TG1, T1SZ, IPS and EPD0/EPD1; cacheability/shareability bits
    /// stay in the architectural register bank until the memory-attribute
    /// phase has a backing cache model.
    pub(crate) fn configure_tcr(
        &mut self,
        ttbr0: u64,
        ttbr1: u64,
        tcr: u64,
        asid: u16,
    ) -> bool {
        let t0sz = (tcr & 0x3f) as u8;
        let t1sz = ((tcr >> 16) & 0x3f) as u8;
        let tg0 = (tcr >> 14) & 0x3;
        let tg1 = (tcr >> 30) & 0x3;
        // Baseline output widths; 52/56-bit descriptors and LPA2 are not
        // implemented by this bounded walker. Reject, never truncate them.
        let physical_bits = match (tcr >> 32) & 7 {
            0 => 32, 1 => 36, 2 => 40, 3 => 42, 4 => 44, 5 => 48,
            _ => return false,
        };
        if tcr & (1u64 << 59) != 0 { return false; }
        let granule = match tg0 {
            0 => Granule::FourKiB,
            2 => Granule::SixteenKiB,
            _ => return false,
        };
        // TG0 and TG1 do not share an encoding: TG0=10 and TG1=01
        // both select 16 KiB; TG0=00 and TG1=10 both select 4 KiB.
        // Keep the single-granule bounded model explicit for both VA halves.
        // TBI translation is not modeled: accepting it would silently use
        // different addresses from the configured architectural regime.
        if tcr & ((1u64 << 37) | (1u64 << 38)) != 0 {
            return false;
        }
        if t1sz != 0 {
            let upper_granule = match tg1 {
                1 => Granule::SixteenKiB,
                2 => Granule::FourKiB,
                _ => return false,
            };
            if upper_granule != granule {
                return false;
            }
        }
        if granule.start_level(t0sz).is_none()
            || ttbr0 & (granule.root_alignment() - 1) != 0
        {
            return false;
        }
        let ttbr1_enabled = t1sz != 0;
        if ttbr1_enabled
            && (granule.start_level(t1sz).is_none()
                || ttbr1 & (granule.root_alignment() - 1) != 0)
        {
            return false;
        }
        self.enabled = true;
        self.ttbr0 = ttbr0;
        self.ttbr1 = ttbr1;
        self.tcr_t0sz = t0sz;
        self.tcr_t1sz = if ttbr1_enabled { t1sz } else { 0 };
        self.asid = asid;
        self.granule = granule;
        self.walk_disabled = [tcr & (1 << 7) != 0, tcr & (1 << 23) != 0];
        self.physical_address_mask = (1u64 << physical_bits) - 1;
        self.invalidate();
        true
    }

    pub(crate) fn disable(&mut self) {
        self.enabled = false;
        self.invalidate();
    }

    pub(crate) fn invalidate(&mut self) {
        self.tlb = [TLB_ENTRY; TLB_ENTRIES];
        self.next = 0;
    }

    fn canonical_va(&self, va: u64) -> bool {
        self.select_root(va).is_some()
    }

    fn select_root(&self, va: u64) -> Option<(u64, u8, bool)> {
        if self.granule.start_level(self.tcr_t0sz).is_some()
            && va >> (64 - u32::from(self.tcr_t0sz)) == 0
        {
            return Some((self.ttbr0, self.tcr_t0sz, self.walk_disabled[0]));
        }
        if self.tcr_t1sz != 0
            && self.granule.start_level(self.tcr_t1sz).is_some()
            && va >> (64 - u32::from(self.tcr_t1sz))
                == (1u64 << self.tcr_t1sz) - 1
        {
            return Some((self.ttbr1, self.tcr_t1sz, self.walk_disabled[1]));
        }
        None
    }

    fn tlb_lookup(
        &self,
        va: u64,
        access: Access,
        current_el: ExceptionLevel,
    ) -> Result<Option<Translation>, TranslationFailure> {
        for entry in self.tlb.iter() {
            if !entry.valid || entry.asid != self.asid {
                continue;
            }
            let shift = u32::from(entry.page_shift);
            if (va >> shift) != entry.va_tag {
                continue;
            }
            let offset_mask = (1u64 << shift) - 1;
            let pa = entry.pa_base | (va & offset_mask);
            let failure = |fault| TranslationFailure::architectural(
                fault, Some(entry.level), FaultContext::CachedLeaf)
                .at_descriptor(entry.descriptor_pa).with_output(pa);
            if pa & !self.physical_address_mask != 0 { return Err(failure(Fault::AddressSize)); }
            let user = current_el == ExceptionLevel::El0;
            let allowed = match access {
                Access::Read => !user || entry.user_accessible,
                Access::Write => entry.writable && (!user || entry.user_accessible),
                Access::Execute => {
                    if user {
                        entry.user_accessible && entry.executable_el0
                    } else {
                        entry.executable_privileged
                    }
                }
            };
            if !allowed {
                return Err(failure(Fault::Permission));
            }
            return Ok(Some(Translation {
                pa,
                writable: entry.writable && (!user || entry.user_accessible),
                executable: if user {
                    entry.executable_el0
                } else {
                    entry.executable_privileged
                },
                page_shift: entry.page_shift,
            }));
        }
        Ok(None)
    }

    fn descriptor_permissions(
        descriptor: u64,
        level: usize,
        current_el: ExceptionLevel,
        access: Access,
    ) -> Result<(bool, bool, bool, bool), Fault> {
        if descriptor & AF_BIT == 0 {
            return Err(Fault::AccessFlag);
        }

        // AP[2:1]: 00 = EL1 RW / EL0 no access; 01 = EL0/EL1 RW;
        // 10 = EL1 RO / EL0 no access; 11 = EL0/EL1 RO.
        let ap = ((descriptor >> 6) & 0x3) as u8;
        let writable = match current_el {
            ExceptionLevel::El0 => ap == 1,
            ExceptionLevel::El1 | ExceptionLevel::El2 | ExceptionLevel::El3 => ap == 0 || ap == 1,
        };
        let user_accessible = ap == 1 || ap == 3;
        if current_el == ExceptionLevel::El0 && !user_accessible {
            return Err(Fault::Permission);
        }
        if matches!(access, Access::Write) && !writable {
            return Err(Fault::Permission);
        }

        // PXN blocks execution from privileged ELs, UXN blocks execution from
        // EL0. A level-0 block is not a valid granule in this implementation.
        let executable_el0 = descriptor & (1u64 << UXN_BIT) == 0;
        let executable_privileged = descriptor & (1u64 << PXN_BIT) == 0;
        let executable = if current_el == ExceptionLevel::El0 {
            executable_el0
        } else {
            executable_privileged
        };
        if matches!(access, Access::Execute) && !executable {
            return Err(Fault::Permission);
        }
        if level == 0 && descriptor & DESCRIPTOR_TYPE_MASK == 1 {
            return Err(Fault::Translation);
        }
        Ok((writable, user_accessible, executable_el0, executable_privileged))
    }

    /// Compatibility entry: an absent descriptor remains a coarse Translation
    /// fault. New memory providers use translate_detailed to retain its cause.
    pub(crate) fn translate<F>(
        &mut self, va: u64, access: Access, current_el: ExceptionLevel, mut read64: F,
    ) -> Result<Translation, Fault>
    where F: FnMut(u64) -> Option<u64>,
    {
        self.translate_detailed(va, access, current_el, |pa|
            read64(pa).ok_or(TableReadError::Unavailable))
            .map_err(TranslationFailure::legacy_fault)
    }

    /// One walker for both APIs. Physical read errors retain their actual level
    /// and descriptor address; no host pointer is ever returned or followed.
    pub(crate) fn translate_detailed<F>(
        &mut self, va: u64, access: Access, current_el: ExceptionLevel, mut read64: F,
    ) -> Result<Translation, TranslationFailure>
    where F: FnMut(u64) -> Result<u64, TableReadError>,
    {
        if matches!(access, Access::Execute) && va & 3 != 0 {
            return Err(TranslationFailure::architectural(Fault::Alignment, None, FaultContext::Input));
        }
        if !self.enabled {
            return Ok(Translation {
                pa: va,
                writable: true,
                executable: true,
                page_shift: self.granule.page_shift() as u8,
            });
        }
        if !self.canonical_va(va) {
            // AArch64_S1Translate VAIsOutOfRange is Translation level 0.
            // AddressSize describes output PA limits, not this VA range gap.
            return Err(TranslationFailure::architectural(Fault::Translation, Some(0), FaultContext::Input));
        }
        if let Some(hit) = self.tlb_lookup(va, access, current_el)? {
            return Ok(hit);
        }

        let (root, tsz, walk_disabled) = self.select_root(va).ok_or_else(|| TranslationFailure::architectural(Fault::Translation, Some(0), FaultContext::Input))?;
        let start_level = self
            .granule
            .start_level(tsz)
            .ok_or_else(|| TranslationFailure::architectural(Fault::AddressSize, Some(0), FaultContext::Input))?;
        // EPD inhibits a table walk on a TLB miss and reports level zero
        // in the AArch64 EL1 regime, independent of the start level.
        if walk_disabled {
            return Err(TranslationFailure::architectural(
                Fault::Translation, Some(0), FaultContext::Input));
        }
        let index_bits = self.granule.index_bits();
        let page_shift = self.granule.page_shift();
        let address_mask = self.granule.address_mask();
        let index_mask = (1u64 << index_bits) - 1;
        // A shortened initial table consumes only VA[63-TxSZ:shift].
        // TTBR1 canonical extension bits are not part of that table index.
        // Later levels consume the usual full index width.
        let table_va = va & (u64::MAX >> tsz);
        let mut table = root & address_mask;
        // TTBR ASID bits are outside address_mask; an actual out-of-range
        // table base faults before the guest-physical read callback is used.
        if table & !self.physical_address_mask != 0 { return Err(TranslationFailure::architectural(Fault::AddressSize, Some(0), FaultContext::Input).with_output(table)); }
        for level in start_level..=MAX_LEVEL {
            let shift = page_shift + index_bits * (MAX_LEVEL as u64 - level as u64);
            let index = (table_va >> shift) & index_mask;
            let walk_fault = |fault| TranslationFailure::architectural(
                fault, Some(level as u8), FaultContext::Walk);
            let entry_address = table.checked_add(index * 8).ok_or_else(|| walk_fault(Fault::Translation))?;
            if entry_address > self.physical_address_mask - 7 {
                return Err(walk_fault(Fault::AddressSize).at_descriptor(entry_address));
            }
            let descriptor = read64(entry_address).map_err(|error| TranslationFailure {
                kind: TranslationFailureKind::TableRead(error), level: Some(level as u8),
                context: FaultContext::Walk, descriptor_pa: Some(entry_address), output_pa: None,
            })?;
            let descriptor_fault = |fault| walk_fault(fault).at_descriptor(entry_address);
            if descriptor & 1 == 0 {
                return Err(descriptor_fault(Fault::Translation));
            }
            let descriptor_type = descriptor & DESCRIPTOR_TYPE_MASK;
            // This walker supports DS=0 only. A 16 KiB L1 block requires
            // LPA2/DS=1; its descriptor is reserved in the supported regime.
            if descriptor_type == 1 && (level == 0 || level == MAX_LEVEL
                || (level == 1 && self.granule == Granule::SixteenKiB)) {
                return Err(descriptor_fault(Fault::Translation));
            }
            let output = descriptor & address_mask;
            if output & !self.physical_address_mask != 0 { return Err(descriptor_fault(Fault::AddressSize).with_output(output)); }
            // Diagnostic leaf PA includes the offset on both cold and cached
            // failures. Compute it without changing descriptor/fault priority.
            let leaf_offset_mask = (1u64 << shift) - 1;
            let leaf_pa = (output & !leaf_offset_mask) | (va & leaf_offset_mask);
            let leaf_fault = |fault| TranslationFailure::architectural(
                fault, Some(level as u8), FaultContext::Leaf).at_descriptor(entry_address).with_output(leaf_pa);
            if level < MAX_LEVEL && descriptor_type == 1 {
                let block_shift = shift;
                let block_mask = !((1u64 << block_shift) - 1);
                let (writable, user_accessible, executable_el0, executable_privileged) =
                    Self::descriptor_permissions(descriptor, level, current_el, access).map_err(leaf_fault)?;
                let pa_base = output & block_mask;
                let pa = pa_base | (va & !block_mask);
                if pa & !self.physical_address_mask != 0 { return Err(leaf_fault(Fault::AddressSize).with_output(pa)); }
                let translation = Translation {
                    pa,
                    writable,
                    executable: if current_el == ExceptionLevel::El0 {
                        executable_el0
                    } else {
                        executable_privileged
                    },
                    page_shift: block_shift as u8,
                };
                self.cache(
                    va,
                    pa_base,
                    block_shift as u8,
                    writable,
                    user_accessible,
                    executable_el0,
                    executable_privileged,
                    level as u8,
                    entry_address,
                );
                return Ok(translation);
            }
            if level < MAX_LEVEL && descriptor_type != 3 {
                return Err(descriptor_fault(Fault::Translation));
            }
            if level == MAX_LEVEL {
                if descriptor_type != 3 {
                    return Err(descriptor_fault(Fault::Translation));
                }
                let (writable, user_accessible, executable_el0, executable_privileged) =
                    Self::descriptor_permissions(descriptor, level, current_el, access).map_err(leaf_fault)?;
                let pa_base = output;
                let translation = Translation {
                    pa: pa_base | (va & ((1u64 << page_shift) - 1)),
                    writable,
                    executable: if current_el == ExceptionLevel::El0 {
                        executable_el0
                    } else {
                        executable_privileged
                    },
                    page_shift: page_shift as u8,
                };
                self.cache(
                    va,
                    pa_base,
                    page_shift as u8,
                    writable,
                    user_accessible,
                    executable_el0,
                    executable_privileged,
                    level as u8,
                    entry_address,
                );
                return Ok(translation);
            }
            table = descriptor & address_mask;
        }
        Err(TranslationFailure::architectural(Fault::Translation, Some(3), FaultContext::Walk))
    }

    fn cache(
        &mut self,
        va: u64,
        pa_base: u64,
        page_shift: u8,
        writable: bool,
        user_accessible: bool,
        executable_el0: bool,
        executable_privileged: bool,
        level: u8,
        descriptor_pa: u64,
    ) {
        let shift = u32::from(page_shift);
        self.tlb[self.next] = TlbEntry {
            valid: true,
            asid: self.asid,
            va_tag: va >> shift,
            pa_base,
            page_shift,
            writable,
            user_accessible,
            executable_el0,
            executable_privileged,
            level,
            descriptor_pa,
        };
        self.next = (self.next + 1) % TLB_ENTRIES;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn read_page(entries: &[(u64, u64)], address: u64) -> Option<u64> {
        entries
            .iter()
            .find(|(base, _)| *base == (address & !7))
            .map(|(_, value)| *value)
    }

    fn detailed_fixture(granule: Granule, leaf_level: usize, leaf: u64)
        -> (VfMmu, [(u64, u64); 4], usize)
    {
        let (step, tcr, start) = match granule {
            Granule::FourKiB => (0x1000, 16, 0),
            Granule::SixteenKiB => (0x4000, 17 | (2 << 14), 1),
        };
        let mut mmu = VfMmu::disabled();
        assert!(mmu.configure_tcr(step, 0, tcr, 0));
        let mut entries = [(0, 0); 4];
        for level in start..=leaf_level {
            let i = level - start;
            let pa = step * (i as u64 + 1);
            entries[i] = (pa, if level == leaf_level { leaf } else { pa + step | 3 });
        }
        (mmu, entries, leaf_level - start + 1)
    }

    #[test]
    fn detailed_invalid_descriptors_preserve_level_and_read_address() {
        for granule in [Granule::FourKiB, Granule::SixteenKiB] {
            let start = if granule == Granule::FourKiB { 0 } else { 1 };
            for level in start..=3 {
                let (mut mmu, entries, count) = detailed_fixture(granule, level, 0);
                let mut reads = 0;
                let fault = mmu.translate_detailed(0, Access::Read, ExceptionLevel::El1, |pa| {
                    reads += 1;
                    read_page(&entries[..count], pa).ok_or(TableReadError::Unavailable)
                }).unwrap_err();
                assert_eq!(reads, count);
                assert_eq!(fault.kind, TranslationFailureKind::Architectural(Fault::Translation));
                assert_eq!(fault.level, Some(level as u8));
                assert_eq!(fault.context, FaultContext::Walk);
                assert_eq!(fault.descriptor_pa, Some(entries[count - 1].0));
                assert_eq!(fault.output_pa, None);
                assert_eq!(mmu.translate(0, Access::Read, ExceptionLevel::El1, |pa|
                    read_page(&entries[..count], pa)), Err(Fault::Translation));
            }
        }
    }

    #[test]
    fn detailed_table_failures_are_not_invalid_descriptors() {
        for granule in [Granule::FourKiB, Granule::SixteenKiB] {
            let start = if granule == Granule::FourKiB { 0 } else { 1 };
            for level in start..=3 { for error in [TableReadError::Unavailable, TableReadError::ExternalAbort] {
                let (mut mmu, entries, count) = detailed_fixture(granule, level, 0);
                let target = entries[count - 1].0;
                let fault = mmu.translate_detailed(0, Access::Write, ExceptionLevel::El1, |pa| {
                    if pa == target { Err(error) } else { Ok(read_page(&entries[..count], pa).unwrap()) }
                }).unwrap_err();
                assert_eq!(fault.kind, TranslationFailureKind::TableRead(error));
                assert_eq!(fault.level, Some(level as u8));
                assert_eq!(fault.context, FaultContext::Walk);
                assert_eq!(fault.descriptor_pa, Some(target));
                assert_eq!(fault.output_pa, None);
                assert_eq!(mmu.translate(0, Access::Write, ExceptionLevel::El1, |pa|
                    if pa == target { None } else { read_page(&entries[..count], pa) }), Err(Fault::Translation));
            }}
        }
    }

    #[test]
    fn detailed_leaf_and_cached_permission_failures_agree() {
        for granule in [Granule::FourKiB, Granule::SixteenKiB] {
            let first_leaf = if granule == Granule::FourKiB { 1 } else { 2 };
            for level in first_leaf..=3 {
                let va = if level == 3 { 0x234 } else { 0x1234 };
                let leaf = 0x8000_04c0 | if level == 3 { 3 } else { 1 };
                let (mut mmu, entries, count) = detailed_fixture(granule, level, leaf);
                let cold = mmu.translate_detailed(va, Access::Write, ExceptionLevel::El1, |pa|
                    read_page(&entries[..count], pa).ok_or(TableReadError::Unavailable)).unwrap_err();
                assert_eq!(cold.kind, TranslationFailureKind::Architectural(Fault::Permission));
                assert_eq!(cold.level, Some(level as u8));
                assert_eq!(cold.context, FaultContext::Leaf);
                assert_eq!(cold.descriptor_pa, Some(entries[count - 1].0));
                assert_eq!(cold.output_pa, Some(0x8000_0000 + va));
                mmu.translate_detailed(va, Access::Read, ExceptionLevel::El1, |pa|
                    read_page(&entries[..count], pa).ok_or(TableReadError::Unavailable)).unwrap();
                let hot = mmu.translate_detailed(va, Access::Write, ExceptionLevel::El1, |_|
                    panic!("cached denial must not reread descriptor memory")).unwrap_err();
                assert_eq!(hot.context, FaultContext::CachedLeaf);
                assert_eq!(hot.kind, cold.kind); assert_eq!(hot.level, cold.level);
                assert_eq!(hot.descriptor_pa, cold.descriptor_pa);
                assert_eq!(hot.output_pa, cold.output_pa);
            }
        }
    }

    #[test]
    fn detailed_access_flags_and_reserved_blocks_keep_their_level() {
        for granule in [Granule::FourKiB, Granule::SixteenKiB] {
            let first_leaf = if granule == Granule::FourKiB { 1 } else { 2 };
            for level in first_leaf..=3 {
                let va = if level == 3 { 0x234 } else { 0x1234 };
                let leaf = 0x8000_0040 | if level == 3 { 3 } else { 1 };
                let (mut mmu, entries, count) = detailed_fixture(granule, level, leaf);
                let fault = mmu.translate_detailed(va, Access::Read, ExceptionLevel::El1, |pa|
                    read_page(&entries[..count], pa).ok_or(TableReadError::Unavailable)).unwrap_err();
                assert_eq!(fault.kind, TranslationFailureKind::Architectural(Fault::AccessFlag));
                assert_eq!(fault.level, Some(level as u8));
                assert_eq!(fault.context, FaultContext::Leaf);
                assert_eq!(fault.output_pa, Some(0x8000_0000 + va));
            }
        }
        let (mut mmu, entries, count) = detailed_fixture(Granule::SixteenKiB, 1, 0x8000_0001);
        let fault = mmu.translate_detailed(0, Access::Read, ExceptionLevel::El1, |pa|
            read_page(&entries[..count], pa).ok_or(TableReadError::Unavailable)).unwrap_err();
        assert_eq!(fault.kind, TranslationFailureKind::Architectural(Fault::Translation));
        assert_eq!(fault.level, Some(1));
        assert_eq!(fault.output_pa, None); // Reserved type wins before AF/output.
    }

    #[test]
    fn detailed_output_address_errors_keep_descriptor_level() {
        for granule in [Granule::FourKiB, Granule::SixteenKiB] {
            let start = if granule == Granule::FourKiB { 0 } else { 1 };
            for level in start..=3 {
                let (mut mmu, entries, count) = detailed_fixture(granule, level, (1u64 << 32) | 3);
                let fault = mmu.translate_detailed(0, Access::Read, ExceptionLevel::El1, |pa|
                    read_page(&entries[..count], pa).ok_or(TableReadError::Unavailable)).unwrap_err();
                assert_eq!(fault.kind, TranslationFailureKind::Architectural(Fault::AddressSize));
                assert_eq!(fault.level, Some(level as u8));
                assert_eq!(fault.descriptor_pa, Some(entries[count - 1].0));
                assert_eq!(fault.output_pa, Some(1u64 << 32));
            }
        }
    }

    #[test]
    fn detailed_epd_and_root_address_errors_use_aarch64_level_zero() {
        for (granule, sizes) in [(Granule::FourKiB, [16u8,25,34]), (Granule::SixteenKiB,[17u8,28,39])] {
            for size in sizes { for upper in [false,true] {
                let mut mmu = VfMmu::disabled();
                let tg0 = if granule == Granule::FourKiB {0} else {2};
                let tg1 = if granule == Granule::FourKiB {2} else {1};
                let mut tcr = u64::from(size) | (tg0 << 14) | (u64::from(size) << 16) | (tg1 << 30);
                let va = if upper { u64::MAX << (64-u32::from(size)) } else {0};
                tcr |= if upper {1 << 23} else {1 << 7};
                assert!(mmu.configure_tcr(0x4000,0x4000,tcr,0));
                let fault = mmu.translate_detailed(va, Access::Read, ExceptionLevel::El1, |_| panic!("EPD read")).unwrap_err();
                assert_eq!(fault.kind, TranslationFailureKind::Architectural(Fault::Translation));
                assert_eq!(fault.level, Some(0)); assert_eq!(fault.context, FaultContext::Input);
                assert_eq!(fault.descriptor_pa, None);
                tcr &= !((1<<7)|(1<<23));
                assert!(mmu.configure_tcr(1u64<<32,1u64<<32,tcr,0));
                let fault = mmu.translate_detailed(va, Access::Read, ExceptionLevel::El1, |_| panic!("bad root read")).unwrap_err();
                assert_eq!(fault.kind, TranslationFailureKind::Architectural(Fault::AddressSize));
                assert_eq!(fault.level, Some(0)); assert_eq!(fault.output_pa, Some(1u64<<32));
            }}
        }
    }

    #[test]
    fn detailed_input_errors_never_invent_a_leaf_or_table_read() {
        let mut mmu = VfMmu::disabled();
        let fault = mmu.translate_detailed(2, Access::Execute, ExceptionLevel::El1, |_| panic!("misaligned fetch read")).unwrap_err();
        assert_eq!(fault.kind, TranslationFailureKind::Architectural(Fault::Alignment));
        assert_eq!(fault.level, None); assert_eq!(fault.context, FaultContext::Input);
        assert_eq!(fault.descriptor_pa, None); assert_eq!(fault.output_pa, None);
        assert!(mmu.configure(0x1000,16,0));
        let fault = mmu.translate_detailed(1u64<<48, Access::Read, ExceptionLevel::El1, |_| panic!("invalid VA read")).unwrap_err();
        assert_eq!(fault.kind, TranslationFailureKind::Architectural(Fault::Translation));
        assert_eq!(fault.level, Some(0)); assert_eq!(fault.descriptor_pa, None);
    }

    #[test]
    fn virtual_range_gaps_are_translation_faults_without_reading_tables() {
        for (granule, sizes) in [(Granule::FourKiB,[16u8,25,34,39]),(Granule::SixteenKiB,[17u8,28,39,47])] {
            for size in sizes {
                let tg0 = if granule == Granule::FourKiB {0} else {2};
                let tg1 = if granule == Granule::FourKiB {2} else {1};
                let tcr = u64::from(size)|(tg0<<14)|(u64::from(size)<<16)|(tg1<<30);
                let low_end = 1u64 << (64-u32::from(size));
                let high_begin = u64::MAX << (64-u32::from(size));
                let mut mmu = VfMmu::disabled();
                assert!(mmu.configure_tcr(0x4000,0x8000,tcr,0));
                for va in [low_end, high_begin-1] {
                    let fault = mmu.translate_detailed(va, Access::Read, ExceptionLevel::El1, |_| panic!("VA range read")).unwrap_err();
                    assert_eq!(fault.kind,TranslationFailureKind::Architectural(Fault::Translation));
                    assert_eq!(fault.level,Some(0)); assert_eq!(fault.context,FaultContext::Input);
                    assert_eq!((fault.descriptor_pa,fault.output_pa),(None,None));
                    assert_eq!(mmu.translate(va, Access::Read, ExceptionLevel::El1, |_| panic!("legacy VA range read")),Err(Fault::Translation));
                }
            }
        }
    }

    #[test]
    fn disabled_is_identity_and_instruction_alignment_is_four_bytes() {
        let mut mmu = VfMmu::disabled();
        assert_eq!(
            mmu.translate(0x1234, Access::Read, ExceptionLevel::El1, |_| None)
                .unwrap()
                .pa,
            0x1234
        );
        assert_eq!(
            mmu.translate(0x1002, Access::Execute, ExceptionLevel::El1, |_| None),
            Err(Fault::Alignment)
        );
    }

    #[test]
    fn page_walk_and_asid_tlb_hit() {
        let mut mmu = VfMmu::disabled();
        assert!(mmu.configure(0x1000, 16, 1));
        // L0 -> L1 -> L2 -> L3 -> PA 0x8000_0000, AF=1, AP=01 (RW EL0).
        let entries = [
            (0x1000, 0x2003),
            (0x2000, 0x3003),
            (0x3000, 0x4003),
            (0x4000, 0x8000_0443),
        ];
        let read = |address| read_page(&entries, address);
        let first = mmu
            .translate(0, Access::Write, ExceptionLevel::El1, read)
            .unwrap();
        assert_eq!(first.pa, 0x8000_0000);
        let read_again = |address| read_page(&entries, address);
        assert_eq!(
            mmu.translate(0x123, Access::Read, ExceptionLevel::El1, read_again)
                .unwrap()
                .pa,
            0x8000_0123
        );
    }

    #[test]
    fn permission_and_access_flag_faults_are_distinct() {
        let mut mmu = VfMmu::disabled();
        assert!(mmu.configure(0x1000, 16, 0));
        let read_only = [
            (0x1000, 0x2003),
            (0x2000, 0x3003),
            (0x3000, 0x4003),
            (0x4000, 0x8000_04c3), // AF=1, AP=11, RO for EL1
        ];
        assert_eq!(
            mmu.translate(0, Access::Write, ExceptionLevel::El1, |a| {
                read_page(&read_only, a)
            }),
            Err(Fault::Permission)
        );
        mmu.invalidate();
        let af_clear = [
            (0x1000, 0x2003),
            (0x2000, 0x3003),
            (0x3000, 0x4003),
            (0x4000, 0x8000_0083),
        ];
        assert_eq!(
            mmu.translate(0, Access::Read, ExceptionLevel::El1, |a| {
                read_page(&af_clear, a)
            }),
            Err(Fault::AccessFlag)
        );
    }

    #[test]
    fn dynamic_start_level_supports_t0sz_25_four_kib_blocks() {
        let mut mmu = VfMmu::disabled();
        // With a 39-bit VA range, TTBR0 points directly at an L1 table and
        // entry 0 is a 1 GiB block.  A 4-level walker that always starts at
        // L0 would read the descriptor at the wrong address.
        assert!(mmu.configure_tcr(0x1000, 0, 25, 0));
        let entries = [(0x1000, 0x8000_0441)]; // AF=1, AP=01, L1 block
        let first = mmu
            .translate(0x1234, Access::Read, ExceptionLevel::El1, |address| {
                read_page(&entries, address)
            })
            .unwrap();
        assert_eq!(first.pa, 0x8000_1234);
        assert_eq!(first.page_shift, 30);
        // The second lookup must be a TLB hit and must not need the table
        // memory to remain readable.
        assert_eq!(
            mmu.translate(0x1234, Access::Read, ExceptionLevel::El1, |_| None)
                .unwrap()
                .pa,
            0x8000_1234
        );
    }

    #[test]
    fn sixteen_kib_tcr_selects_l2_block_and_preserves_fault_class() {
        let mut mmu = VfMmu::disabled();
        // T0SZ=28 (36 VA bits), TG0=16 KiB, IPS=40 bits, EPD1=1.  The root
        // is therefore an L2 table and each block covers 32 MiB.
        let tcr = 28u64 | (2u64 << 14) | (2u64 << 32) | (1u64 << 23);
        assert!(mmu.configure_tcr(0x4000, 0, tcr, 7));
        assert_eq!(mmu.granule, Granule::SixteenKiB);
        assert_eq!(mmu.tcr_t0sz, 28);
        let entries = [(0x4000, 0x4000_0441)]; // AF=1, AP=01, L2 block
        let translated = mmu
            .translate(0x12_345, Access::Write, ExceptionLevel::El1, |address| {
                read_page(&entries, address)
            })
            .unwrap();
        assert_eq!(translated.pa, 0x4001_2345);
        assert_eq!(translated.page_shift, 25);

        mmu.invalidate();
        let read_only = [(0x4000, 0x4000_04c1)]; // AP=11, read-only
        assert_eq!(
            mmu.translate(0, Access::Write, ExceptionLevel::El1, |address| {
                read_page(&read_only, address)
            }),
            Err(Fault::Permission)
        );
    }

    #[test]
    fn upper_canonical_addresses_use_ttbr1_and_asid_tlb() {
        let mut mmu = VfMmu::disabled();
        let tcr = 25u64 | (25u64 << 16) | (2u64 << 30); // 4 KiB TG0=00, TG1=10
        assert!(mmu.configure_tcr(0x1000, 0x2000, tcr, 9));
        let upper = (!0u64) << 39;
        let entries = [(0x2000, 0xc000_0441)]; // 1 GiB-aligned L1 block
        let translated = mmu
            .translate(upper + 0x2345, Access::Read, ExceptionLevel::El1, |address| {
                read_page(&entries, address)
            })
            .unwrap();
        assert_eq!(translated.pa, 0xc000_2345);
        assert_eq!(translated.page_shift, 30);
        assert_eq!(
            mmu.translate(upper + 0x2345, Access::Read, ExceptionLevel::El1, |_| None)
                .unwrap()
                .pa,
            0xc000_2345
        );
    }

    #[test]
    fn disabled_translation_accepts_full_guest_address_width() {
        let mut mmu = VfMmu::disabled();
        assert_eq!(
            mmu.translate(
                0xffff_ffff_ffff_f000,
                Access::Read,
                ExceptionLevel::El1,
                |_| None,
            )
            .unwrap()
            .pa,
            0xffff_ffff_ffff_f000
        );
    }

    #[test]
    fn tlb_does_not_bypass_el0_access_permission() {
        let mut mmu = VfMmu::disabled();
        assert!(mmu.configure(0x1000, 16, 0));
        // AP=00: privileged read/write, no EL0 access.  Populate the TLB
        // from EL1 first, then perform an EL0 lookup without page-table
        // memory.  A TLB hit must still apply the user-access bit.
        let entries = [
            (0x1000, 0x2003),
            (0x2000, 0x3003),
            (0x3000, 0x4003),
            (0x4000, 0x8000_0403),
        ];
        assert!(
            mmu.translate(0, Access::Read, ExceptionLevel::El1, |address| {
                read_page(&entries, address)
            })
            .is_ok()
        );
        assert_eq!(
            mmu.translate(0, Access::Read, ExceptionLevel::El0, |_| None),
            Err(Fault::Permission)
        );
    }

    #[test]
    fn tlb_keeps_el0_uxn_and_privileged_pxn_permissions_distinct() {
        let mut mmu = VfMmu::disabled();
        assert!(mmu.configure(0x1000, 16, 0));
        // AP=01, PXN=1, UXN=0: EL0 may execute, while privileged execution
        // must fault.  The second lookup deliberately omits page-table data
        // so the result proves the cached permission tuple is complete.
        let entries = [
            (0x1000, 0x2003),
            (0x2000, 0x3003),
            (0x3000, 0x4003),
            (0x4000, 0x8000_0443 | (1u64 << PXN_BIT)),
        ];
        assert!(
            mmu.translate(0, Access::Execute, ExceptionLevel::El0, |address| {
                read_page(&entries, address)
            })
            .is_ok()
        );
        assert_eq!(
            mmu.translate(0, Access::Execute, ExceptionLevel::El1, |_| None),
            Err(Fault::Permission)
        );
    }

    #[test]
    fn sixteen_kib_upper_page_uses_distinct_tg1_encoding() {
        let mut mmu = VfMmu::disabled();
        let tcr = 28 | (28 << 16) | (2 << 14) | (1 << 30);
        assert!(mmu.configure_tcr(0x4000, 0x8000, tcr, 3));
        let upper = u64::MAX << 36;
        let descriptors = [(0x8000, 0xc003), (0xc000, 0x8000_0443)];
        let mapped = mmu.translate(upper + 0x1234, Access::Read, ExceptionLevel::El1,
            |a| read_page(&descriptors, a)).unwrap();
        assert_eq!(mapped.pa, 0x8000_1234);
        assert_eq!(mapped.page_shift, 14);
        assert_eq!(mmu.translate(upper + 0x1234, Access::Read, ExceptionLevel::El1,
            |_| panic!("expected cached translation")).unwrap(), mapped);
    }

    #[test]
    fn disabled_walk_is_translation_fault_without_reading_descriptors() {
        let tcr = 25 | (25 << 16) | (2 << 30);
        let upper = u64::MAX << 39;
        for (disable, blocked, allowed, root) in [(1 << 7, 0, upper, 0x2000),
                                                (1 << 23, upper, 0, 0x1000)] {
            let mut mmu = VfMmu::disabled();
            assert!(mmu.configure_tcr(0x1000, 0x2000, tcr | disable, 0));
            assert_eq!(mmu.translate(blocked, Access::Read, ExceptionLevel::El1,
                |_| panic!("disabled walk accessed a descriptor")), Err(Fault::Translation));
            assert_eq!(mmu.translate(1 << 48, Access::Read, ExceptionLevel::El1,
                |_| panic!("noncanonical address accessed a descriptor")), Err(Fault::Translation));
            assert_eq!(mmu.translate(allowed, Access::Read, ExceptionLevel::El1,
                |a| if a == root { Some(0x8000_0441) } else { None }).unwrap().pa, 0x8000_0000);
        }
    }

    #[test]
    fn rejected_tcr_preserves_previous_configuration_and_cached_mapping() {
        let mut mmu = VfMmu::disabled();
        let good = 25 | (25 << 16) | (2 << 30);
        assert!(mmu.configure_tcr(0x1000, 0x2000, good, 9));
        let upper = u64::MAX << 39;
        let expected = mmu.translate(upper + 4, Access::Read, ExceptionLevel::El1,
            |a| if a == 0x2000 { Some(0xc000_0441) } else { None }).unwrap();
        // Reserved TG1, unsupported 64 KiB, mixed granules, TBI0 and TBI1.
        for bad in [good & !(3 << 30), good | (1 << 30),
                    (good & !(3 << 30)) | (1 << 30), good | (1 << 37), good | (1 << 38)] {
            assert!(!mmu.configure_tcr(0x3000, 0x4000, bad, 12));
            assert_eq!(mmu.asid, 9);
            assert_eq!(mmu.translate(upper + 4, Access::Read, ExceptionLevel::El1,
                |_| panic!("failed configure must preserve the TLB")).unwrap(), expected);
        }
        assert!(!mmu.configure_tcr(0x1001, 0x2000, good, 9));
        assert!(!mmu.configure_tcr(0x1000, 0x2001, good, 9));
    }


    #[test]
    fn partial_initial_tables_exclude_upper_canonical_bits() {
        // Independently specified architectural table shapes. Cover each
        // supported starting level with a shortened initial table, at both
        // ends of both canonical ranges. Child tables retain their full size.
        for (tsz, start, page_shift, index_bits, root_entries, tg0, tg1) in [
            (17u8, 0usize, 12u32, 9u32, 256u64, 0u64, 2u64),
            (26, 1, 12, 9, 256, 0, 2),
            (35, 2, 12, 9, 256, 0, 2),
            (18, 1, 14, 11, 1024, 2, 1),
            (29, 2, 14, 11, 1024, 2, 1),
            (40, 3, 14, 11, 1024, 2, 1),
        ] {
            let low_mask = u64::MAX >> tsz;
            let page_mask = (1u64 << page_shift) - 1;
            for upper in [false, true] {
                for last in [false, true] {
                    let mut mmu = VfMmu::disabled();
                    let tcr = u64::from(tsz) | (u64::from(tsz) << 16)
                        | (tg0 << 14) | (tg1 << 30);
                    assert!(mmu.configure_tcr(0x10000, 0x20000, tcr, 1));
                    let root = if upper { 0x20000 } else { 0x10000 };
                    let range_base = if upper { !low_mask } else { 0 };
                    let page_offset = if last { low_mask & !page_mask } else { 0 };
                    let va = range_base | page_offset | 0x234;
                    let mut entries = std::vec::Vec::new();
                    let mut table = root;
                    for level in start..=3 {
                        let count = if level == start { root_entries } else { 1 << index_bits };
                        let index = if last { count - 1 } else { 0 };
                        let next = 0x30000 + (level as u64) * 0x10000;
                        let desc = if level == 3 { 0x8000_0443 } else { next | 3 };
                        entries.push((table + index * 8, desc));
                        table = next;
                    }
                    let mut reads = std::vec::Vec::new();
                    let translated = mmu.translate(va, Access::Read, ExceptionLevel::El1,
                        |address| { reads.push(address); read_page(&entries, address) })
                        .unwrap_or_else(|error| panic!("tsz={tsz} upper={upper} last={last}: {error:?}, reads={reads:x?}"));
                    assert_eq!(translated.pa, 0x8000_0234);
                    assert_eq!(translated.page_shift, page_shift as u8);
                    assert_eq!(reads, entries.iter().map(|entry| entry.0).collect::<std::vec::Vec<_>>());
                    assert_eq!(mmu.translate(va, Access::Read, ExceptionLevel::El1,
                        |_| panic!("expected cached translation")).unwrap(), translated);
                }
            }
        }
    }


    #[test]
    fn physical_output_width_bounds_roots_tables_and_leaf_pages() {
        // Real architectural IPS encodings, tested against both granules and
        // both canonical regions. Deliberately use nonzero ASID bits in TTBRs.
        for (ips, bits) in [(0u64,32u32),(1,36),(2,40),(3,42),(4,44),(5,48)] {
            for (page_shift, tsz, tg0, tg1) in [(12u32,34u64,0u64,2u64), (14,28,2,1)] {
                let limit = 1u64 << bits;
                let page_size = 1u64 << page_shift;
                let tcr = tsz | (tsz << 16) | (tg0 << 14) | (tg1 << 30) | (ips << 32);
                for upper in [false,true] {
                    let mut mmu=VfMmu::disabled();
                    assert!(mmu.configure_tcr(0x1234_0000_0001_0000,0xabcd_0000_0002_0000,tcr,7));
                    let root=if upper {0x20000} else {0x10000};
                    let va=if upper {u64::MAX << (64-tsz)} else {0};
                    let entries=[(root,0x30003),(0x30000,(limit-page_size)|0x443)];
                    let mapped=mmu.translate(va+0x234,Access::Read,ExceptionLevel::El1,
                        |address| read_page(&entries,address)).unwrap();
                    assert_eq!(mapped.pa,limit-page_size+0x234);
                    assert_eq!(mmu.translate(va+0x234,Access::Read,ExceptionLevel::El1,
                        |_| panic!("expected cached boundary PA")).unwrap(),mapped);
                    // Every representable width below48 has an encodable
                    // descriptor exactly at the first forbidden address.
                    if bits<48 {
                        mmu.invalidate();
                        let bad_leaf=[(root,0x30003),(0x30000,limit|0x443)];
                        assert_eq!(mmu.translate(va,Access::Read,ExceptionLevel::El1,
                            |address| read_page(&bad_leaf,address)),Err(Fault::AddressSize));
                        let mut reads=0;
                        assert_eq!(mmu.translate(va,Access::Read,ExceptionLevel::El1,
                            |address| { reads+=1; assert_eq!(address,root); Some(limit|3) }),Err(Fault::AddressSize));
                        assert_eq!(reads,1,"must not follow an out-of-range table descriptor");
                        assert!(mmu.configure_tcr(if upper {0x10000} else {limit},
                            if upper {limit} else {0x20000},tcr,7));
                        assert_eq!(mmu.translate(va,Access::Read,ExceptionLevel::El1,
                            |_| panic!("out-of-range root was dereferenced")),Err(Fault::AddressSize));
                    }
                }
            }
        }
    }

    #[test]
    fn sixteen_kib_l1_blocks_require_unsupported_lpa2() {
        let mut mmu = VfMmu::disabled();
        let tcr = 25 | (2 << 14); // DS=0, 16 KiB L1 root, 32-bit output.
        assert!(mmu.configure_tcr(0x10000, 0, tcr, 0));
        // Both low and high offsets are Translation faults, even when the
        // output field also exceeds IPS. Rejected descriptors never cache.
        for descriptor in [0x441, (1 << 40) | 0x441] {
            for va in [0x234, 1 << 32] {
                let mut reads = 0;
                assert_eq!(mmu.translate(va, Access::Read, ExceptionLevel::El1,
                    |address| { assert_eq!(address, 0x10000); reads += 1; Some(descriptor) }),
                    Err(Fault::Translation));
                assert_eq!(reads, 1);
            }
        }
        // A supported L2 block still admits and caches its full offset range.
        assert!(mmu.configure_tcr(0x10000, 0, 28 | (2 << 14), 0));
        let entries = [(0x10000, 0xfe00_0441)];
        assert_eq!(mmu.translate(0x234, Access::Read, ExceptionLevel::El1,
            |address| read_page(&entries, address)).unwrap().pa, 0xfe00_0234);
        assert_eq!(mmu.translate(0x1ff_ffff, Access::Read, ExceptionLevel::El1,
            |_| panic!("valid block offset should hit the cache")).unwrap().pa, 0xffff_ffff);
    }

    #[test]
    fn unsupported_physical_regime_preserves_cached_configuration() {
        let mut mmu=VfMmu::disabled();
        let tcr=34 | (1 << 32); //36-bit PA, 4KiB L2 root.
        assert!(mmu.configure_tcr(0x10000,0,tcr,0));
        let mapped=mmu.translate(0x234,Access::Read,ExceptionLevel::El1,
            |address| if address==0x10000 {Some(0xf_0000_0441)} else {None}).unwrap();
        for unsupported in [(tcr & !(7 << 32)) | (6 << 32),
                            (tcr & !(7 << 32)) | (7 << 32),tcr | (1 << 59)] {
            assert!(!mmu.configure_tcr(0x20000,0,unsupported,1));
            assert_eq!(mmu.translate(0x234,Access::Read,ExceptionLevel::El1,
                |_| panic!("invalid configuration evicted old translation")).unwrap(),mapped);
        }
    }

}
