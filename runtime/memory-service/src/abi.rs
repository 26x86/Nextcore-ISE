//! Fixed-width callback ABI; no Rust ownership or enum layout crosses C.
use core::ffi::c_void;
pub const ABI_VERSION:u32=1;
pub const FETCH:u32=1;pub const LOAD:u32=2;pub const STORE:u32=3;
pub const OK:u32=0;pub const GUEST_FAULT:u32=1;pub const UNSUPPORTED:u32=2;pub const INVALID_REQUEST:u32=3;
pub const PC_ALIGNMENT:u32=1;pub const DATA_ALIGNMENT:u32=2;
#[repr(C)]
#[derive(Clone,Copy,Debug,Default)]
pub struct Request {
    pub abi_version:u32,pub struct_size:u32,pub operation:u32,pub flags:u32,
    pub pc:u64,pub address:u64,pub value0:u64,pub value1:u64,pub sctlr:u64,pub epoch:u64,
    pub width:u32,pub count:u32,pub current_el:u32,pub reserved:u32,
}
#[repr(C)]
#[derive(Clone,Copy,Debug,Default)]
pub struct Reply {
    pub abi_version:u32,pub struct_size:u32,pub result:u32,pub fault:u32,
    pub value0:u64,pub value1:u64,pub address:u64,pub esr:u64,pub epoch:u64,pub reserved:[u64;3],
}
pub type Callback=unsafe extern "C" fn(*mut c_void,*const Request,*mut Reply)->i32;
const _: [();80]=[();core::mem::size_of::<Request>()];
const _: [();80]=[();core::mem::size_of::<Reply>()];
const _: [();8]=[();core::mem::align_of::<Request>()];
const _: [();8]=[();core::mem::align_of::<Reply>()];
