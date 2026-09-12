//! Linux host proof: generated x86 calls the canonical, caller-owned Rust service.
use core::ffi::c_void;
use core::mem::{align_of,offset_of,size_of};
use nextcore_memory_service::{abi::*,MemoryService,vf_memory_service_step};
#[path="preos/src/platform.rs"] mod platform;
#[path="memory_boot.rs"] mod memory_boot;
use memory_boot::MemoryRunResultV1 as Run;
use platform::BootOptionsV2;
type Protect=unsafe extern "C" fn(*mut c_void,usize,i32,*mut c_void)->i32;
unsafe extern "C" {
    fn mmap(p:*mut c_void,n:usize,prot:i32,flags:i32,fd:i32,off:isize)->*mut c_void;
    fn mprotect(p:*mut c_void,n:usize,prot:i32)->i32;
    fn munmap(p:*mut c_void,n:usize)->i32;
    fn vf_boot_run_memory_v1(base:u64,size:u64,entry:u64,args:u64,stack:u64,
        code:*mut u8,code_bytes:usize,budget:u64,protect:Option<Protect>,protect_owner:*mut c_void,
        initial:*const u64,pauth:*const c_void,options:*const BootOptionsV2,
        callback:Option<Callback>,owner:*mut c_void,result:*mut Run)->i32;
    fn vf_memory_layout(out:*mut u64,capacity:usize)->usize;
    fn vf_memory_mmu_gate_probe()->i32;
    fn vf_memory_state_probe(mode:u32,code:*mut u8,protect:Protect,callback:Callback,owner:*mut c_void,result:*mut Run)->i32;
}
unsafe extern "C" fn protect(p:*mut c_void,n:usize,execute:i32,_:*mut c_void)->i32 {
    unsafe{mprotect(p,n,if execute!=0 {5} else {3})}
}
struct Code(*mut u8);
impl Code {fn new()->Self {let p=unsafe{mmap(core::ptr::null_mut(),4096,3,0x22,-1,0)};
    assert_ne!(p as isize,-1);Self(p.cast())}}
impl Drop for Code {fn drop(&mut self){assert_eq!(unsafe{munmap(self.0.cast(),4096)},0);}}
struct Owner<'a>{service:MemoryService<'a>,requests:Vec<Request>,corruption:u32}
unsafe extern "C" fn callback(owner:*mut c_void,r:*const Request,out:*mut Reply)->i32 {
    let owner=unsafe{&mut *owner.cast::<Owner<'_>>()};let r=unsafe{*r};owner.requests.push(r);
    if owner.corruption==20 {return -7;}
    if owner.corruption==14 && r.operation==STORE {
        unsafe{out.write(Reply{abi_version:1,struct_size:80,value0:1,..Reply::default()})};return 0;
    }
    let status=unsafe{vf_memory_service_step((&mut owner.service as *mut MemoryService<'_>).cast(),&r,out)};
    if status!=0 {return status;}
    let out=unsafe{&mut *out};
    match owner.corruption {
        1=>out.abi_version=2,2=>out.struct_size=79,3=>out.epoch=1,4=>out.reserved[2]=1,
        5=>out.result=4,6=>out.value1=1,7=>out.value0=1<<32,8=>out.fault=3,
        9=>out.address=1,10=>out.esr=1,
        11 if r.operation!=FETCH=>out.esr^=64,
        12 if r.operation!=FETCH=>out.address+=1,
        13 if r.operation!=FETCH=>out.fault=PC_ALIGNMENT,
        14 if r.operation==STORE=>out.value0=1,
        15 if r.operation!=FETCH=>out.result=OK,
        _=>{}
    }0
}
const BASE:u64=0x40000000;
const HLT:u32=0xd4400000;
fn scalar(size:u32,opc:u32,imm:u32,rn:u32,rt:u32)->u32 {0x39000000|size<<30|opc<<22|imm<<10|rn<<5|rt}
fn pair(wide:bool,read:bool,mode:u32,imm:i32,rn:u32,rt:u32,rt2:u32)->u32 {
    0x28000000|u32::from(wide)<<31|u32::from(read)<<22|mode<<23|((imm as u32)&127)<<15|rt2<<10|rn<<5|rt
}
fn payload(words:&[u32],size:usize)->Vec<u8>{let mut ram=vec![0xa5;size];for(i,w)in words.iter().enumerate(){ram[i*4..i*4+4].copy_from_slice(&w.to_le_bytes());}ram}
fn run(ram:&mut[u8],initial:[u64;4],stack:u64,budget:u64,corruption:u32)->(Run,Vec<Request>) {
    let code=Code::new();let size=ram.len()as u64;
    let mut owner=Owner{service:MemoryService::new(ram,BASE).unwrap(),requests:Vec::new(),corruption};
    let mut result=Run::default();
    let status=unsafe{vf_boot_run_memory_v1(BASE,size,BASE,BASE+64,stack,code.0,4096,budget,
        Some(protect),core::ptr::null_mut(),initial.as_ptr(),core::ptr::null(),core::ptr::null(),
        Some(callback),(&mut owner as *mut Owner<'_>).cast(),&mut result)};
    assert_eq!(status as u32,result.execution.base.status);
    assert_eq!((result.abi_version,result.struct_size,result.reserved0,result.reserved1),(1,192,0,0));
    (result,owner.requests)
}
#[test]
fn actual_c_and_rust_layouts_match_every_field_and_mmu_gate_prevents_callbacks() {
    macro_rules! layout {($t:ty;$($f:ident),+)=>{[size_of::<$t>()as u64,align_of::<$t>()as u64,$(offset_of!($t,$f)as u64),+]};}
    let mut expected=layout!(Request;abi_version,struct_size,operation,flags,pc,address,value0,value1,sctlr,epoch,width,count,current_el,reserved).to_vec();
    expected.extend(layout!(Reply;abi_version,struct_size,result,fault,value0,value1,address,esr,epoch,reserved));
    expected.extend(layout!(Run;abi_version,struct_size,provider_status,reserved0,execution,guest_far,last_address,fetch_requests,data_requests,completed_data_operations,reserved1));
    let mut actual=vec![u64::MAX;expected.len()];
    assert_eq!(unsafe{vf_memory_layout(actual.as_mut_ptr(),actual.len())},expected.len());assert_eq!(actual,expected);
    assert_eq!(unsafe{vf_memory_mmu_gate_probe()},1);
}
#[test]
fn native_alu_and_memory_commit_through_actual_rust_callbacks() {
    let mut ram=payload(&[0xd2800540,scalar(3,0,0,1,0),scalar(3,1,0,1,2),0x91001443,HLT],256);
    let (r,requests)=run(&mut ram,[0,BASE+128,0,0],BASE+256,16,0);
    let b=r.execution.base;
    assert_eq!((b.status,b.retired,b.compiled_blocks,b.x0,b.x2,b.x3),(1,5,5,42,42,47));
    assert_eq!((r.provider_status,r.fetch_requests,r.data_requests,r.completed_data_operations),(0,5,2,2));
    assert_eq!(&ram[128..136],&42u64.to_le_bytes());
    assert_eq!(requests.iter().map(|q|q.operation).collect::<Vec<_>>(),[FETCH,FETCH,STORE,FETCH,LOAD,FETCH,FETCH]);
}
#[test]
fn conditional_comparison_stays_native_between_provider_fetches() {
    // First compare gives Z=1,C=1; the false NE replaces NZCV with 0xf.
    // NV then executes unconditionally and produces Z=1,C=1 again.
    let words=[0xfa41e000,0x3a41100f,0xfa5ff800,HLT];
    let mut ram=payload(&words,256);let before=ram.clone();
    let (r,_)=run(&mut ram,[31,31,0x33,0x44],BASE+256,8,0);
    assert_eq!((r.execution.base.status,r.execution.base.retired,r.execution.base.compiled_blocks,r.fetch_requests,r.data_requests),(1,4,4,4,0));
    assert_eq!((r.execution.pstate>>28,r.execution.base.x0,r.execution.base.x1,r.execution.base.x2,r.execution.base.x3),(6,31,31,0x33,0x44));
    assert_eq!(ram,before);
}
#[test]
fn all_thirteen_scalar_forms_use_provider_with_signed_and_zero_register_semantics() {
    for size in 0..4 {for opc in 0..4 {if opc>=2 && (size==3 || (size==2&&opc==3)){continue;}
        for sp in [false,true] {for zero in [false,true] {
            let bytes=1usize<<size;let rt=if zero {31}else{0};
            let mut ram=payload(&[scalar(size,opc,1,if sp {31}else{1},rt),HLT],256);
            let at=128+bytes;ram[at..at+bytes].fill(0x80);let before=ram.clone();
            let (r,requests)=run(&mut ram,[0xfedcba9876543280,BASE+128,0,0],if sp {BASE+128}else{BASE+256},8,0);
            assert_eq!((r.execution.base.status,r.execution.base.retired,r.data_requests,r.completed_data_operations),(1,2,1,1));
            let q=requests.iter().find(|q|q.operation!=FETCH).unwrap();assert_eq!((q.width,q.count,q.address),(bytes as u32,1,BASE+at as u64));
            if opc==0 {let value=if zero {0}else{0xfedcba9876543280u64};assert_eq!(&ram[at..at+bytes],&value.to_le_bytes()[..bytes]);}
            else {assert_eq!(ram,before);if !zero {
                let mut raw=0u64;for n in 0..bytes {raw|=0x80u64<<(n*8);}
                if opc>=2 {raw|=u64::MAX<<(bytes*8);}
                if opc==3 || (opc==1 && size<3) {raw=raw as u32 as u64;}
                assert_eq!(r.execution.base.x0,raw);
            }}
        }}
    }}
}
#[test]
fn pair_modes_widths_and_success_only_writeback_use_one_full_span_request() {
    for wide in [false,true] {for read in [false,true] {for mode in 1..=3 {for imm in [-2,2] {
        let width=if wide {8}else{4};let updated=(128i64+imm as i64*width)as usize;
        let at=if mode==1 {128}else{updated};
        let mut ram=payload(&[pair(wide,read,mode,imm,3,0,1),HLT],256);
        ram[at..at+width as usize].fill(0x80);ram[at+width as usize..at+width as usize*2].fill(0x7f);
        let before=ram.clone();let (r,qs)=run(&mut ram,[0x123456789abcdef0,0xfedcba9876543210,0,BASE+128],BASE+256,8,0);
        assert_eq!((r.execution.base.status,r.data_requests,r.completed_data_operations),(1,1,1));
        let q=qs.iter().find(|q|q.operation!=FETCH).unwrap();assert_eq!((q.width,q.count,q.address),(width as u32,2,BASE+at as u64));
        assert_eq!(r.execution.base.x3,if mode==2 {BASE+128}else{BASE+updated as u64});
        if read {assert_eq!(ram,before);assert_eq!(r.execution.base.x0,if wide {0x8080808080808080}else{0x80808080});assert_eq!(r.execution.base.x1,if wide {0x7f7f7f7f7f7f7f7f}else{0x7f7f7f7f});}
        else {assert_eq!(&ram[at..at+width as usize],&0x123456789abcdef0u64.to_le_bytes()[..width as usize]);assert_eq!(&ram[at+width as usize..at+width as usize*2],&0xfedcba9876543210u64.to_le_bytes()[..width as usize]);}
    }}}}
}
#[test]
fn missing_second_element_and_invalid_encoding_never_partially_commit() {
    for read in [false,true] {
        let mut ram=payload(&[pair(true,read,1,2,3,0,1),HLT],256);let before=ram.clone();
        let(r,_)=run(&mut ram,[11,12,13,BASE+248],BASE+256,8,0);
        assert_eq!((r.execution.base.status,r.provider_status,r.execution.base.retired,r.execution.base.compiled_blocks),(4,1,0,1));
        assert_eq!((r.last_address,r.guest_far,r.execution.esr,r.completed_data_operations),(BASE+256,0,0,0));
        assert_eq!((r.execution.base.x0,r.execution.base.x1,r.execution.base.x3),(11,12,BASE+248));assert_eq!(ram,before);
    }
    for w in [scalar(3,0,0,1,0)|(1<<26),scalar(3,2,0,1,0),pair(true,true,3,1,0,0,1)] {
        let mut ram=payload(&[w,HLT],256);let before=ram.clone();let(r,_)=run(&mut ram,[BASE+128,BASE+128,0,0],BASE+256,8,0);
        assert_eq!((r.execution.base.status,r.execution.base.retired,r.data_requests,r.execution.esr),(8,0,0,1<<25));assert_eq!(ram,before);
    }
}
#[test]
fn data_alignment_has_exact_esr_far_and_unchanged_memory_registers() {
    for read in [false,true] {for pair_access in [false,true] {
        let w=if pair_access {pair(true,read,1,2,3,0,1)}else{scalar(3,u32::from(read),0,3,0)};
        let mut ram=payload(&[w,HLT],256);let before=ram.clone();let(r,_)=run(&mut ram,[11,12,13,BASE+129],BASE+256,8,0);
        assert_eq!((r.execution.base.status,r.provider_status,r.execution.base.retired,r.completed_data_operations),(12,0,0,0));
        assert_eq!((r.guest_far,r.execution.esr,r.execution.elr),(BASE+129,0x96000021|if read{0}else{64},BASE));
        assert_eq!((r.execution.base.x0,r.execution.base.x1,r.execution.base.x3),(11,12,BASE+129));assert_eq!(ram,before);
    }}
}
#[test]
fn lower_el_fault_and_original_sp_priority_keep_the_precise_saved_state() {
    for mode in 0..2 {
        let instruction=if mode==0 {scalar(3,0,0,3,0)}else{pair(true,true,3,-1,31,0,1)};
        let mut ram=payload(&[instruction],256);let before=ram.clone();let code=Code::new();
        let mut owner=Owner{service:MemoryService::new(&mut ram,BASE).unwrap(),requests:Vec::new(),corruption:0};
        let mut r=Run::default();
        let status=unsafe{vf_memory_state_probe(mode,code.0,protect,callback,(&mut owner as *mut Owner<'_>).cast(),&mut r)};
        assert_eq!((status,r.execution.base.retired,r.fetch_requests,r.data_requests),(if mode==0{12}else{19},0,1,if mode==0{1}else{0}));
        assert_eq!(r.execution.esr,if mode==0{0x92000061}else{0x9a000000});
        assert_eq!((r.execution.base.x0,r.execution.base.x1,r.execution.sp),(11,12,BASE+136));
        // SP alignment preserves the existing runtime's saved diagnostic bank;
        // only the data-abort case asserts architecturally meaningful FAR.
        assert_eq!(r.guest_far,if mode==0{BASE+129}else{BASE+136});
        drop(owner);assert_eq!(ram,before);
    }
}
#[test]
fn only_committed_control_flow_is_fetched_and_pc_alignment_is_precise() {
    let mut ram=payload(&[0x14000002,0xffffffff,HLT],256);
    let(r,qs)=run(&mut ram,[0;4],BASE+256,8,0);
    assert_eq!((r.execution.base.status,r.execution.base.retired,r.fetch_requests),(1,2,2));
    assert_eq!(qs.iter().map(|q|q.address).collect::<Vec<_>>(),[BASE,BASE+8]);
    let(r,qs)=run(&mut ram,[0;4],BASE+256,1,0);
    assert_eq!((r.execution.base.status,r.execution.base.retired,r.fetch_requests),(5,1,1));assert_eq!(qs.len(),1);
    let mut ram=payload(&[0xd61f0000,HLT],256);let(r,_)=run(&mut ram,[BASE+2,0,0,0],BASE+256,8,0);
    assert_eq!((r.execution.base.status,r.execution.base.retired,r.execution.base.compiled_blocks,r.fetch_requests),(16,1,1,2));
    assert_eq!((r.execution.esr,r.guest_far,r.execution.elr,r.execution.base.fault_instruction),(0x8a000000,BASE+2,BASE+2,0));
}
#[test]
fn callback_failures_and_malformed_replies_are_not_guest_exceptions() {
    for corruption in 1..=10 {
        let mut ram=payload(&[HLT],256);let before=ram.clone();let(r,_)=run(&mut ram,[0;4],BASE+256,8,corruption);
        assert_eq!((r.execution.base.status,r.provider_status,r.execution.base.retired,r.execution.base.compiled_blocks),(4,3,0,0));
        assert_eq!((r.execution.esr,r.guest_far,r.execution.base.fault_instruction),(0,0,0));assert_eq!(ram,before);
    }
    for corruption in [11,12,13,15] {
        let mut ram=payload(&[scalar(3,0,0,3,0)],256);let before=ram.clone();let(r,_)=run(&mut ram,[7,0,0,BASE+129],BASE+256,8,corruption);
        assert_eq!((r.execution.base.status,r.provider_status,r.execution.base.retired),(4,3,0));
        assert_eq!((r.execution.esr,r.guest_far,r.execution.base.fault_instruction),(0,0,0));assert_eq!(ram,before);
    }
    let mut ram=payload(&[HLT],256);let(r,_)=run(&mut ram,[0;4],BASE+256,8,20);
    assert_eq!((r.execution.base.status,r.provider_status,r.execution.base.retired,r.execution.esr),(4,4,0,0));
    let mut ram=payload(&[scalar(3,0,0,3,0)],256);let before=ram.clone();
    let(r,_)=run(&mut ram,[7,0,0,BASE+128],BASE+256,8,14);
    assert_eq!((r.execution.base.status,r.provider_status,r.execution.base.retired,r.execution.esr),(4,3,0,0));assert_eq!(ram,before);
}
#[test]
fn public_entry_rejects_missing_callback_overflow_and_known_alias_before_effects() {
    let code=Code::new();let initial=[0;4];let mut owner=0u64;
    for (base,size,callback) in [(BASE,256,None),(u64::MAX-15,256,Some(callback as Callback)),(BASE,0,Some(callback as Callback))] {
        let mut result=Run::default();let rc=unsafe{vf_boot_run_memory_v1(base,size,base,base,base,code.0,4096,1,Some(protect),core::ptr::null_mut(),initial.as_ptr(),core::ptr::null(),core::ptr::null(),callback,(&mut owner as *mut u64).cast(),&mut result)};
        assert_eq!((rc,result.provider_status,result.fetch_requests),(4,2,0));
    }
    // Deliberately overlapping valid records must be rejected before result clear.
    let mut result=Run{abi_version:99,..Run::default()};let original=result.abi_version;
    let rc=unsafe{vf_boot_run_memory_v1(BASE,256,BASE,BASE+64,BASE+256,code.0,4096,1,Some(protect),core::ptr::null_mut(),(&result as *const Run).cast(),core::ptr::null(),core::ptr::null(),Some(callback),(&mut owner as *mut u64).cast(),&mut result)};
    assert_eq!((rc,result.abi_version),(4,original));
}

#[path="ubfm_provider_cases.rs"]mod ubfm_cases;
#[test]fn ubfm_is_native_with_v1_callbacks_and_preserves_state() {
    for case in ubfm_cases::cases() {
        let mut ram=payload(&[case.word,HLT],256);let before=ram.clone();
        let mut expected=[0xaau64,case.source,8,0xfedcba9876543210];let initial=expected;
        if case.destination!=31 {expected[case.destination]=case.expected;}
        let(r,requests)=run(&mut ram,initial,BASE+256,4,0);let b=r.execution.base;
        assert_eq!((b.status,b.retired,b.compiled_blocks),(1,2,2));
        assert_eq!([b.x0,b.x1,b.x2,b.x3],expected);assert_eq!((b.pc,r.execution.sp,r.execution.pstate),(BASE+8,BASE+256,0x3c5));
        assert_eq!((r.provider_status,r.fetch_requests,r.data_requests,r.completed_data_operations),(0,2,0,0));
        assert_eq!((r.execution.esr,r.guest_far),(0,0));assert!(requests.iter().all(|q|q.operation==FETCH));assert_eq!(ram,before);
    }
    for word in ubfm_cases::INVALID {
        let mut ram=payload(&[word,HLT],256);let before=ram.clone();let initial=[1,2,3,4];
        let(r,_) =run(&mut ram,initial,BASE+256,4,0);let b=r.execution.base;
        assert_eq!((b.status,b.retired,b.compiled_blocks,r.fetch_requests),(8,0,1,1));
        assert_eq!([b.x0,b.x1,b.x2,b.x3],initial);assert_eq!((b.pc,r.execution.esr,r.data_requests),(BASE,1<<25,0));assert_eq!(ram,before);
    }
}

#[test]
fn extended_arithmetic_native_through_v1_provider() {
    let words = [0x8b218003, 0xcb210c62, 0xeb21c05f, HLT];
    let mut ram = payload(&words, 256); let before = ram.clone();
    let (r, requests) = run(&mut ram, [0, 0xff, 0, 0], BASE + 256, 8, 0);
    let b = r.execution.base;
    assert_eq!((b.status, b.retired, b.compiled_blocks, b.x2, b.x3), (1, 4, 4, u64::MAX - 2040, u64::MAX));
    assert_eq!((r.execution.pstate >> 28, r.fetch_requests, r.data_requests), (10, 4, 0));
    assert!(requests.iter().all(|q| q.operation == FETCH)); assert_eq!(ram, before);
}

#[test]
fn conditional_selection_native_through_v1_provider() {
    let mut ram = payload(&[0xf100143f, 0x9a9f0022, 0x9a8217e2, 0x5a8113e0, 0xda8217e3, HLT], 256); let before = ram.clone();
    let (r, requests) = run(&mut ram, [u64::MAX, 5, 0, 0], BASE + 256, 16, 0);
    let b = r.execution.base;
    assert_eq!((b.status, b.retired, b.compiled_blocks), (1, 6, 6));
    assert_eq!((b.x0, b.x1, b.x2, b.x3), (0xfffffffa, 5, 6, u64::MAX - 5));
    assert_eq!((r.execution.pstate, r.fetch_requests, r.data_requests), (0x600003c5, 6, 0));
    assert!(requests.iter().all(|q| q.operation == FETCH)); assert_eq!(ram, before);
}

#[path="register_offset_provider_cases.rs"]mod register_offset_cases;
#[test]fn register_offset_v1_all_forms_and_host_or_guest_failures() {
    for case in register_offset_cases::cases() {
        let address=BASE+128;let mut ram=payload(&[case.word,HLT],256);
        ram[128..136].copy_from_slice(&0x80ff7f0102030480u64.to_le_bytes());
        let initial=[0, address.wrapping_sub(case.offset), case.index, 0x1234567887654321];
        let mut expected_regs=initial;let mut expected_ram=ram.clone();register_offset_cases::expected(&case,&mut expected_regs,&mut expected_ram,128);
        let (r,requests)=run(&mut ram,initial,BASE+256,8,0);let b=r.execution.base;
        assert_eq!((b.status,b.retired,b.compiled_blocks),(1,2,2));assert_eq!([b.x0,b.x1,b.x2,b.x3],expected_regs);
        assert_eq!((r.execution.pstate,r.execution.sp,r.data_requests,r.completed_data_operations),(0x3c5,BASE+256,1,1));
        let data=requests.iter().find(|q|q.operation!=FETCH).unwrap();assert_eq!((data.address,data.width,data.count),(address,case.bytes as u32,1));assert_eq!(ram,expected_ram);
    }
    for word in register_offset_cases::invalid() {
        let mut ram=payload(&[word,HLT],256);let before=ram.clone();let initial=[1,BASE+128,0,3];
        let(r,_)=run(&mut ram,initial,BASE+256,8,0);let b=r.execution.base;
        assert_eq!((b.status,b.retired,r.data_requests,r.execution.esr),(8,0,0,1<<25));assert_eq!([b.x0,b.x1,b.x2,b.x3],initial);assert_eq!(ram,before);
    }
    for read in [false,true] {for fault in 0..3 {
        let word=register_offset_cases::word(1,u32::from(read),6,1,1,2,2);
        let address=if fault==0 {BASE+256}else{BASE+129};let initial=[1,address+2,u64::MAX,3];
        let mut ram=payload(&[word,HLT],256);let before=ram.clone();let(r,_)=run(&mut ram,initial,BASE+256,8,if fault==2 {11}else{0});let b=r.execution.base;
        assert_eq!((b.status,b.retired,r.completed_data_operations),(if fault==1 {12}else{4},0,0));
        assert_eq!([b.x0,b.x1,b.x2,b.x3],initial);assert_eq!(ram,before);
        assert_eq!(r.provider_status,if fault==0 {1}else if fault==2 {3}else{0});
        assert_eq!(r.execution.esr,if fault==1 {0x96000021|if read {0}else{64}}else{0});
        assert_eq!(r.guest_far,if fault==1 {address}else{0});
    }}
}

fn test_bit_branch(op:u32,bit:u32,imm:i32,rt:u32)->u32 {
    0x36000000|op<<24|(bit>>5)<<31|(bit&31)<<19|((imm as u32)&0x3fff)<<5|rt
}
#[test]fn test_bit_v1_fetches_only_committed_pc_and_retains_branch_on_fault() {
    for bit in 0..64 {for op in 0..2 {for set in [false,true] {for zr in [false,true] {
        let source=if set {1u64<<bit}else{0};let take=(set&&!zr)==(op!=0);
        let word=test_bit_branch(op,bit,2,if zr {31}else{1});let mut ram=payload(&[word,HLT,HLT],256);let before=ram.clone();let initial=[0,source,8,9];
        let(r,requests)=run(&mut ram,initial,BASE+256,8,0);let b=r.execution.base;
        assert_eq!((b.status,b.retired,b.compiled_blocks,b.pc),(1,2,2,BASE+if take {12}else{8}));
        assert_eq!([b.x0,b.x1,b.x2,b.x3],initial);assert_eq!((r.execution.pstate,r.execution.sp,r.fetch_requests,r.data_requests),(0x3c5,BASE+256,2,0));
        assert_eq!(requests.iter().map(|q|q.address).collect::<Vec<_>>(),[BASE,BASE+if take {8}else{4}]);assert_eq!(ram,before);
    }}}}
    let mut ram=payload(&[0xd1000421,test_bit_branch(1,0,-1,1),HLT],256);
    let(r,requests)=run(&mut ram,[0,2,8,9],BASE+256,8,0);
    assert_eq!((r.execution.base.status,r.execution.base.retired,r.execution.base.x1,r.fetch_requests),(1,5,0,5));
    assert_eq!(requests.iter().map(|q|q.address).collect::<Vec<_>>(),[BASE,BASE+4,BASE,BASE+4,BASE+8]);
    let mut ram=payload(&[test_bit_branch(0,63,-8192,31),HLT],256);let before=ram.clone();let(r,requests)=run(&mut ram,[1,2,3,4],BASE+256,8,0);let b=r.execution.base;
    assert_eq!((b.status,b.retired,b.compiled_blocks,r.fetch_requests,r.provider_status),(4,1,1,2,1));
    assert_eq!((b.pc,r.last_address),(BASE-32768,BASE-32768));assert_eq!(requests[1].address,BASE-32768);assert_eq!(ram,before);
}
