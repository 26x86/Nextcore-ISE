//! Separate BP34 protocol; v2 records/acceptance remain unchanged.
use core::ffi::c_void;
pub const DATA_VERSION:u32=3;
pub const PROFILE:u32=2;
pub const VERSION:u32=1;
pub const SNAPSHOT:u32=0;pub const PREPARE:u32=1;pub const COMMIT:u32=2;pub const CANCEL:u32=3;
pub const WRITE:u32=1;pub const ISB:u32=2;pub const DSB:u32=3;pub const TLBI:u32=4;
pub const SCTLR:u32=1;pub const TTBR0:u32=2;pub const TTBR1:u32=3;pub const TCR:u32=4;pub const MAIR:u32=5;
pub const OK:u32=0;pub const UNSUPPORTED:u32=1;pub const INVALID:u32=2;pub const UNAVAILABLE:u32=3;
pub const EMPTY:u32=0;pub const PROPOSED:u32=1;pub const KNOWN:u32=2;pub const UNCERTAIN:u32=3;
pub const CONTROL:u32=1;pub const PENDING:u32=2;pub const WRAP:u32=3;pub const MAPPING:u32=4;
pub const BYTES:u32=5;pub const STALE:u32=6;pub const TOKEN:u32=7;pub const MALFORMED:u32=8;
pub const OVERFLOW:u32=9;pub const BACKING:u32=10;
pub type DataRequest=crate::abi_v2::Request;
pub type DataReply=crate::abi_v2::Reply;
pub type Controls=crate::abi_v2::Controls;
pub type DataCallback=unsafe extern "C" fn(*mut c_void,*const DataRequest,*mut DataReply)->i32;
#[repr(C)]#[derive(Clone,Copy,Debug,Default,PartialEq,Eq)]
pub struct Snapshot {
    pub sctlr:u64,pub ttbr0:u64,pub ttbr1:u64,pub tcr:u64,pub mair:u64,pub hcr:u64,pub scr:u64,pub reserved:u64,
}
impl Snapshot {
    pub fn from_controls(c:Controls)->Self {Self{sctlr:c.sctlr,ttbr0:c.ttbr0,ttbr1:c.ttbr1,tcr:c.tcr,mair:c.mair,hcr:c.hcr,scr:c.scr,reserved:0}}
    pub fn controls(self,epoch:u64)->Controls {Controls{abi_version:DATA_VERSION,struct_size:80,profile:PROFILE,reserved:0,
        sctlr:self.sctlr,ttbr0:self.ttbr0,ttbr1:self.ttbr1,tcr:self.tcr,mair:self.mair,hcr:self.hcr,scr:self.scr,epoch}}
}
#[repr(C)]#[derive(Clone,Copy,Debug,Default,PartialEq,Eq)]
pub struct Request {
    pub abi_version:u32,pub struct_size:u32,pub phase:u32,pub operation:u32,pub selector:u32,pub current_el:u32,pub flags:u32,pub reserved:u32,
    pub pc:u64,pub operand:u64,pub revision:u64,pub epoch:u64,pub before:Snapshot,pub candidate:Snapshot,
}
#[repr(C)]#[derive(Clone,Copy,Debug,Default,PartialEq,Eq)]
pub struct Reply {
    pub abi_version:u32,pub struct_size:u32,pub result:u32,pub detail:u32,pub phase:u32,pub operation:u32,pub selector:u32,pub state_tag:u32,
    pub token:u64,pub revision:u64,pub epoch:u64,pub invalidations:u64,pub architectural:Snapshot,pub effective:Snapshot,
}
pub type Callback=unsafe extern "C" fn(*mut c_void,*const Request,*mut Reply)->i32;

#[cfg(test)]mod tests {
    use super::*;
    #[test]fn dynamic_record_layouts_are_separate() {
        use core::mem::{size_of,align_of,offset_of};
        assert_eq!((size_of::<Snapshot>(),size_of::<Request>(),size_of::<Reply>()),(64,192,192));
        assert_eq!((align_of::<Snapshot>(),align_of::<Request>(),align_of::<Reply>()),(8,8,8));
        assert_eq!((offset_of!(Request,pc),offset_of!(Request,before),offset_of!(Request,candidate)),(32,64,128));
        assert_eq!((offset_of!(Reply,token),offset_of!(Reply,architectural),offset_of!(Reply,effective)),(32,64,128));
        assert_eq!((size_of::<DataRequest>(),size_of::<DataReply>()),(160,128));
    }
}
