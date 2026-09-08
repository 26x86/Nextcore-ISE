//! Explicit software platform profile and stable x86 EFI ABI. No hardware
//! reset claim: unsupported platform registers/fields never become no-ops.
#![allow(dead_code)]

pub const PROFILE_NONE: u32 = 0;
pub const PROFILE_IRQ_COMPAT_V1: u32 = 1;
pub const OVERRIDE_KEY: u32 = 0x6fa8;
pub const OVERRIDE_MASK: u64 = 0x00f00000;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct BootOptionsV2 {
    pub abi_version: u32, pub struct_size: u32, pub platform_profile: u32, pub flags: u32,
    pub initial_override: u64, pub initial_pstate: u64, pub vbar: u64,
    pub irq_level: u64, pub fiq_level: u64, pub reserved: u64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct BaseBootResult {
    pub status: u32, pub fault_instruction: u32,
    pub retired: u64, pub pc: u64, pub x0: u64, pub x1: u64, pub x2: u64, pub x3: u64,
    pub compiled_blocks: u64,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct BootResultV2 {
    pub base: BaseBootResult, pub platform_override: u64,
    pub pending_lines: u32, pub platform_profile: u32,
    pub elr: u64, pub spsr: u64, pub exception_vector: u64,
    pub esr: u64, pub pstate: u64, pub sp: u64,
}
const _: [(); 64] = [(); core::mem::size_of::<BootOptionsV2>()];
const _: [(); 128] = [(); core::mem::size_of::<BootResultV2>()];

#[derive(Clone, Copy)]
pub struct PlatformState { pub profile: u32, pub override_value: u64, pub irq: bool, pub fiq: bool }
impl PlatformState {
    pub const fn reset() -> Self { Self { profile: 0, override_value: 0, irq: false, fiq: false } }
    pub fn configure(&mut self, profile: u32, value: u64) -> bool {
        if profile > PROFILE_IRQ_COMPAT_V1 || (profile == PROFILE_NONE && value != 0)
            || value & !OVERRIDE_MASK != 0 || !matches!(value >> 20 & 3, 0 | 2)
            || !matches!(value >> 22 & 3, 0 | 2) { return false; }
        self.profile=profile;self.override_value=value;true
    }
    pub fn irq_enabled(&self) -> bool { self.profile == 0 || (self.override_value >> 22 & 3) != 2 }
    pub fn fiq_enabled(&self) -> bool { self.profile == 0 || (self.override_value >> 20 & 3) != 2 }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn platform_abi_receipt() {
        std::println!("VF_PLATFORM_ABI {{\"options_size\":{},\"options_align\":{},\"options_override\":{},\"options_vbar\":{},\"result_size\":{},\"result_align\":{},\"result_override\":{},\"result_pending\":{},\"result_elr\":{},\"result_sp\":{}}}",
            core::mem::size_of::<BootOptionsV2>(),core::mem::align_of::<BootOptionsV2>(),
            core::mem::offset_of!(BootOptionsV2,initial_override),core::mem::offset_of!(BootOptionsV2,vbar),
            core::mem::size_of::<BootResultV2>(),core::mem::align_of::<BootResultV2>(),
            core::mem::offset_of!(BootResultV2,platform_override),core::mem::offset_of!(BootResultV2,pending_lines),
            core::mem::offset_of!(BootResultV2,elr),core::mem::offset_of!(BootResultV2,sp));
    }
}
