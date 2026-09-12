//! Authored generated-x86 / canonical Rust control-and-memory execution proof.
use core::ffi::c_void;
use nextcore_memory_service::{abi::{LOAD,STORE},abi_v2 as m,dynamic_abi as c,
    dynamic::{MemoryServiceDynamic,vf_memory_dynamic_control_v1,vf_memory_dynamic_step_v1}};
#[path="preos/src/platform.rs"]mod platform;
#[path="memory_boot.rs"]mod memory_boot;
#[path="memory_boot_v2.rs"]mod memory_boot_v2;
#[path="memory_dynamic.rs"]mod memory_dynamic;
use memory_dynamic::MemoryRunResultDynamic as Run;
type Protect=unsafe extern "C" fn(*mut c_void,usize,i32,*mut c_void)->i32;
unsafe extern "C" {
    fn mmap(p:*mut c_void,n:usize,prot:i32,flags:i32,fd:i32,off:isize)->*mut c_void;
    fn mprotect(p:*mut c_void,n:usize,prot:i32)->i32;
    fn munmap(p:*mut c_void,n:usize)->i32;
    fn vf_dynamic_layout(out:*mut u64,capacity:usize)->usize;
    fn vf_dynamic_cpu_probe(code:*mut u8,code_bytes:usize,controls:*const m::Controls,
        memory:Option<m::Callback>,control:Option<c::Callback>,owner:*mut c_void,protect:Option<Protect>,
        entry:u64,enable:u64,observed:*mut u64,result:*mut Run)->i32;
    fn vf_boot_run_memory_dynamic(base:u64,size:u64,entry:u64,args:u64,stack:u64,
        code:*mut u8,code_bytes:usize,budget:u64,protect:Option<Protect>,opaque:*mut c_void,
        initial:*const u64,options:*const platform::BootOptionsV2,controls:*const m::Controls,
        memory:Option<m::Callback>,control:Option<c::Callback>,owner:*mut c_void,result:*mut Run)->i32;
}
unsafe extern "C" fn protect(p:*mut c_void,n:usize,x:i32,_:*mut c_void)->i32 {unsafe{mprotect(p,n,if x!=0 {5}else{3})}}
struct Code(*mut u8);
impl Code {fn new()->Self {let p=unsafe{mmap(core::ptr::null_mut(),4096,3,0x22,-1,0)};assert_ne!(p as isize,-1);Self(p.cast())}}
impl Drop for Code {fn drop(&mut self){assert_eq!(unsafe{munmap(self.0.cast(),4096)},0);}}
const RAM:u64=0x40000000;const TABLE:u64=0x10000000;
const HLT:u32=0xd4400000;const ISB:u32=0xd5033fdf;const DSB:u32=0xd5033f9f;const TLBI:u32=0xd508871f;
struct Owner<'a>{service:MemoryServiceDynamic<'a>,data:Vec<m::Request>,controls:Vec<(c::Request,c::Reply)>,corrupt:u32}
unsafe extern "C" fn memory(owner:*mut c_void,q:*const m::Request,out:*mut m::Reply)->i32 {
    let o=unsafe{&mut *owner.cast::<Owner<'_>>()};let q=unsafe{*q};o.data.push(q);
    let status=unsafe{vf_memory_dynamic_step_v1((&mut o.service as *mut MemoryServiceDynamic<'_>).cast(),&q,out)};
    if status!=0{return status;}let r=unsafe{&mut *out};
    if o.corrupt>=100 {
        let clean=m::Reply{abi_version:3,struct_size:128,level:m::NO_LEVEL,epoch:q.controls.epoch,..Default::default()};
        match o.corrupt {
            101=>*r=m::Reply{value0:HLT as u64,..clean},
            102=>*r=m::Reply{result:m::GUEST_FAULT,fault:m::TRANSLATION,fsc:7,esr:0x86000007,
                address:q.address,level:3,context:m::WALK,metadata_flags:m::HAS_DESCRIPTOR,descriptor_pa:TABLE,..clean},
            103=>*r=m::Reply{result:m::UNAVAILABLE,address:q.address,metadata_flags:m::HAS_OUTPUT,output_pa:q.address+4,..clean},
            104=>r.abi_version=2,_=>{}
        }
    }0
}
unsafe extern "C" fn control(owner:*mut c_void,q:*const c::Request,out:*mut c::Reply)->i32 {
    let o=unsafe{&mut *owner.cast::<Owner<'_>>()};let q=unsafe{*q};
    let code=unsafe{vf_memory_dynamic_control_v1((&mut o.service as *mut MemoryServiceDynamic<'_>).cast(),&q,out)};
    if code!=0{return code;}let r=unsafe{&mut *out};o.controls.push((q,*r));
    if q.phase==c::PREPARE {match o.corrupt {1=>r.token=0,2=>r.revision+=1,3=>r.effective=r.architectural,
        4=>r.epoch+=1,5=>r.architectural.reserved=1,6=>r.phase=c::COMMIT,7=>return 1,_=>{}}}
    if q.phase==c::COMMIT {match o.corrupt {8=>r.epoch+=1,9=>return 1,10=>r.token=7,11=>r.state_tag=c::PROPOSED,
        12=>r.result=c::UNSUPPORTED,_=>{}}}0
}
fn words(ram:&mut[u8],at:usize,words:&[u32]) {for(i,w)in words.iter().enumerate(){ram[at+4*i..at+4*i+4].copy_from_slice(&w.to_le_bytes());}}
fn fixture(sixteen:bool,tail:&[u32])->(m::Controls,Vec<u8>,Vec<u8>,usize,usize) {
    let step=if sixteen {0x4000}else{0x1000};let mut tables=vec![0;step*8];let mut ram=vec![0xa5;step*6];
    let(start,bits,page)=if sixteen {(1,11,14)}else{(0,9,12)};let mut leaf=0;
    for level in start..=3 {
        let n=level-start;let index=((RAM>>(page+bits*(3-level)))&((1<<bits)-1))as usize;let at=n*step+index*8;
        let value=if level==3 {leaf=at;RAM|0x403}else{TABLE+((n+1)*step)as u64|3};
        tables[at..at+8].copy_from_slice(&value.to_le_bytes());
    }
    tables[leaf+8..leaf+16].copy_from_slice(&(RAM+3*step as u64|0x403).to_le_bytes());
    tables[leaf+16..leaf+24].copy_from_slice(&(RAM+4*step as u64|0x403).to_le_bytes());
    tables[leaf+24..leaf+32].copy_from_slice(&(RAM+2*step as u64|0x403).to_le_bytes());
    let first=tables[..step].to_vec();tables[4*step..5*step].copy_from_slice(&first);
    words(&mut ram,step-12,&[0x91000442,0xd5181000,ISB]); // ADD x2; MSR SCTLR,x0; ISB
    words(&mut ram,3*step,tail);
    let tcr=if sixteen {17|(17<<16)|(2<<14)|(1<<30)|(5<<32)}else{16|(16<<16)|(2<<30)|(5<<32)};
    (m::Controls{abi_version:3,struct_size:80,profile:2,sctlr:0x30d00802,ttbr0:TABLE,ttbr1:TABLE,tcr,mair:0x44,epoch:1,..Default::default()},tables,ram,step,leaf)
}
struct Evidence {out:Run,data:Vec<m::Request>,controls:Vec<(c::Request,c::Reply)>,service_state:c::Reply,reads:u64}
fn run(c:m::Controls,tables:&[u8],ram:&mut[u8],entry:u64,initial:[u64;4],budget:u64,corrupt:u32)->Evidence {
    let size=ram.len()as u64;let code=Code::new();
    let mut o=Owner{service:MemoryServiceDynamic::new(ram,RAM,tables,TABLE,c).unwrap(),data:vec![],controls:vec![],corrupt};
    let mut out=Run::default();let status=unsafe{vf_boot_run_memory_dynamic(RAM,size,entry,initial[0],RAM+0x400,
        code.0,4096,budget,Some(protect),core::ptr::null_mut(),initial.as_ptr(),core::ptr::null(),&c,
        Some(memory),Some(control),(&mut o as *mut Owner<'_>).cast(),&mut out)};
    assert_eq!(status as u32,out.memory.base.execution.base.status);let state=o.service.final_state();let reads=o.service.table_reads();
    Evidence{out,data:o.data,controls:o.controls,service_state:state,reads}
}
#[test]fn generated_x86_crosses_guarded_isb_and_uses_nonidentity_scalar_pair_memory() {
    for sixteen in [false,true] {
        let(c,tables,mut ram,step,_)=fixture(sixteen,&[
            0xd5381000, // MRS x0,SCTLR
            0xf9400023,0x91000463,0xf9000023, // LDR/ADD/STR x3,[x1]
            0xa9000c22,0xa9400823, // STP x2,x3; LDP x3,x2 at discontiguous boundary
            DSB,TLBI,DSB,ISB,0xf9400020,HLT]);
        let address=RAM+3*step as u64-8;ram[5*step-8..5*step].copy_from_slice(&41u64.to_le_bytes());let before=ram.clone();
        let e=run(c,&tables,&mut ram,RAM+step as u64-12,[c.sctlr|1,address,8,0],64,0);
        let r=e.out;let b=r.memory.base.execution.base;
        assert_eq!((b.status,b.retired,r.memory.base.provider_status),(1,15,0),"{r:?}");
        assert_eq!((b.x0,b.x1,b.x2,b.x3),(9,address,42,9));assert!(b.compiled_blocks>=12);
        assert_eq!((r.memory.base.data_requests,r.memory.base.completed_data_operations),(5,5));
        assert_eq!((r.final_control.architectural.sctlr,r.final_control.effective.sctlr,r.final_control.revision,r.final_control.epoch,r.final_control.invalidations),(c.sctlr|1,c.sctlr|1,2,2,1));
        assert_eq!(r.final_control,e.service_state);assert!(e.reads>0);
        assert_eq!(&ram[5*step-8..5*step],&9u64.to_le_bytes());assert_eq!(&ram[2*step..2*step+8],&42u64.to_le_bytes());
        let mut expected=before;expected[5*step-8..5*step].copy_from_slice(&9u64.to_le_bytes());expected[2*step..2*step+8].copy_from_slice(&42u64.to_le_bytes());assert_eq!(ram,expected);
        assert_eq!(e.controls.len(),12);assert!(e.controls.chunks(2).all(|q|q[0].0.phase==c::PREPARE && q[1].0.phase==c::COMMIT));
        let pending=e.data.iter().find(|q|q.pc==RAM+step as u64-4).unwrap();assert_eq!(pending.controls.sctlr&1,0);
        let after=e.data.iter().find(|q|q.pc==RAM+step as u64).unwrap();assert_eq!((after.controls.sctlr&1,after.controls.epoch),(1,2));
        assert!(e.data.iter().any(|q|q.operation==STORE && q.count==2));assert!(e.data.iter().any(|q|q.operation==LOAD && q.count==2));
    }
}
#[test]fn actual_native_post_isb_fetch_and_data_faults_are_precise() {
    for sixteen in [false,true] {for fetch in [true,false] {
        let(c,mut tables,mut ram,step,leaf)=fixture(sixteen,&[0xf9400023,HLT]);
        let at=leaf+if fetch {8}else{16};tables[at..at+8].fill(0);let before=ram.clone();
        let e=run(c,&tables,&mut ram,RAM+step as u64-12,[c.sctlr|1,RAM+2*step as u64,8,99],64,0);
        let r=e.out;let b=r.memory.base.execution.base;assert_eq!((b.status,b.retired,r.memory.base.provider_status),(if fetch {16}else{17},3,0),"{r:?}");
        assert_eq!((r.memory.base.execution.esr,r.memory.base.guest_far),(if fetch {0x86000007}else{0x96000007},RAM+if fetch {step as u64}else{2*step as u64}));
        assert_eq!(r.memory.base.execution.elr,RAM+step as u64);assert_eq!(r.memory.last_reply.level,3);
        assert_eq!((b.x2,b.x3),(9,99));assert_eq!((r.final_control.epoch,r.final_control.state_tag),(2,c::KNOWN));assert_eq!(ram,before);
    }}
}
#[test]fn native_prepare_guards_reject_without_retiring_enable_or_changing_cpu_controls() {
    for sixteen in [false,true] {for variant in 0..6 {
        let(c,mut tables,mut ram,step,leaf)=fixture(sixteen,&[HLT]);
        match variant {0=>words(&mut ram,step-4,&[0xd503201f]),1=>tables[leaf..leaf+8].fill(0),
            2=>tables[leaf..leaf+8].copy_from_slice(&(RAM|0x403|1<<53).to_le_bytes()),
            3=>tables[leaf..leaf+8].copy_from_slice(&(RAM|0x407).to_le_bytes()),
            4=>tables[leaf..leaf+8].copy_from_slice(&(RAM+step as u64|0x403).to_le_bytes()),_=>tables.truncate(8)}
        let before=ram.clone();let e=run(c,&tables,&mut ram,RAM+step as u64-12,[c.sctlr|1,0,8,99],64,0);
        let r=e.out;let b=r.memory.base.execution.base;
        assert_eq!((b.status,b.retired,b.pc),(4,1,RAM+step as u64-8));assert_ne!(r.memory.base.provider_status,0);
        assert_eq!((r.final_control.architectural.sctlr,r.final_control.effective.sctlr,r.final_control.revision,r.final_control.epoch),(c.sctlr,c.sctlr,1,1));
        assert_eq!((r.memory.base.execution.esr,r.memory.base.guest_far,b.fault_instruction),(0,0,0));
        assert_eq!(e.controls.len(),1);assert_eq!(ram,before);
    }}
}
#[test]fn malformed_control_acknowledgement_never_retires_and_marks_uncertain_commit() {
    for corrupt in 1..=12 {
        let(c,tables,mut ram,step,_)=fixture(false,&[HLT]);let before=ram.clone();
        let e=run(c,&tables,&mut ram,RAM+step as u64-12,[c.sctlr|1,0,8,99],64,corrupt);
        let r=e.out;let b=r.memory.base.execution.base;
        assert_eq!((b.status,b.retired,b.pc),(4,1,RAM+step as u64-8),"corruption {corrupt}: {r:?}");
        assert_eq!(r.memory.base.provider_status,if corrupt==7 || corrupt==9 {4}else{3});
        assert_eq!((r.final_control.architectural.sctlr,r.final_control.effective.sctlr,r.final_control.revision,r.final_control.epoch),(c.sctlr,c.sctlr,1,1));
        assert_eq!(r.final_control.state_tag,if corrupt>=8 {c::UNCERTAIN}else{c::KNOWN});
        assert_eq!(e.service_state.architectural.sctlr,if corrupt>=8 {c.sctlr|1}else{c.sctlr});
        assert_eq!((r.memory.base.execution.esr,r.memory.base.guest_far,b.fault_instruction),(0,0,0));assert_eq!(ram,before);
    }
}
#[test]fn architectural_mrs_and_budget_stop_preserve_unsynchronized_control_distinction() {
    let(c,tables,mut ram,step,_)=fixture(false,&[HLT]);
    words(&mut ram,0,&[0xd5182002,0xd5382003,DSB,ISB,HLT]); // TTBR0 x2, MRS x3
    let new_table=TABLE+4*step as u64;
    for budget in [1,2,3,4,5] {
        let mut ram=ram.clone();let e=run(c,&tables,&mut ram,RAM,[c.sctlr|1,0,new_table,0],budget,0);
        let r=e.out;let b=r.memory.base.execution.base;assert_eq!((b.status,b.retired),(if budget==5 {1}else{5},budget));
        assert_eq!(r.final_control.architectural.ttbr0,new_table);
        assert_eq!(r.final_control.effective.ttbr0,if budget>=4 {new_table}else{TABLE});
        assert_eq!(r.final_control.epoch,if budget>=4 {2}else{1});
        if budget>=2 {assert_eq!(b.x3,new_table);}
        assert_eq!(r.final_control,e.service_state);assert_eq!(r.final_control.invalidations,0);
    }
}
#[test]fn unsupported_controls_and_pending_instructions_are_not_silently_executed() {
    for instruction in [0xd5181000,0xd5182000,0xd5033bbf,0xd508831f,0xd50342ff] {
        let(c,tables,mut ram,step,_)=fixture(false,&[instruction,HLT]);let before=ram.clone();
        let e=run(c,&tables,&mut ram,RAM+step as u64-12,[c.sctlr|1,0,8,99],64,0);
        // First use the supported value to reach M=1; the target write still
        // rejects even if it repeats the current value rather than clearing M.
        assert_eq!((e.out.memory.base.execution.base.status,e.out.memory.base.execution.base.retired),(if instruction==0xd5181000 || instruction==0xd5182000 {4}else{13},3));
        assert_eq!((e.out.final_control.epoch,e.out.final_control.revision),(2,2));assert_eq!(ram,before);
    }
    let(c,tables,mut ram,_,_)=fixture(false,&[HLT]);
    words(&mut ram,0,&[0xd5182002,0xd50342ff,HLT]);
    let e=run(c,&tables,&mut ram,RAM,[c.sctlr|1,0,TABLE+0x4000,99],64,0);
    assert_eq!((e.out.memory.base.execution.base.status,e.out.memory.base.execution.base.retired),(13,1));
    assert_eq!(e.out.memory.base.execution.pstate&0xc0,0xc0);assert_eq!(e.out.final_control.epoch,1);
}

#[test]fn actual_c_rust_dynamic_layout_matches_without_changing_cpu_or_v2() {
    use core::mem::{size_of,align_of,offset_of};
    let expected=[888,size_of::<m::Controls>(),size_of::<m::Request>(),size_of::<m::Reply>(),
        size_of::<c::Snapshot>(),size_of::<c::Request>(),size_of::<c::Reply>(),size_of::<Run>(),
        align_of::<c::Request>(),align_of::<c::Reply>(),align_of::<Run>(),
        offset_of!(c::Request,pc),offset_of!(c::Request,before),offset_of!(c::Request,candidate),
        offset_of!(c::Reply,token),offset_of!(c::Reply,architectural),offset_of!(c::Reply,effective),offset_of!(Run,final_control)];
    let mut actual=[0u64;18];assert_eq!(unsafe{vf_dynamic_layout(actual.as_mut_ptr(),actual.len())},18);
    assert_eq!(actual,expected.map(|v|v as u64));
}

#[test]fn actual_cpu_control_bank_changes_only_after_a_valid_commit_acknowledgement() {
    for corrupt in 1..=12 {
        let(c,tables,mut ram,step,_)=fixture(false,&[HLT]);let entry=RAM+step as u64-8;let code=Code::new();
        let mut owner=Owner{service:MemoryServiceDynamic::new(&mut ram,RAM,&tables,TABLE,c).unwrap(),data:vec![],controls:vec![],corrupt};
        let mut observed=[0;8];let mut out=Run::default();
        let status=unsafe{vf_dynamic_cpu_probe(code.0,4096,&c,Some(memory),Some(control),
            (&mut owner as *mut Owner<'_>).cast(),Some(protect),entry,c.sctlr|1,observed.as_mut_ptr(),&mut out)};
        assert_eq!(status,4);assert_eq!(observed,[c.sctlr,c.ttbr0,c.ttbr1,c.tcr,c.mair,entry,0,0],"corrupt {corrupt}");
    }
}
#[test]fn boot_constructor_rejects_bad_spans_profiles_and_overlap_before_callbacks() {
    let(c,tables,mut ram,step,_)=fixture(false,&[HLT]);let size=ram.len()as u64;let entry=RAM+step as u64-8;let code=Code::new();
    let mut owner=Owner{service:MemoryServiceDynamic::new(&mut ram,RAM,&tables,TABLE,c).unwrap(),data:vec![],controls:vec![],corrupt:0};
    let initial=[c.sctlr|1,0,0,0];
    for variant in 0..7 {
        let mut bad=c;let mut options=platform::BootOptionsV2{abi_version:2,struct_size:64,initial_pstate:0x3c5,..Default::default()};
        let(mut base,mut bytes)=(RAM,size);
        match variant {0=>bad.abi_version=2,1=>bad.sctlr|=1,2=>bad.epoch=2,3=>bytes=0,
            4=>base=u64::MAX-3,5=>options.initial_pstate=0x305,_=>options.initial_pstate=0x3c0}
        let mut out=Run::default();let status=unsafe{vf_boot_run_memory_dynamic(base,bytes,entry,initial[0],RAM+0x400,code.0,4096,16,
            Some(protect),core::ptr::null_mut(),initial.as_ptr(),&options,&bad,Some(memory),Some(control),
            (&mut owner as *mut Owner<'_>).cast(),&mut out)};
        assert_eq!(status,4);assert_eq!(out.memory.base.provider_status,2);assert!(owner.data.is_empty());assert!(owner.controls.is_empty());
    }
    // A result/code alias and result/input alias must not even zero the aliased bytes.
    unsafe{core::ptr::write_bytes(code.0,0xa5,4096)};
    let result=code.0.cast::<Run>();let status=unsafe{vf_boot_run_memory_dynamic(RAM,size,entry,initial[0],RAM+0x400,code.0,4096,16,
        Some(protect),core::ptr::null_mut(),initial.as_ptr(),core::ptr::null(),&c,Some(memory),Some(control),
        (&mut owner as *mut Owner<'_>).cast(),result)};
    assert_eq!(status,4);assert!(unsafe{core::slice::from_raw_parts(code.0,4096)}.iter().all(|b|*b==0xa5));
    let mut out=Run::default();let initial_alias=(&mut out as *mut Run).cast::<u64>();let before=out;
    let status=unsafe{vf_boot_run_memory_dynamic(RAM,size,entry,initial[0],RAM+0x400,code.0,4096,16,
        Some(protect),core::ptr::null_mut(),initial_alias,core::ptr::null(),&c,Some(memory),Some(control),
        (&mut owner as *mut Owner<'_>).cast(),&mut out)};
    assert_eq!(status,4);assert_eq!(out.memory.base.abi_version,before.memory.base.abi_version);
}

#[test]fn m0_callback_cannot_invent_translation_fault_or_out_of_profile_success() {
    for corrupt in 101..=104 {
        let(c,tables,mut ram,_,_)=fixture(false,&[HLT]);let before=ram.clone();
        let entry=if corrupt==101 {1u64<<48}else{RAM};
        let e=run(c,&tables,&mut ram,entry,[c.sctlr|1,0,0,0],16,corrupt);let r=e.out;
        assert_eq!((r.memory.base.execution.base.status,r.memory.base.execution.base.retired,r.memory.base.provider_status),(4,0,3));
        assert_eq!((r.memory.base.execution.esr,r.memory.base.guest_far),(0,0));assert_eq!(ram,before);
    }
}

#[path="ubfm_provider_cases.rs"]mod ubfm_cases;
#[test]fn ubfm_stays_native_before_and_after_one_way_enable_without_widening_guard() {
    for sixteen in [false,true] {for mapped in [false,true] {for case in ubfm_cases::cases() {
        let(c,tables,mut ram,step,_)=fixture(sixteen,&[case.word,HLT]);
        let entry=if mapped {RAM+step as u64-12}else{words(&mut ram,0,&[case.word,HLT]);RAM};let before=ram.clone();
        let mut expected=[c.sctlr|1,case.source,8,0xfedcba9876543210];let initial=expected;
        if mapped {expected[2]+=1;}
        if case.destination!=31 {expected[case.destination]=case.expected;}
        let e=run(c,&tables,&mut ram,entry,initial,8,0);let r=e.out;let b=r.memory.base.execution.base;
        let count=if mapped {5}else{2};assert_eq!((b.status,b.retired,b.compiled_blocks),(1,count,count));
        assert_eq!([b.x0,b.x1,b.x2,b.x3],expected);assert_eq!((r.memory.base.execution.sp,r.memory.base.execution.pstate),(RAM+0x400,0x3c5));
        assert_eq!((b.pc,r.memory.base.provider_status),(if mapped {RAM+step as u64+8}else{RAM+8},0));
        assert_eq!((r.memory.base.fetch_requests,r.memory.base.data_requests,r.memory.base.completed_data_operations),(count,0,0));
        assert_eq!((r.memory.base.execution.esr,r.memory.base.guest_far),(0,0));assert_eq!(ram,before);
        assert_eq!((r.final_control.architectural.sctlr&1,r.final_control.effective.sctlr&1),(u64::from(mapped),u64::from(mapped)));
        assert_eq!(r.final_control,e.service_state);assert!(e.data.iter().all(|q|q.operation==nextcore_memory_service::abi::FETCH));
    }}}
    for sixteen in [false,true] {
        for word in ubfm_cases::INVALID {
            let(c,tables,mut ram,step,_)=fixture(sixteen,&[word,HLT]);let before=ram.clone();
            let initial=[c.sctlr|1,2,8,4];let e=run(c,&tables,&mut ram,RAM+step as u64-12,initial,8,0);let r=e.out;let b=r.memory.base.execution.base;
            assert_eq!((b.status,b.retired,b.compiled_blocks,r.memory.base.fetch_requests),(8,3,4,4));
            assert_eq!([b.x0,b.x1,b.x2,b.x3],[initial[0],2,9,4]);assert_eq!((b.pc,r.memory.base.execution.esr,r.memory.base.data_requests),(RAM+step as u64,1<<25,0));assert_eq!(ram,before);
        }
        let(c,tables,mut ram,step,_)=fixture(sixteen,&[HLT]);words(&mut ram,step-4,&[0xd3401c23]);let before=ram.clone();
        let e=run(c,&tables,&mut ram,RAM+step as u64-12,[c.sctlr|1,9,8,7],8,0);let r=e.out;let b=r.memory.base.execution.base;
        assert_eq!((b.status,b.retired,r.memory.base.provider_status),(4,1,1));assert_eq!((r.final_control.architectural.sctlr&1,r.final_control.effective.sctlr&1),(0,0));
        assert_eq!(b.pc,RAM+step as u64-8);assert_eq!(ram,before);
    }
}

#[test]
fn extended_arithmetic_native_through_dynamic_provider() {
    for sixteen in [false, true] { for mapped in [false, true] {
        let (c, tables, mut ram, step, _) = fixture(sixteen, &[0x8b218023, 0xcb210c62, 0xeb21c05f, HLT]);
        let entry = if mapped {RAM + step as u64 - 12} else {words(&mut ram, 0, &[0x8b218023, 0xcb210c62, 0xeb21c05f, HLT]); RAM};
        let before = ram.clone(); let e = run(c, &tables, &mut ram, entry, [c.sctlr | 1, 0xff, 8, 0], 16, 0);
        let r = e.out; let b = r.memory.base.execution.base; let count = if mapped {7} else {4};
        assert_eq!((b.status, b.retired, b.compiled_blocks, b.x2, b.x3), (1, count, count, u64::MAX - 1785, 254));
        assert_eq!((r.memory.base.execution.pstate >> 28, r.memory.base.fetch_requests, r.memory.base.data_requests), (10, count, 0));
        assert!(e.data.iter().all(|q| q.operation == nextcore_memory_service::abi::FETCH)); assert_eq!(ram, before);
        assert_eq!(r.final_control, e.service_state);
    }}
}

#[test]
fn conditional_selection_native_through_dynamic_provider() {
    for sixteen in [false, true] { for mapped in [false, true] {
        let (c, tables, mut ram, step, _) = fixture(sixteen, &[0xf100143f, 0x9a9f0022, 0x9a8217e2, 0x5a8113e0, 0xda8217e3, HLT]);
        let entry = if mapped {RAM + step as u64 - 12} else {words(&mut ram, 0, &[0xf100143f, 0x9a9f0022, 0x9a8217e2, 0x5a8113e0, 0xda8217e3, HLT]); RAM};
        let before = ram.clone(); let e = run(c, &tables, &mut ram, entry, [c.sctlr | 1, 5, 8, 0], 16, 0);
        let r = e.out; let b = r.memory.base.execution.base; let count = if mapped {9} else {6};
        assert_eq!((b.status, b.retired, b.compiled_blocks), (1, count, count));
        assert_eq!((b.x0, b.x1, b.x2, b.x3), (0xfffffffa, 5, 6, u64::MAX - 5));
        assert_eq!((r.memory.base.execution.pstate, r.memory.base.fetch_requests, r.memory.base.data_requests), (0x600003c5, count, 0));
        assert!(e.data.iter().all(|q| q.operation == nextcore_memory_service::abi::FETCH)); assert_eq!(ram, before);
        assert_eq!(r.final_control, e.service_state);
    }}
}

#[path="register_offset_provider_cases.rs"]mod register_offset_cases;
#[test]fn register_offset_dynamic_all_forms_before_and_after_enable() {
    for sixteen in [false,true] {for mapped in [false,true] {for case in register_offset_cases::cases() {
        let(c,tables,mut ram,step,_)=fixture(sixteen,&[case.word,HLT]);
        let entry=if mapped {RAM+step as u64-12}else{words(&mut ram,0,&[case.word,HLT]);RAM};
        let address=RAM+2*step as u64;let at=if mapped {4*step}else{2*step};
        ram[at..at+8].copy_from_slice(&0x80ff7f0102030480u64.to_le_bytes());
        let initial=[c.sctlr|1,address.wrapping_sub(case.offset),case.index.wrapping_sub(u64::from(mapped)),0x1234567887654321];
        let mut regs=initial;regs[2]=case.index;let mut expected=ram.clone();register_offset_cases::expected(&case,&mut regs,&mut expected,at);
        let e=run(c,&tables,&mut ram,entry,initial,8,0);let r=e.out;let b=r.memory.base.execution.base;let count=if mapped {5}else{2};
        assert_eq!((b.status,b.retired,b.compiled_blocks),(1,count,count));assert_eq!([b.x0,b.x1,b.x2,b.x3],regs);
        assert_eq!((r.memory.base.execution.pstate,r.memory.base.execution.sp,r.memory.base.data_requests,r.memory.base.completed_data_operations),(0x3c5,RAM+0x400,1,1));
        let q=e.data.iter().find(|q|q.operation!=nextcore_memory_service::abi::FETCH).unwrap();assert_eq!((q.address,q.width,q.count),(address,case.bytes as u32,1));assert_eq!(ram,expected);assert_eq!(r.final_control,e.service_state);
    }}}
    for sixteen in [false,true] {for read in [false,true] {for permission in [false,true] {
        let word=register_offset_cases::word(1,u32::from(read),6,1,1,2,2);
        let(c,mut tables,mut ram,step,leaf)=fixture(sixteen,&[word,HLT]);let address=RAM+2*step as u64;
        if permission {tables[leaf+16..leaf+24].copy_from_slice(&(RAM+4*step as u64|if read {3}else{0x483}).to_le_bytes());}
        else {tables[leaf+16..leaf+24].fill(0);}
        let before=ram.clone();let initial=[c.sctlr|1,address+2,u64::MAX-1,99];
        let e=run(c,&tables,&mut ram,RAM+step as u64-12,initial,8,0);let r=e.out;let b=r.memory.base.execution.base;
        assert_eq!((b.status,b.retired,r.memory.base.data_requests,r.memory.base.completed_data_operations),(17,3,1,0));
        assert_eq!([b.x0,b.x1,b.x2,b.x3],[initial[0],initial[1],u64::MAX,99]);assert_eq!(ram,before);
        let esr=0x96000000|if read {0}else{64}|if permission {if read {11}else{15}}else{7};
        assert_eq!((r.memory.base.execution.esr,r.memory.base.guest_far),(esr,address));assert_eq!(r.final_control,e.service_state);
    }}}
}
