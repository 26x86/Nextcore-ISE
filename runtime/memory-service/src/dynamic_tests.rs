use super::*;
use crate::abi::LOAD;
use std::{vec,vec::Vec};
const RAM:u64=0x40000000;const TABLE:u64=0x10000000;
fn fixture(sixteen:bool)->(m::Controls,Vec<u8>,Vec<u8>,usize,usize) {
    let step=if sixteen {0x4000}else{0x1000};let mut tables=vec![0;step*4];let mut ram=vec![0xa5;step*6];
    let(start,bits,page)=if sixteen {(1,11,14)}else{(0,9,12)};let mut leaf=0;
    for level in start..=3 {
        let n=level-start;let index=((RAM>>(page+bits*(3-level)))&((1<<bits)-1))as usize;let at=n*step+index*8;
        let value=if level==3 {leaf=at;RAM|0x403}else{TABLE+((n+1)*step)as u64|3};
        tables[at..at+8].copy_from_slice(&value.to_le_bytes());
    }
    tables[leaf+8..leaf+16].copy_from_slice(&(RAM+3*step as u64|0x403).to_le_bytes());
    tables[leaf+16..leaf+24].copy_from_slice(&(RAM+4*step as u64|0x403).to_le_bytes());
    tables[leaf+24..leaf+32].copy_from_slice(&(RAM+2*step as u64|0x403).to_le_bytes());
    ram[step-4..step].copy_from_slice(&ISB_WORD.to_le_bytes());
    ram[3*step..3*step+4].copy_from_slice(&0xd4400000u32.to_le_bytes());
    let tcr=if sixteen {17|(17<<16)|(2<<14)|(1<<30)|(5<<32)}else{16|(16<<16)|(2<<30)|(5<<32)};
    (m::Controls{abi_version:3,struct_size:80,profile:2,sctlr:0x30d00802,ttbr0:TABLE,ttbr1:TABLE,tcr,mair:0x44,epoch:1,..Default::default()},tables,ram,step,leaf)
}
fn request(s:&MemoryServiceDynamic<'_>,pc:u64,operation:u32,selector:u32,operand:u64)->c::Request {
    let mut candidate=s.architectural;
    if operation==c::WRITE {match selector {c::SCTLR=>candidate.sctlr=operand,c::TTBR0=>candidate.ttbr0=operand,
        c::TTBR1=>candidate.ttbr1=operand,c::TCR=>candidate.tcr=operand,c::MAIR=>candidate.mair=operand,_=>{}}}
    c::Request{abi_version:1,struct_size:192,phase:c::PREPARE,operation,selector,current_el:1,pc,operand,
        revision:s.revision,epoch:s.epoch,before:s.architectural,candidate,..Default::default()}
}
fn commit(s:&mut MemoryServiceDynamic<'_>,q:c::Request)->c::Reply {
    let before=s.final_state();let p=s.control(&q);assert_eq!(p.result,c::OK,"{p:?}");assert_eq!(p.state_tag,c::PROPOSED);
    assert_eq!(s.final_state(),before);let mut c=q;c.phase=c::COMMIT;c.operand=p.token;
    let out=s.control(&c);assert_eq!(out.result,c::OK);assert_eq!(out.state_tag,c::KNOWN);assert_eq!(out.token,0);
    assert_eq!((out.architectural,out.effective,out.revision,out.epoch),(p.architectural,p.effective,p.revision,p.epoch));out
}
fn data(s:&MemoryServiceDynamic<'_>,operation:u32,address:u64,width:u32,count:u32)->m::Request {
    m::Request{abi_version:3,struct_size:160,operation,width,count,current_el:1,pc:address,address,pstate:0x3c5,
        controls:s.effective_controls(),..Default::default()}
}
#[test]fn enable_splits_architectural_effective_state_then_reuses_real_tlb() {
    for sixteen in [false,true] {
        let(c,tables,mut ram,step,_)=fixture(sixteen);let pc=RAM+step as u64-8;
        let mut s=MemoryServiceDynamic::new(&mut ram,RAM,&tables,TABLE,c).unwrap();
        let q=request(&s,pc,c::WRITE,c::SCTLR,c.sctlr|1);let out=commit(&mut s,q);
        assert_eq!((out.architectural.sctlr&1,out.effective.sctlr&1,out.revision,out.epoch),(1,0,2,1));
        let q=data(&s,LOAD,RAM+step as u64,8,1);assert_eq!(s.execute(&q).result,m::UNSUPPORTED);
        let q=data(&s,FETCH,pc+4,4,1);assert_eq!(s.execute(&q).value0,u64::from(ISB_WORD));
        let q=request(&s,pc+4,c::ISB,0,0);let out=commit(&mut s,q);assert_eq!((out.effective.sctlr&1,out.epoch),(1,2));
        let q=data(&s,FETCH,pc+8,4,1);assert_eq!(s.execute(&q).value0,0xd4400000);let reads=s.table_reads();assert!(reads>0);
        assert_eq!(s.execute(&q).value0,0xd4400000);assert_eq!(s.table_reads(),reads);
        for operation in [c::DSB,c::ISB] {
            let control=request(&s,pc+8,operation,0,0);commit(&mut s,control);
            assert_eq!(s.execute(&q).value0,0xd4400000);assert_eq!(s.table_reads(),reads);
        }
        let control=request(&s,pc+8,c::TLBI,0,0);let out=commit(&mut s,control);assert_eq!(out.invalidations,1);
        assert_eq!(s.execute(&q).value0,0xd4400000);assert!(s.table_reads()>reads);
        let address=RAM+3*step as u64-8;let mut store=data(&s,STORE,address,8,2);store.value0=0x123456789abcdef0;store.value1=0x8123456789abcdef;
        assert_eq!(s.execute(&store).result,m::OK);let load=data(&s,LOAD,address,8,2);let out=s.execute(&load);
        assert_eq!((out.value0,out.value1),(store.value0,store.value1));
        assert_eq!(&ram[2*step..2*step+8],&store.value1.to_le_bytes());
    }
}
#[test]fn guarded_enable_failures_leave_all_state_and_ram_unchanged() {
    for sixteen in [false,true] {for variant in 0..8 {
        let(c,mut tables,mut ram,step,leaf)=fixture(sixteen);let mut pc=RAM+step as u64-8;
        match variant {0=>ram[step-4..step].fill(0),1=>tables[leaf..leaf+8].fill(0),
            2=>tables[leaf..leaf+8].copy_from_slice(&(RAM|0x403|(1<<53)).to_le_bytes()),
            3=>tables[leaf..leaf+8].copy_from_slice(&(RAM|0x407).to_le_bytes()),
            4=>tables[leaf..leaf+8].copy_from_slice(&(RAM+step as u64|0x403).to_le_bytes()),
            5=>tables.truncate(8),6=>pc=u64::MAX-3,_=>ram.truncate(step-1)}
        let before=ram.clone();let mut s=MemoryServiceDynamic::new(&mut ram,RAM,&tables,TABLE,c).unwrap();let state=s.final_state();
        let q=request(&s,pc,c::WRITE,c::SCTLR,c.sctlr|1);let out=s.control(&q);
        assert_ne!(out.result,c::OK,"variant {variant}");assert_eq!(out.state_tag,c::EMPTY);assert_eq!(out.token,0);
        assert_eq!(s.final_state(),state);assert!(s.prepared.is_none());assert_eq!(ram,before);
    }}
}
#[test]fn cancel_replay_stale_and_foreign_tokens_cannot_commit() {
    let(c,tables,mut ram,_,_)=fixture(false);let mut other_ram=ram.clone();
    let mut a=MemoryServiceDynamic::new(&mut ram,RAM,&tables,TABLE,c).unwrap();
    let mut b=MemoryServiceDynamic::new(&mut other_ram,RAM,&tables,TABLE,c).unwrap();
    let q=request(&a,RAM,c::WRITE,c::TTBR0,c.ttbr0);let pa=a.control(&q);let pb=b.control(&q);assert_ne!(pa.token,pb.token);
    let state=a.final_state();let mut bad=q;bad.phase=c::COMMIT;bad.operand=pb.token;assert_eq!(a.control(&bad).detail,c::TOKEN);
    bad.operand=pa.token;bad.pc+=4;assert_eq!(a.control(&bad).detail,c::TOKEN);
    bad=q;bad.phase=c::COMMIT;bad.operand=pa.token;bad.epoch+=1;assert_eq!(a.control(&bad).detail,c::STALE);
    let mut cancel=q;cancel.phase=c::CANCEL;cancel.operand=pa.token;assert_eq!(a.control(&cancel).result,c::OK);
    assert_eq!(a.final_state(),state);assert_eq!(a.control(&cancel).detail,c::TOKEN);
    let out=commit(&mut a,q);assert_eq!(out.revision,2);let mut replay=q;replay.phase=c::COMMIT;replay.operand=pa.token;
    assert_ne!(a.control(&replay).result,c::OK);assert_eq!(a.revision,2);
}
#[test]fn m0_device_alignment_and_effective_snapshot_are_preserved() {
    let(c,tables,mut ram,step,_)=fixture(false);let before=ram.clone();
    let mut s=MemoryServiceDynamic::new(&mut ram,RAM,&tables,TABLE,c).unwrap();
    let q=data(&s,STORE,RAM+1,8,1);let out=s.execute(&q);assert_eq!((out.result,out.esr),(m::GUEST_FAULT,0x96000061));
    let mut q=data(&s,LOAD,RAM,8,1);q.controls.epoch+=1;assert_eq!(s.execute(&q).result,m::INVALID_REQUEST);
    let q=data(&s,LOAD,1<<48,8,1);assert_eq!(s.execute(&q).result,m::UNSUPPORTED);
    let q=request(&s,RAM,c::WRITE,c::TTBR0,c.ttbr0+0x1000);commit(&mut s,q);
    let q=request(&s,RAM+step as u64-8,c::WRITE,c::SCTLR,c.sctlr|1);assert_eq!(s.control(&q).detail,c::PENDING);
    let mut q=data(&s,FETCH,RAM,4,1);q.pstate&=!0x80;assert_eq!(s.execute(&q).result,m::UNSUPPORTED);
    assert_eq!(ram,before);
}
