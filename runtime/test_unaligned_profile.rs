//! Independent authored profile-3 cases through the real C JIT and Rust service.
use super::*;

fn unaligned_fixture(sixteen:bool,words:&[u32])->(Controls,Vec<u8>,Vec<u8>,usize,usize) {
    let(mut c,tables,ram,step,leaf)=fixture(sixteen,words);
    c.profile=3;c.sctlr=0x30d00801;(c,tables,ram,step,leaf)
}
fn backing(va:u64,step:usize)->usize {
    let offset=(va-VA)as usize;
    if offset<2*step {3*step+offset-step}else{step+offset-2*step}
}
#[test]fn scalar_and_pair_unaligned_transfers_preflight_noncontiguous_pages() {
    for sixteen in [false,true] {for read in [false,true] {for paired in [false,true] {
        let word=if paired {pair(true,read,2,0,1,0,2)}else{scalar(3,u32::from(read),1,0)};
        let(c,tables,seed,step,_)=unaligned_fixture(sixteen,&[word,HLT]);
        for offset in [1,3,7,step-7,step-3,step-1] {
            let address=VA+step as u64+offset as u64;
            let mut ram=seed.clone();
            for i in 0..16 {ram[backing(address+i,step)]=(i*13+7)as u8;}
            let mut expected=ram.clone();let values=[0x1122334455667788u64,0x99aabbccddeeff00];
            let count=if paired {2}else{1};let mut loaded=[0u64;2];
            for element in 0..count {for byte in 0..8 {
                let at=backing(address+(element*8+byte)as u64,step);
                loaded[element]|=u64::from(ram[at])<<(byte*8);
                if !read {expected[at]=(values[element]>>(byte*8))as u8;}
            }}
            let(r,requests)=run(c,&tables,&mut ram,VA,[values[0],address,values[1],4],0);
            let b=r.base.execution.base;
            assert_eq!((b.status,b.retired,r.base.provider_status,r.base.completed_data_operations),(1,2,0,1));
            assert_eq!(b.x1,address);assert_eq!(b.x0,if read {loaded[0]}else{values[0]});
            assert_eq!(b.x2,if read&&paired {loaded[1]}else{values[1]});
            assert_eq!(requests.len(),3);assert_eq!(requests[1].width,8);
            assert_eq!(requests[1].count,count as u32);assert_eq!(requests[1].address,address);
            assert_eq!(ram,expected);
        }
    }}}
}
#[test]fn second_page_fault_is_precise_and_never_partially_commits() {
    for sixteen in [false,true] {for read in [false,true] {for paired in [false,true] {
        let word=if paired {pair(true,read,2,0,1,0,2)}else{scalar(3,u32::from(read),1,0)};
        for failure in 0..3 {
            if read && failure==2 {continue;}
            let(c,mut tables,mut ram,step,leaf)=unaligned_fixture(sixteen,&[word,HLT]);
            let entry=leaf+16;
            let mut descriptor=u64::from_le_bytes(tables[entry..entry+8].try_into().unwrap());
            descriptor=match failure {0=>0,1=>descriptor&!0x400,_=>descriptor|0x80};
            tables[entry..entry+8].copy_from_slice(&descriptor.to_le_bytes());
            let address=VA+2*step as u64-3;let before=ram.clone();
            let(r,requests)=run(c,&tables,&mut ram,VA,[0x12345678,address,0xabcdef,4],0);
            let b=r.base.execution.base;let fsc=match failure {0=>7,1=>11,_=>15};
            assert_eq!((b.status,b.retired,r.base.provider_status,r.base.completed_data_operations),(17,0,0,0));
            assert_eq!((b.pc,b.x0,b.x1,b.x2),(VA,0x12345678,address,0xabcdef));
            assert_eq!(r.last_reply.address,VA+2*step as u64);
            assert_eq!(r.base.guest_far,VA+2*step as u64);
            assert_eq!(r.base.execution.esr,0x96000000|fsc|if read {0}else{64});
            assert_eq!(requests.len(),2);assert_eq!(ram,before);
        }
    }}}
}
unsafe extern "C" fn forged_alignment(owner:*mut c_void,q:*const Request,out:*mut Reply)->i32 {
    let status=unsafe{callback(owner,q,out)};
    let request=unsafe{&*q};
    if status==0 && request.operation==LOAD {
        unsafe{*out=Reply{abi_version:2,struct_size:128,result:GUEST_FAULT,fault:DATA_ALIGNMENT,
            level:NO_LEVEL,context:INPUT,fsc:0x21,address:request.address,esr:0x96000021,
            epoch:request.controls.epoch,..Reply::default()};}
    }status
}
#[test]fn c_rejects_fabricated_alignment_for_profile3_without_committing_state() {
    let(c,tables,mut ram,step,_)=unaligned_fixture(false,&[scalar(3,1,1,0),HLT]);
    let before=ram.clone();let code=Code::new();let size=ram.len()as u64;
    let mut owner=Owner{service:MemoryServiceV2::new(&mut ram,RAM,&tables,TABLES,c).unwrap(),requests:Vec::new(),corruption:0};
    let initial=[42,VA+step as u64+3,2,3];let mut result=Run::default();
    let status=unsafe{vf_boot_run_memory_v2(RAM,size,VA,42,VA+0x400,code.0,4096,8,
        Some(protect),core::ptr::null_mut(),initial.as_ptr(),core::ptr::null(),&c,
        Some(forged_alignment),(&mut owner as *mut Owner<'_>).cast(),&mut result)};
    assert_eq!((status,result.base.provider_status,result.base.execution.base.retired),(4,3,0));
    assert_eq!((result.base.execution.base.x0,result.base.execution.esr,result.base.guest_far),(42,0,0));
    assert_eq!(owner.requests.len(),2);drop(owner);assert_eq!(ram,before);
}
#[test]fn profile3_keeps_pc_and_optional_sp_alignment_checks() {
    for sixteen in [false,true] {
        let(c,tables,mut ram,_,_)=unaligned_fixture(sixteen,&[HLT]);let before=ram.clone();
        let(r,requests)=run(c,&tables,&mut ram,VA+1,[1,2,3,4],0);
        assert_eq!((r.base.execution.base.status,r.base.execution.base.retired,r.base.execution.esr),(16,0,0x8a000000));
        assert_eq!(requests.len(),1);assert_eq!(ram,before);
        for el in [0,1] {for read in [false,true] {
            let(mut c,tables,mut ram,_,_)=unaligned_fixture(sixteen,&[scalar(3,u32::from(read),31,0),HLT]);
            c.sctlr|=if el==0 {16}else{8};let before=ram.clone();
            let options=platform::BootOptionsV2{abi_version:2,struct_size:64,
                initial_pstate:0x3c0|if el==0 {0}else{5},..Default::default()};
            let(r,requests)=run_with_stack(c,&tables,&mut ram,VA,VA+0x408,[1,2,3,4],0,Some(&options));
            assert_eq!((r.base.execution.base.status,r.base.execution.base.retired,r.base.data_requests),(19,0,0));
            assert_eq!(r.base.execution.esr,0x9a000000);assert_eq!(requests.len(),1);assert_eq!(ram,before);
        }}
    }
}
#[test]fn control_contract_and_wrap_preserve_strict_profile() {
    for sixteen in [false,true] {for profile in [1,3] {for sa in [0,8,16,24] {for alignment in [0,2] {
        let(mut c,tables,mut ram,_,_)=fixture(sixteen,&[scalar(3,1,1,0),HLT]);
        c.profile=profile;c.sctlr=0x30d00801|sa|alignment;
        let valid=(profile==1)==(alignment==2);
        assert_eq!(unsafe{vf_stage1_controls_probe(&c)}!=0,valid);
        assert_eq!(nextcore_memory_service::stage1::controls_valid(&c),valid);
        if valid {
            let before=ram.clone();let(r,_)=run(c,&tables,&mut ram,VA,[42,u64::MAX-3,2,3],0);
            assert_eq!(r.base.execution.base.retired,0);assert_eq!(ram,before);
            if profile==1 {assert_eq!((r.base.execution.base.status,r.base.execution.esr),(12,0x96000021));}
            else {assert_eq!((r.base.execution.base.status,r.base.provider_status),(4,1));}
        }
    }}}}
}
