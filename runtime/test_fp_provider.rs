//! Authored native C/Rust consumer for the separate incomplete FP research entry.
//! Compile with `--cfg nextcore_fp_execution` only when the C objects also use
//! NEXTCORE_FP_EXECUTION. The same fixture checks unavailable-entry rejection
//! when both compile-time selections are omitted. No original images are used.
#![allow(dead_code)]

use core::ffi::c_void;
use nextcore_memory_service::{
    abi::FETCH,
    abi_v2::{Callback, Controls, Reply, Request},
    stage1::{vf_memory_service_step_v2, MemoryServiceV2},
};
#[path = "memory_boot.rs"]
mod memory_boot;
#[path = "memory_boot_v2.rs"]
mod memory_boot_v2;
#[path = "preos/src/platform.rs"]
mod platform;
use memory_boot_v2::MemoryRunResultV2 as Run;

type Protect = unsafe extern "C" fn(*mut c_void, usize, i32, *mut c_void) -> i32;
type Pauth = unsafe extern "C" fn(*mut c_void, u32) -> i32;
unsafe extern "C" {
    fn mmap(p: *mut c_void, n: usize, prot: i32, flags: i32, fd: i32, off: isize) -> *mut c_void;
    fn mprotect(p: *mut c_void, n: usize, prot: i32) -> i32;
    fn munmap(p: *mut c_void, n: usize) -> i32;
    fn vf_boot_run_memory_v2(
        base: u64,
        size: u64,
        entry: u64,
        args: u64,
        stack: u64,
        code: *mut u8,
        code_bytes: usize,
        budget: u64,
        protect: Option<Protect>,
        opaque: *mut c_void,
        initial: *const u64,
        options: *const platform::BootOptionsV2,
        controls: *const Controls,
        callback: Option<Callback>,
        owner: *mut c_void,
        result: *mut Run,
    ) -> i32;
    fn vf_boot_run_memory_pauth_v2(
        base: u64,
        size: u64,
        entry: u64,
        args: u64,
        stack: u64,
        code: *mut u8,
        code_bytes: usize,
        budget: u64,
        protect: Option<Protect>,
        opaque: *mut c_void,
        initial: *const u64,
        pauth: Option<Pauth>,
        options: *const platform::BootOptionsV2,
        controls: *const Controls,
        callback: Option<Callback>,
        owner: *mut c_void,
        result: *mut Run,
    ) -> i32;
    fn vf_boot_run_memory_fp_research_v2(
        base: u64,
        size: u64,
        entry: u64,
        args: u64,
        stack: u64,
        code: *mut u8,
        code_bytes: usize,
        budget: u64,
        protect: Option<Protect>,
        opaque: *mut c_void,
        initial: *const u64,
        pauth: Option<Pauth>,
        initial_cpacr: u64,
        options: *const platform::BootOptionsV2,
        controls: *const Controls,
        callback: Option<Callback>,
        owner: *mut c_void,
        result: *mut Run,
    ) -> i32;
}

const RAM: u64 = 0x40000000;
const TABLES: u64 = 0x10000000;
const VA: u64 = 0x10000;
const HLT: u32 = 0xd4400000;
const MRS_FPCR_X0: u32 = 0xd53b4400;
const MSR_CPACR_X0: u32 = 0xd5181040;
const FP_ENABLED: u64 = 0x300000;
const PROVIDER_INVALID_REQUEST: u32 = 2;

struct Code(*mut u8);
impl Code {
    fn new() -> Self {
        let p = unsafe { mmap(core::ptr::null_mut(), 4096, 3, 0x22, -1, 0) };
        assert_ne!(p as isize, -1);
        Self(p.cast())
    }
}
impl Drop for Code {
    fn drop(&mut self) {
        assert_eq!(unsafe { munmap(self.0.cast(), 4096) }, 0);
    }
}
unsafe extern "C" fn protect(p: *mut c_void, n: usize, x: i32, opaque: *mut c_void) -> i32 {
    unsafe { *opaque.cast::<usize>() += 1 };
    unsafe { mprotect(p, n, if x != 0 { 5 } else { 3 }) }
}
unsafe extern "C" fn reject_pauth(_: *mut c_void, _: u32) -> i32 {
    // These authored programs contain no PAC operations. Unsupported PAC never
    // acquires fake semantics if an unexpected decoder route invokes this hook.
    8
}
struct Owner<'a> {
    service: MemoryServiceV2<'a>,
    requests: Vec<Request>,
}
unsafe extern "C" fn callback(owner: *mut c_void, q: *const Request, out: *mut Reply) -> i32 {
    let owner = unsafe { &mut *owner.cast::<Owner<'_>>() };
    let request = unsafe { *q };
    owner.requests.push(request);
    unsafe {
        vf_memory_service_step_v2(
            (&mut owner.service as *mut MemoryServiceV2<'_>).cast(),
            &request,
            out,
        )
    }
}

fn fixture(sixteen: bool, words: &[u32]) -> (Controls, Vec<u8>, Vec<u8>) {
    let step = if sixteen { 0x4000 } else { 0x1000 };
    let mut tables = vec![0; step * 4];
    let mut ram = vec![0xa5; step * 4];
    let (start, bits, page) = if sixteen { (1, 11, 14) } else { (0, 9, 12) };
    for level in start..=3 {
        let n = level - start;
        let index = ((VA >> (page + bits * (3 - level))) & ((1 << bits) - 1)) as usize;
        let at = n * step + index * 8;
        let value = if level == 3 {
            RAM | 0x403
        } else {
            TABLES + ((n + 1) * step) as u64 | 3
        };
        tables[at..at + 8].copy_from_slice(&value.to_le_bytes());
    }
    for (i, word) in words.iter().enumerate() {
        ram[4 * i..4 * i + 4].copy_from_slice(&word.to_le_bytes());
    }
    let tcr = if sixteen {
        17 | (17 << 16) | (2 << 14) | (1 << 30) | (5 << 32)
    } else {
        16 | (16 << 16) | (2 << 30) | (5 << 32)
    };
    let controls = Controls {
        abi_version: 2,
        struct_size: 80,
        profile: 1,
        sctlr: 0x30d00803,
        ttbr0: TABLES,
        ttbr1: TABLES,
        tcr,
        mair: 0x44,
        epoch: 1,
        ..Controls::default()
    };
    (controls, tables, ram)
}

#[derive(Clone, Copy)]
enum Entry {
    Scalar,
    LegacyPauth,
    Research(u64),
}
fn run(
    entry: Entry,
    controls: Controls,
    tables: &[u8],
    ram: &mut [u8],
    initial: [u64; 4],
    options: Option<&platform::BootOptionsV2>,
) -> (Run, Vec<Request>, usize) {
    let code = Code::new();
    let size = ram.len() as u64;
    let mut protections = 0usize;
    let mut owner = Owner {
        service: MemoryServiceV2::new(ram, RAM, tables, TABLES, controls).unwrap(),
        requests: Vec::new(),
    };
    let mut result = Run::default();
    let options = options.map_or(core::ptr::null(), |v| v);
    let opaque = (&mut protections as *mut usize).cast();
    let owner_ptr = (&mut owner as *mut Owner<'_>).cast();
    let status = unsafe {
        match entry {
            Entry::Scalar => vf_boot_run_memory_v2(
                RAM,
                size,
                VA,
                initial[0],
                VA + 0x400,
                code.0,
                4096,
                64,
                Some(protect),
                opaque,
                initial.as_ptr(),
                options,
                &controls,
                Some(callback),
                owner_ptr,
                &mut result,
            ),
            Entry::LegacyPauth => vf_boot_run_memory_pauth_v2(
                RAM,
                size,
                VA,
                initial[0],
                VA + 0x400,
                code.0,
                4096,
                64,
                Some(protect),
                opaque,
                initial.as_ptr(),
                Some(reject_pauth),
                options,
                &controls,
                Some(callback),
                owner_ptr,
                &mut result,
            ),
            Entry::Research(cpacr) => vf_boot_run_memory_fp_research_v2(
                RAM,
                size,
                VA,
                initial[0],
                VA + 0x400,
                code.0,
                4096,
                64,
                Some(protect),
                opaque,
                initial.as_ptr(),
                Some(reject_pauth),
                cpacr,
                options,
                &controls,
                Some(callback),
                owner_ptr,
                &mut result,
            ),
        }
    };
    assert_eq!(status as u32, result.base.execution.base.status);
    assert_eq!((result.base.abi_version, result.base.struct_size), (2, 320));
    assert_eq!(core::mem::size_of::<Run>(), 320);
    (result, owner.requests, protections)
}
fn registers(result: &Run) -> [u64; 4] {
    let b = result.base.execution.base;
    [b.x0, b.x1, b.x2, b.x3]
}
fn assert_fetches(requests: &[Request], controls: Controls, count: usize) {
    assert_eq!(requests.len(), count);
    for (i, q) in requests.iter().enumerate() {
        assert_eq!(
            (q.operation, q.pc, q.address, q.controls),
            (FETCH, VA + 4 * i as u64, VA + 4 * i as u64, controls)
        );
    }
}

#[test]
fn legacy_entries_keep_fp_absent_in_both_compile_modes() {
    for sixteen in [false, true] {
        for entry in [Entry::Scalar, Entry::LegacyPauth] {
            let (c, tables, mut ram) = fixture(sixteen, &[MRS_FPCR_X0, HLT]);
            let before = ram.clone();
            let initial = [1, 2, 3, 4];
            let (r, requests, _) = run(entry, c, &tables, &mut ram, initial, None);
            let b = r.base.execution.base;
            assert_eq!(
                (b.status, b.retired, b.pc, b.fault_instruction),
                (8, 0, VA, MRS_FPCR_X0)
            );
            assert_eq!(registers(&r), initial);
            assert_eq!(
                (
                    r.base.provider_status,
                    r.base.data_requests,
                    r.base.execution.esr
                ),
                (0, 0, 1 << 25)
            );
            assert_fetches(&requests, c, 1);
            assert_eq!(ram, before);
        }
    }
}

#[cfg(not(nextcore_fp_execution))]
#[test]
fn uncompiled_research_entry_rejects_before_fetch_or_code_protection() {
    for cpacr in [0, FP_ENABLED, u64::MAX] {
        let (c, tables, mut ram) = fixture(false, &[MRS_FPCR_X0, HLT]);
        let before = ram.clone();
        let (r, requests, protections) = run(
            Entry::Research(cpacr),
            c,
            &tables,
            &mut ram,
            [1, 2, 3, 4],
            None,
        );
        assert_eq!(
            (
                r.base.execution.base.status,
                r.base.execution.base.retired,
                r.base.provider_status
            ),
            (4, 0, PROVIDER_INVALID_REQUEST)
        );
        assert_eq!(
            (r.base.fetch_requests, r.base.data_requests, protections),
            (0, 0, 0)
        );
        assert!(requests.is_empty());
        assert_eq!(ram, before);
    }
}

#[cfg(nextcore_fp_execution)]
#[test]
fn zero_software_handoff_traps_fpcr_precisely_without_retirement() {
    for sixteen in [false, true] {
        let (c, tables, mut ram) = fixture(sixteen, &[MRS_FPCR_X0, HLT]);
        let before = ram.clone();
        let initial = [1, 2, 3, 4];
        let options = platform::BootOptionsV2 {
            abi_version: 2,
            struct_size: 64,
            initial_pstate: 0x3c5,
            vbar: VA + 0x800,
            ..Default::default()
        };
        let (r, requests, _) = run(
            Entry::Research(0),
            c,
            &tables,
            &mut ram,
            initial,
            Some(&options),
        );
        let b = r.base.execution.base;
        assert_eq!(
            (b.status, b.retired, b.pc, b.fault_instruction),
            (21, 0, VA, MRS_FPCR_X0)
        );
        assert_eq!(registers(&r), initial);
        assert_eq!(
            (
                r.base.execution.esr,
                r.base.execution.elr,
                r.base.execution.spsr
            ),
            (0x1fe00000, VA, 0x3c5)
        );
        // Recording a synchronous exception does not dispatch its vector.
        assert_eq!(
            (r.base.execution.exception_vector, r.base.guest_far),
            (0, 0)
        );
        assert_eq!(
            (
                r.base.provider_status,
                r.base.data_requests,
                r.base.completed_data_operations
            ),
            (0, 0, 0)
        );
        assert_fetches(&requests, c, 1);
        assert_eq!(ram, before);
    }
}

#[cfg(nextcore_fp_execution)]
#[test]
fn actual_guest_cpacr_write_enables_live_control_arithmetic_and_bitwise_retirement() {
    let words = [
        MSR_CPACR_X0,
        0xd51b4403,
        MRS_FPCR_X0,
        0x1e270020,
        0x1e270041,
        0x1e212802,
        0x1e260042,
        0x6e211c02,
        0x1e260041,
        0xd5381043,
        HLT,
    ];
    for sixteen in [false, true] {
        let (c, tables, mut ram) = fixture(sixteen, &words);
        let before = ram.clone();
        let (r, requests, _) = run(
            Entry::Research(0),
            c,
            &tables,
            &mut ram,
            [FP_ENABLED, 0x3f800000, 0x40000000, 0x00400000],
            None,
        );
        let b = r.base.execution.base;
        assert_eq!(
            (b.status, b.retired, b.pc, b.compiled_blocks),
            (1, 11, VA + 44, 11)
        );
        assert_eq!(
            registers(&r),
            [0x00400000, 0x7f800000, 0x40400000, FP_ENABLED]
        );
        assert_eq!(
            (
                r.base.provider_status,
                r.base.data_requests,
                r.base.execution.esr
            ),
            (0, 0, 0)
        );
        assert_fetches(&requests, c, words.len());
        assert_eq!(ram, before);
    }
}

#[cfg(nextcore_fp_execution)]
#[test]
fn explicit_enabled_software_handoff_retires_fpcr_before_guest_cpacr_write() {
    let words = [
        MRS_FPCR_X0,
        0x1e270020,
        0x1e270041,
        0x1e212802,
        0x1e260043,
        0xd53b4422,
        HLT,
    ];
    for sixteen in [false, true] {
        let (c, tables, mut ram) = fixture(sixteen, &words);
        let before = ram.clone();
        let (r, requests, _) = run(
            Entry::Research(FP_ENABLED),
            c,
            &tables,
            &mut ram,
            [0xfeed, 0x3f800000, 0x40000000, 0xbeef],
            None,
        );
        let b = r.base.execution.base;
        assert_eq!(
            (b.status, b.retired, b.pc, b.compiled_blocks),
            (1, 7, VA + 28, 7)
        );
        assert_eq!(registers(&r), [0, 0x3f800000, 0, 0x40400000]);
        assert_eq!(
            (
                r.base.provider_status,
                r.base.data_requests,
                r.base.execution.esr
            ),
            (0, 0, 0)
        );
        assert_fetches(&requests, c, words.len());
        assert_eq!(ram, before);
    }
}

#[cfg(nextcore_fp_execution)]
#[test]
fn coverage_gaps_stop_host_without_guest_exception_or_data_provider() {
    for sixteen in [false, true] {
        for cpacr in [0, FP_ENABLED] {
            for word in [0x1e200800, 0x4e208400, 0x3dc00000] {
                let (c, tables, mut ram) = fixture(sixteen, &[word, HLT]);
                let before = ram.clone();
                let initial = [1, 2, 3, 4];
                let (r, requests, _) =
                    run(Entry::Research(cpacr), c, &tables, &mut ram, initial, None);
                let b = r.base.execution.base;
                assert_eq!(
                    (b.status, b.retired, b.pc, b.fault_instruction),
                    (20, 0, VA, word)
                );
                assert_eq!(registers(&r), initial);
                assert_eq!(
                    (
                        r.base.execution.esr,
                        r.base.execution.elr,
                        r.base.execution.spsr,
                        r.base.execution.exception_vector,
                        r.base.guest_far
                    ),
                    (0, 0, 0, 0, 0)
                );
                assert_eq!(
                    (
                        r.base.provider_status,
                        r.base.data_requests,
                        r.base.completed_data_operations
                    ),
                    (0, 0, 0)
                );
                assert_fetches(&requests, c, 1);
                assert_eq!(ram, before);
            }
        }
    }
}

#[cfg(nextcore_fp_execution)]
#[test]
fn guest_ttbr_write_still_rejects_immutable_controls_after_fp_control() {
    for sixteen in [false, true] {
        let (c, tables, mut ram) = fixture(sixteen, &[MRS_FPCR_X0, 0xd5182001, HLT]);
        let before = ram.clone();
        let (r, requests, _) = run(
            Entry::Research(FP_ENABLED),
            c,
            &tables,
            &mut ram,
            [1, TABLES + 0x4000, 3, 4],
            None,
        );
        let b = r.base.execution.base;
        assert_eq!(
            (b.status, b.retired, b.pc, b.fault_instruction),
            (13, 1, VA + 4, 0xd5182001)
        );
        assert_eq!(registers(&r), [0, TABLES + 0x4000, 3, 4]);
        assert_eq!((r.base.provider_status, r.base.data_requests), (0, 0));
        assert_fetches(&requests, c, 2);
        assert_eq!(ram, before);
    }
}

#[cfg(nextcore_fp_execution)]
#[test]
fn invalid_software_handoff_and_el0_entry_reject_before_fetch_or_code_protection() {
    for cpacr in [1, 0x400000, u64::MAX] {
        let (c, tables, mut ram) = fixture(false, &[MRS_FPCR_X0, HLT]);
        let before = ram.clone();
        let (r, requests, protections) = run(
            Entry::Research(cpacr),
            c,
            &tables,
            &mut ram,
            [1, 2, 3, 4],
            None,
        );
        assert_eq!(
            (
                r.base.execution.base.status,
                r.base.execution.base.retired,
                r.base.provider_status
            ),
            (4, 0, PROVIDER_INVALID_REQUEST)
        );
        assert_eq!(
            (r.base.fetch_requests, r.base.data_requests, protections),
            (0, 0, 0)
        );
        assert!(requests.is_empty());
        assert_eq!(ram, before);
    }
    let (c, tables, mut ram) = fixture(false, &[MRS_FPCR_X0, HLT]);
    let before = ram.clone();
    let options = platform::BootOptionsV2 {
        abi_version: 2,
        struct_size: 64,
        initial_pstate: 0x3c0,
        ..Default::default()
    };
    let (r, requests, protections) = run(
        Entry::Research(0),
        c,
        &tables,
        &mut ram,
        [1, 2, 3, 4],
        Some(&options),
    );
    assert_eq!(
        (
            r.base.execution.base.status,
            r.base.execution.base.retired,
            r.base.provider_status
        ),
        (4, 0, PROVIDER_INVALID_REQUEST)
    );
    assert_eq!(
        (r.base.fetch_requests, r.base.data_requests, protections),
        (0, 0, 0)
    );
    assert!(requests.is_empty());
    assert_eq!(ram, before);
}
