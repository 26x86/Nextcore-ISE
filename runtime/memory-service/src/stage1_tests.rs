use super::*;
use std::vec;
fn fixture(sixteen:bool)->(Controls,std::vec::Vec<u8>,std::vec::Vec<u8>,u64,usize,usize) {
    let step=if sixteen {0x4000} else {0x1000};let base=0x10000000u64;
    let mut tables=vec![0;step*4];let ram=vec![0xa5;step*4];let va=0x10000u64;
    let start=if sixteen {1} else {0};let bits=if sixteen {11} else {9};let page=if sixteen {14} else {12};
    let mut leaf=0;
    for level in start..=3 {
        let n=level-start;let index=((va>>(page+bits*(3-level)))&((1<<bits)-1)) as usize;
        let at=n*step+index*8;
        let descriptor=if level==3 {leaf=at;0x40000403u64} else {base+((n+1)*step) as u64|3};
        tables[at..at+8].copy_from_slice(&descriptor.to_le_bytes());
    }
    tables[leaf+8..leaf+16].copy_from_slice(&(0x40000403u64+(3*step) as u64).to_le_bytes());
    let tcr=if sixteen {17|(17<<16)|(2<<14)|(1<<30)|(5<<32)} else {16|(16<<16)|(2<<30)|(5<<32)};
    let c=Controls{abi_version:2,struct_size:80,profile:1,sctlr:0x30d00803,ttbr0:base,ttbr1:base,tcr,mair:0x44,epoch:1,..Controls::default()};
    (c,tables,ram,va,step,leaf)
}
fn req(c:Controls,operation:u32,width:u32,count:u32,address:u64)->Request {
    Request{abi_version:2,struct_size:160,operation,width,count,current_el:1,pc:address,address,pstate:5,controls:c,..Request::default()}
}
#[test]fn constructor_rejects_invalid_physical_ranges_without_effects() {
    let(c,tables,mut ram,_,_,_)=fixture(false);let before=ram.clone();
    for (base,table_base) in [(u64::MAX-7,0x10000000),(0x40000000,u64::MAX-7),
        (0x10000000,0x10000000),(0x40000000,0x10000001)] {
        assert!(matches!(MemoryServiceV2::new(&mut ram,base,&tables,table_base,c),Err(Error::InvalidRange)));
        assert_eq!(ram,before);
    }
    assert!(matches!(MemoryServiceV2::new(&mut ram[..0],0x40000000,&tables,0x10000000,c),Err(Error::InvalidRange)));
    assert!(matches!(MemoryServiceV2::new(&mut ram,0x40000000,&tables[..7],0x10000000,c),Err(Error::InvalidRange)));
    let mut bad=c;bad.sctlr&=!1;
    assert!(matches!(MemoryServiceV2::new(&mut ram,0x40000000,&tables,0x10000000,bad),Err(Error::InvalidControls)));
    assert_eq!(ram,before);
}
#[test]fn nonidentity_scalar_and_discontiguous_pair_transfers_both_granules() {
    for sixteen in [false,true] {for width in [1u32,2,4,8] {for count in [1u32,2] {
        if count==2 && width<4 {continue;}
        let (c,tables,mut ram,va,step,_)=fixture(sixteen);
        let address=if count==2 {va+step as u64-u64::from(width)} else {va+8};
        let mut service=MemoryServiceV2::new(&mut ram,0x40000000,&tables,0x10000000,c).unwrap();
        let mut store=req(c,STORE,width,count,address);let mask=if width==8 {u64::MAX} else {(1u64<<(width*8))-1};
        store.value0=0xfedcba9876543280&mask;if count==2 {store.value1=0x8123456789abcdef&mask;}
        assert_eq!(service.execute(&store).result,OK);
        let load=service.execute(&req(c,LOAD,width,count,address));
        assert_eq!((load.result,load.value0,load.value1),(OK,store.value0,store.value1));
        assert_eq!(load,(Reply{value0:store.value0,value1:store.value1,..service.empty(OK)}));
        if count==2 {assert_eq!(ram[3*step],store.value1 as u8);assert_eq!(ram[step],0xa5);}
    }}}
}
#[test]fn pair_second_page_failure_never_commits_a_first_transfer() {
    for sixteen in [false,true] {for fault in 0..5 {for op in [LOAD,STORE] {
        let (c,mut tables,mut ram,va,step,leaf)=fixture(sixteen);let before=ram.clone();
        let second=match fault {0=>0,1=>0x40000003u64+(3*step) as u64,
            2=>0x40000483u64+(3*step) as u64,3=>0x50000403,_=>0x40000407};
        tables[leaf+8..leaf+16].copy_from_slice(&second.to_le_bytes());
        let mut r=req(c,op,8,2,va+step as u64-8);if op==STORE {r.value0=1;r.value1=2;}
        let out=MemoryServiceV2::new(&mut ram,0x40000000,&tables,0x10000000,c).unwrap().execute(&r);
        if fault==2 && op==LOAD {assert_eq!(out.result,OK);continue;}
        assert_ne!(out.result,OK);assert_eq!(out.address,va+step as u64);
        assert_eq!((out.value0,out.value1),(0,0));assert_eq!(ram,before);
        if fault>=3 {assert_eq!((out.fault,out.esr,out.fsc),(0,0,0));}
    }}}
}
#[test]fn controls_requests_and_missing_tables_have_fresh_diagnostics() {
    let (c,tables,mut ram,va,_,_)=fixture(false);
    for bit in [2u64,12,19,24,25,31] {let mut bad=c;bad.sctlr^=1<<bit;assert!(!controls_valid(&bad));}
    for bit in [8u64,12,36,37,39,40,41,42,59] {let mut bad=c;bad.tcr|=1<<bit;assert!(!controls_valid(&bad));}
    // A1 selects the immutable eight-bit ASID; changing it after construction is still rejected.
    let mut a1=c;a1.tcr|=1<<22;assert!(controls_valid(&a1));
    for upper in [false,true] {
        let mut tagged=c;if upper {tagged.ttbr1|=1<<48;} else {tagged.ttbr0|=1<<48;}
        assert!(controls_valid(&tagged));
        if upper {tagged.ttbr1|=1<<56;} else {tagged.ttbr0|=1<<56;}
        assert!(!controls_valid(&tagged));
    }
    let mut service=MemoryServiceV2::new(&mut ram,0x40000000,&tables[..8],0x10000000,c).unwrap();
    let out=service.execute(&req(c,LOAD,8,1,va));
    assert_eq!((out.result,out.fault,out.esr,out.fsc,out.value0,out.value1),(UNAVAILABLE,0,0,0,0,0));
    assert_eq!((out.context,out.level,out.metadata_flags,out.descriptor_pa),(WALK,1,HAS_DESCRIPTOR,0x10001000));
    for change in 0..6 {
        let mut r=req(c,STORE,8,1,va);match change {0=>r.controls.epoch=2,1=>r.pstate|=1<<22,
            2=>r.value1=1,3=>r.current_el=0,4=>r.flags=1,_=>r.controls.tcr^=1<<22};
        assert_eq!(service.execute(&r),service.empty(INVALID_REQUEST));
    }
    let r=req(c,LOAD,8,1,va);let mut reply=Reply{esr:u64::MAX,descriptor_pa:u64::MAX,..Reply::default()};
    assert_eq!(unsafe{vf_memory_service_step_v2((&mut service as *mut MemoryServiceV2).cast(),&r,&mut reply)},0);
    assert_eq!(reply,out);
}
#[test]fn fetch_uses_execute_permissions_and_upper_va_root() {
    for sixteen in [false,true] {
        let (c,mut tables,mut ram,va,_,leaf)=fixture(sixteen);
        let high=if sixteen {0xffff800000000000} else {0xffff000000000000};
        let mut service=MemoryServiceV2::new(&mut ram,0x40000000,&tables,0x10000000,c).unwrap();
        let out=service.execute(&req(c,FETCH,4,1,high|va));assert_eq!((out.result,out.value0),(OK,0xa5a5a5a5));
        drop(service);
        let leafword=u64::from_le_bytes(tables[leaf..leaf+8].try_into().unwrap())|(1<<53);
        tables[leaf..leaf+8].copy_from_slice(&leafword.to_le_bytes());
        let out=MemoryServiceV2::new(&mut ram,0x40000000,&tables,0x10000000,c).unwrap().execute(&req(c,FETCH,4,1,va));
        assert_eq!((out.result,out.fault,out.level,out.fsc,out.esr),(GUEST_FAULT,PERMISSION,3,15,0x8600000f));
    }
}
