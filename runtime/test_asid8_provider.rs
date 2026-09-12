// Appended to the canonical provider harness by the independent runner.
unsafe extern "C" {fn test_asid8_dynamic(c:*const Controls)->i32;}
fn asid8_request(c:Controls,address:u64,op:u32)->Request {
 Request{abi_version:2,struct_size:core::mem::size_of::<Request>()as u32,operation:op,width:if op==FETCH{4}else{8},count:1,current_el:1,pstate:0x3c5,pc:address,address,controls:c,..Request::default()}
}
#[test]fn asid8_actual_selected_tag_cold_warm_and_native_execution(){
 let mut snapshots=Vec::new();
 for profile in [1u32,3] {for sixteen in [false,true] {for tag in [0u64,1,127,255] {for a1 in [false,true] {
  let(mut c,t,mut ram,step,_)=fixture(sixteen,&[scalar(3,1,1,0),HLT]);
  c.profile=profile;c.sctlr=if profile==1{0x30d00803}else{0x30d00801};
  let other=255-tag;c.ttbr0|=(if a1{other}else{tag})<<48;c.ttbr1|=(if a1{tag}else{other})<<48;c.tcr|=u64::from(a1)<<22;
  assert_eq!(unsafe{vf_stage1_controls_probe(&c)},1);assert!(nextcore_memory_service::stage1::controls_valid(&c));
  let upper=(!0u64<<(if sixteen{47}else{48}))|VA;
  {let mut svc=MemoryServiceV2::new(&mut ram,RAM,&t,TABLES,c).unwrap();assert_eq!(svc.test_asid8_tag(),tag as u16);
   for address in [VA,upper,VA,upper] {let reply=svc.execute(&asid8_request(c,address,FETCH));assert_eq!(reply.result,0);assert_eq!(reply.value0,u64::from(scalar(3,1,1,0)));assert_eq!(svc.test_asid8_tag(),tag as u16);}
   assert_eq!(svc.controls(),c);
  }
  for entry in [VA,upper] {
   let before=ram.clone();let(r,q)=run(c,&t,&mut ram,entry,[0,entry+step as u64,2,3],0);let b=r.base.execution.base;
   assert_eq!((b.status,b.retired,b.x0),(1,2,0xa5a5a5a5a5a5a5a5));assert_eq!(ram,before);assert_eq!(q.len(),3);assert!(q.iter().all(|q|q.controls==c));
   snapshots.extend_from_slice(unsafe{core::slice::from_raw_parts((&r as *const Run).cast::<u8>(),core::mem::size_of::<Run>())});
   for request in q {snapshots.extend_from_slice(unsafe{core::slice::from_raw_parts((&request as *const Request).cast::<u8>(),core::mem::size_of::<Request>())});}
   snapshots.extend_from_slice(&ram);
  }
 }}}}
 std::fs::write(std::env::var_os("NEXTCORE_ASID8_SNAPSHOT").unwrap(),snapshots).unwrap();
}
#[test]fn asid8_validator_negatives_and_dynamic_stays_zero(){
 for profile in [1u32,3] {for sixteen in [false,true] {let(mut c,_,_,_,_)=fixture(sixteen,&[HLT]);c.profile=profile;c.sctlr=if profile==1{0x30d00803}else{0x30d00801};
  for bit in [0,1,7,15,16,22,31,32,47,48,55,56,63] {for which in 0..4 {
   let mut x=c;match which{0=>x.ttbr0^=1u64<<bit,1=>x.ttbr1^=1u64<<bit,2=>x.tcr^=1u64<<bit,_=>x.sctlr^=1u64<<bit};
   assert_eq!(unsafe{vf_stage1_controls_probe(&x)}!=0,nextcore_memory_service::stage1::controls_valid(&x));
  }}
  for bad in [0,1,2,3,4,5,6,7] {let mut x=c;match bad{0=>x.ttbr0|=1<<56,1=>x.ttbr1|=1<<63,2=>x.tcr|=1<<36,3=>x.tcr|=1<<37,4=>x.tcr|=1<<38,5=>x.sctlr|=1<<25,6=>x.hcr=1,_=>x.scr=1};assert_eq!(unsafe{vf_stage1_controls_probe(&x)},0);assert!(!nextcore_memory_service::stage1::controls_valid(&x));}
  let mut d=c;d.abi_version=3;d.profile=2;d.sctlr|=2;
  assert_eq!(unsafe{test_asid8_dynamic(&d)},1);assert!(nextcore_memory_service::dynamic::controls_valid_dynamic(&d));
  for k in 0..4 {let mut x=d;match k{0=>x.ttbr0|=1<<48,1=>x.ttbr1|=1<<48,2=>x.tcr|=1<<22,_=>x.tcr|=1<<36};assert_eq!(unsafe{test_asid8_dynamic(&x)},0);assert!(!nextcore_memory_service::dynamic::controls_valid_dynamic(&x));}
 }}
}
#[test]fn asid8_context_isolation_and_changed_request_no_store(){
 for sixteen in [false,true] {let(mut c,t,mut ram,step,leaf)=fixture(sixteen,&[HLT]);c.ttbr0|=127<<48;
  ram[step..step+8].copy_from_slice(&0x1122334455667788u64.to_le_bytes());
  let mut other=t.clone();other[leaf..leaf+8].copy_from_slice(&(RAM+step as u64|0x403).to_le_bytes());
  for tables in [&t,&other,&t] {let before=ram.clone();let mut svc=MemoryServiceV2::new(&mut ram,RAM,tables,TABLES,c).unwrap();let expected=if core::ptr::eq(tables,&other){0x1122334455667788}else{u64::from_le_bytes(before[..8].try_into().unwrap())};
   for _ in 0..2{assert_eq!(svc.execute(&asid8_request(c,VA,LOAD)).value0,expected);}
   for kind in 0..3 {let mut q=asid8_request(c,VA,STORE);q.value0=0xdead;match kind{0=>q.controls.ttbr0^=1<<48,1=>q.controls.ttbr1^=1<<48,_=>q.controls.tcr^=1<<22};assert_eq!(svc.execute(&q).result,INVALID_REQUEST);}
   drop(svc);assert_eq!(ram,before);
  }
 }
}
#[test]fn asid8_ips_actual_backend_boundaries(){
 for sixteen in [false,true] {for ips in [2u64,5] {for pa_bit in [39u32,40,47] {
  let(mut c,mut t,mut ram,_,leaf)=fixture(sixteen,&[HLT]);let high=1u64<<pa_bit;c.ttbr0|=255<<48;c.tcr=(c.tcr&!(7<<32))|(ips<<32);t[leaf..leaf+8].copy_from_slice(&(high|0x403).to_le_bytes());
  let mut svc=MemoryServiceV2::new(&mut ram,high,&t,TABLES,c).unwrap();let q=svc.execute(&asid8_request(c,VA,FETCH));
  if ips==2&&pa_bit>=40{assert_eq!(q.result,GUEST_FAULT);assert_eq!(q.fault,ADDRESS_SIZE);}else{assert_eq!(q.result,0);assert_eq!(q.value0,u64::from(HLT));}
 }}}
}

unsafe extern "C" {fn test_asid8_cpu_mismatch(c:*const Controls,bytes:*mut u8,cb:Callback,owner:*mut c_void,kind:u32)->i32;}
#[test]fn asid8_cpu_snapshot_change_rejects_before_fetch_or_commit(){
 for kind in 0..3 {let(mut c,t,mut ram,_,_)=fixture(false,&[scalar(3,0,1,0),HLT]);c.ttbr0|=127<<48;let before=ram.clone();let code=Code::new();
 let mut o=Owner{service:MemoryServiceV2::new(&mut ram,RAM,&t,TABLES,c).unwrap(),requests:Vec::new(),corruption:0};
 assert_eq!(unsafe{test_asid8_cpu_mismatch(&c,code.0,callback,(&mut o as *mut Owner<'_>).cast(),kind)},1);assert!(o.requests.is_empty());drop(o);assert_eq!(ram,before);}
}
