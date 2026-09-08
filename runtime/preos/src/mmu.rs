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
    /// consumes TG0, T0SZ, TG1, T1SZ and EPD0/EPD1; cacheability/shareability bits
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
    ) -> Result<Option<Translation>, Fault> {
        for entry in self.tlb.iter() {
            if !entry.valid || entry.asid != self.asid {
                continue;
            }
            let shift = u32::from(entry.page_shift);
            if (va >> shift) != entry.va_tag {
                continue;
            }
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
                return Err(Fault::Permission);
            }
            let offset_mask = (1u64 << shift) - 1;
            return Ok(Some(Translation {
                pa: entry.pa_base | (va & offset_mask),
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

    /// Translate a VA through the selected TTBR0/TTBR1 range. `read64` reads guest physical memory,
    /// not host memory; returning `None` is a translation fault.
    pub(crate) fn translate<F>(
        &mut self,
        va: u64,
        access: Access,
        current_el: ExceptionLevel,
        mut read64: F,
    ) -> Result<Translation, Fault>
    where
        F: FnMut(u64) -> Option<u64>,
    {
        if matches!(access, Access::Execute) && va & 3 != 0 {
            return Err(Fault::Alignment);
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
            return Err(Fault::AddressSize);
        }
        if let Some(hit) = self.tlb_lookup(va, access, current_el)? {
            return Ok(hit);
        }

        let (root, tsz, walk_disabled) = self.select_root(va).ok_or(Fault::AddressSize)?;
        // EPD inhibits a table walk on a TLB miss, not canonical-address
        // recognition. A matching TLB entry was considered above.
        if walk_disabled {
            return Err(Fault::Translation);
        }
        let start_level = self
            .granule
            .start_level(tsz)
            .ok_or(Fault::AddressSize)?;
        let index_bits = self.granule.index_bits();
        let page_shift = self.granule.page_shift();
        let address_mask = self.granule.address_mask();
        let index_mask = (1u64 << index_bits) - 1;
        // A shortened initial table consumes only VA[63-TxSZ:shift].
        // TTBR1 canonical extension bits are not part of that table index.
        // Later levels consume the usual full index width.
        let table_va = va & (u64::MAX >> tsz);
        let mut table = root & address_mask;
        for level in start_level..=MAX_LEVEL {
            let shift = page_shift + index_bits * (MAX_LEVEL as u64 - level as u64);
            let index = (table_va >> shift) & index_mask;
            let entry_address = table.checked_add(index * 8).ok_or(Fault::Translation)?;
            let descriptor = read64(entry_address).ok_or(Fault::Translation)?;
            if descriptor & 1 == 0 {
                return Err(Fault::Translation);
            }
            let descriptor_type = descriptor & DESCRIPTOR_TYPE_MASK;
            if level < MAX_LEVEL && descriptor_type == 1 {
                let block_shift = shift;
                let block_mask = !((1u64 << block_shift) - 1);
                let (writable, user_accessible, executable_el0, executable_privileged) =
                    Self::descriptor_permissions(descriptor, level, current_el, access)?;
                let pa_base = descriptor & address_mask & block_mask;
                let translation = Translation {
                    pa: pa_base | (va & !block_mask),
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
                );
                return Ok(translation);
            }
            if level < MAX_LEVEL && descriptor_type != 3 {
                return Err(Fault::Translation);
            }
            if level == MAX_LEVEL {
                if descriptor_type != 3 {
                    return Err(Fault::Translation);
                }
                let (writable, user_accessible, executable_el0, executable_privileged) =
                    Self::descriptor_permissions(descriptor, level, current_el, access)?;
                let pa_base = descriptor & address_mask;
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
                );
                return Ok(translation);
            }
            table = descriptor & address_mask;
        }
        Err(Fault::Translation)
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
                |_| panic!("noncanonical address accessed a descriptor")), Err(Fault::AddressSize));
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

}
