//! New dynamic result composes the unchanged fixed-regime result fields.
use crate::memory_boot_v2::MemoryRunResultV2;
use nextcore_memory_service::dynamic_abi::Reply;
#[repr(C)]
#[derive(Clone,Copy,Debug,Default)]
pub struct MemoryRunResultDynamic {pub memory:MemoryRunResultV2,pub final_control:Reply}
const _:[();512]=[();core::mem::size_of::<MemoryRunResultDynamic>()];
const _:[();8]=[();core::mem::align_of::<MemoryRunResultDynamic>()];
const _:[();320]=[();core::mem::offset_of!(MemoryRunResultDynamic,final_control)];
