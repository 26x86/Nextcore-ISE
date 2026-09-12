//! Versioned immutable stage-1 provider ABI; v1 records are unchanged.
use core::ffi::c_void;
pub const VERSION:u32=2;
pub const PROFILE_FIXED_NC:u32=1;
pub const PROFILE_FIXED_NC_UNALIGNED:u32=3;
pub const OK:u32=0;
pub const GUEST_FAULT:u32=1;
pub const UNSUPPORTED:u32=2;
pub const UNAVAILABLE:u32=3;
pub const INVALID_REQUEST:u32=4;
pub const PC_ALIGNMENT:u32=1;
pub const DATA_ALIGNMENT:u32=2;
pub const ADDRESS_SIZE:u32=3;
pub const TRANSLATION:u32=4;
pub const PERMISSION:u32=5;
pub const ACCESS_FLAG:u32=6;
pub const NO_LEVEL:u32=u32::MAX;
pub const INPUT:u32=1;
pub const WALK:u32=2;
pub const LEAF:u32=3;
pub const CACHED_LEAF:u32=4;
pub const HAS_DESCRIPTOR:u32=1;
pub const HAS_OUTPUT:u32=2;
#[repr(C)]
#[derive(Clone,Copy,Debug,Default,PartialEq,Eq)]
pub struct Controls {
    pub abi_version:u32,pub struct_size:u32,pub profile:u32,pub reserved:u32,
    pub sctlr:u64,pub ttbr0:u64,pub ttbr1:u64,pub tcr:u64,pub mair:u64,
    pub hcr:u64,pub scr:u64,pub epoch:u64,
}
#[repr(C)]
#[derive(Clone,Copy,Debug,Default)]
pub struct Request {
    pub abi_version:u32,pub struct_size:u32,pub operation:u32,pub flags:u32,
    pub width:u32,pub count:u32,pub current_el:u32,pub reserved0:u32,
    pub pc:u64,pub address:u64,pub value0:u64,pub value1:u64,pub pstate:u64,
    pub controls:Controls,pub reserved1:u64,
}
#[repr(C)]
#[derive(Clone,Copy,Debug,Default,PartialEq,Eq)]
pub struct Reply {
    pub abi_version:u32,pub struct_size:u32,pub result:u32,pub fault:u32,
    pub level:u32,pub context:u32,pub fsc:u32,pub metadata_flags:u32,
    pub value0:u64,pub value1:u64,pub address:u64,pub esr:u64,pub epoch:u64,
    pub descriptor_pa:u64,pub output_pa:u64,pub reserved:[u64;5],
}
pub type Callback=unsafe extern "C" fn(*mut c_void,*const Request,*mut Reply)->i32;

#[cfg(test)]mod tests {
    use super::*;
    #[test]fn v2_layout_is_fixed_and_separate_from_v1() {
        use core::mem::{size_of,align_of,offset_of};
        assert_eq!((size_of::<Controls>(),size_of::<Request>(),size_of::<Reply>()),(80,160,128));
        assert_eq!((align_of::<Controls>(),align_of::<Request>(),align_of::<Reply>()),(8,8,8));
        assert_eq!(offset_of!(Controls,sctlr),16);assert_eq!(offset_of!(Controls,epoch),72);
        assert_eq!(offset_of!(Request,pc),32);assert_eq!(offset_of!(Request,controls),72);
        assert_eq!(offset_of!(Request,reserved1),152);assert_eq!(offset_of!(Reply,value0),32);
        assert_eq!(offset_of!(Reply,descriptor_pa),72);assert_eq!(offset_of!(Reply,reserved),88);
        assert_eq!(size_of::<crate::abi::Request>(),80);assert_eq!(size_of::<crate::abi::Reply>(),80);
    }
}
