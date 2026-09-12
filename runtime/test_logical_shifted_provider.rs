// Appended to canonical stage1 fixture in scratch; real generated native execution.
#[test]fn logical_shifted_canonical_all_forms_no_data_and_cache_equivalence(){
 let output=std::env::var("NEXTCORE_LOGICAL_SNAPSHOT").unwrap();let mut snapshots=Vec::new();
 for &(word,a,b,value,nzcv) in LOGICAL_ORACLE {
  let(mut c,t,mut ram,_,_)=fixture(true,&[word,0x17ffffff]);c.profile=3;c.sctlr=0x30d00801;let before=ram.clone();
  let options=platform::BootOptionsV2{abi_version:2,struct_size:64,initial_pstate:0xf00003c5,..Default::default()};
  let(r,q)=run_with_options(c,&t,&mut ram,VA,[a,b,0xface,33],0,Some(&options));let x=r.base.execution.base;
  assert_eq!((x.status,x.retired,x.pc,x.x0,x.x1,x.x2,x.x3),(5,32,VA,a,b,value,33));
  assert_eq!((r.base.fetch_requests,r.base.data_requests,r.base.completed_data_operations,q.len()),(32,0,0,32));
  assert_eq!((r.base.execution.pstate,r.base.execution.sp),(nzcv|0x3c5,VA+0x400));
  for(i,request)in q.iter().enumerate(){assert_eq!((request.operation,request.width,request.count,request.address),(FETCH,4,1,VA+4*(i%2)as u64));}
  assert_eq!(ram,before);
  snapshots.extend_from_slice(unsafe{core::slice::from_raw_parts((&r as *const Run).cast::<u8>(),core::mem::size_of::<Run>())});
  for request in q {snapshots.extend_from_slice(unsafe{core::slice::from_raw_parts((&request as *const Request).cast::<u8>(),core::mem::size_of::<Request>())});}
 }
 for amount in 32..64 {let word=0x0a000000|amount<<10;let(c,t,mut ram,_,_)=fixture(true,&[word]);let before=ram.clone();let(r,q)=run(c,&t,&mut ram,VA,[11,22,33,44],0);let b=r.base.execution.base;assert_eq!((b.status,b.retired,b.pc,b.x0,b.x1,b.x2,b.x3,q.len(),r.base.data_requests),(8,0,VA,11,22,33,44,1,0));assert_eq!(ram,before);}
 std::fs::write(output,snapshots).unwrap();
}
