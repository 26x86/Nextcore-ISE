//! New boot result composes the existing canonical platform ABI.
use crate::platform::BootResultV2;
#[repr(C)]
#[derive(Clone,Copy,Debug,Default)]
pub struct MemoryRunResultV1 {
    pub abi_version:u32,pub struct_size:u32,pub provider_status:u32,pub reserved0:u32,
    pub execution:BootResultV2,
    pub guest_far:u64,pub last_address:u64,pub fetch_requests:u64,pub data_requests:u64,
    pub completed_data_operations:u64,pub reserved1:u64,
}
const _: [();192]=[();core::mem::size_of::<MemoryRunResultV1>()];
const _: [();8]=[();core::mem::align_of::<MemoryRunResultV1>()];
