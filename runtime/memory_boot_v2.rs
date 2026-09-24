//! New fixed-regime result; canonical v1/platform layouts remain unchanged.
use crate::memory_boot::MemoryRunResultV1;
use nextcore_memory_service::abi_v2::Reply;
#[repr(C)]
#[derive(Clone,Copy,Debug,Default)]
pub struct MemoryRunResultV2 {pub base:MemoryRunResultV1,pub last_reply:Reply}
const _:[();320]=[();core::mem::size_of::<MemoryRunResultV2>()];
const _:[();8]=[();core::mem::align_of::<MemoryRunResultV2>()];
