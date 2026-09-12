// Authored test fragment appended to the existing canonical fixture by the runner.


fn unaligned_fixture(sixteen:bool,words:&[u32])->(Controls,Vec<u8>,Vec<u8>,usize,usize) {
    let(mut c,tables,ram,step,leaf)=fixture(sixteen,words);
    c.profile=3;c.sctlr=0x30d00801;(c,tables,ram,step,leaf)
}
fn backing(va:u64,step:usize)->usize {
    let offset=(va-VA)as usize;
    if offset<2*step {3*step+offset-step}else{step+offset-2*step}
}
#[test]fn scalar_and_pair_unaligned_transfers_preflight_noncontiguous_pages() {
    for mode in [1u32,3] {for sixteen in [false,true] {for read in [false,true] {for paired in [false] {
        let word=if paired {pair(true,read,2,0,1,0,2)}else{0x38000000|3<<30|u32::from(read)<<22|0x1ff<<12|mode<<10|1<<5};
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
            let(r,requests)=run(c,&tables,&mut ram,VA,[values[0],address+if mode==3 {1}else{0},values[1],4],0);
            let b=r.base.execution.base;
            assert_eq!((b.status,b.retired,r.base.provider_status,r.base.completed_data_operations),(1,2,0,1));
            assert_eq!(b.x1,address.wrapping_sub(if mode==1 {1}else{0}));assert_eq!(b.x0,if read {loaded[0]}else{values[0]});
            assert_eq!(b.x2,if read&&paired {loaded[1]}else{values[1]});
            assert_eq!(requests.len(),3);assert_eq!(requests[1].width,8);
            assert_eq!(requests[1].count,count as u32);assert_eq!(requests[1].address,address);
            assert_eq!(ram,expected);
        }
    }}}}
}
#[test]fn second_page_fault_is_precise_and_never_partially_commits() {
    for mode in [1u32,3] {for sixteen in [false,true] {for read in [false,true] {for paired in [false] {
        let word=if paired {pair(true,read,2,0,1,0,2)}else{0x38000000|3<<30|u32::from(read)<<22|0x1ff<<12|mode<<10|1<<5};
        for failure in 0..3 {
            if read && failure==2 {continue;}
            let(c,mut tables,mut ram,step,leaf)=unaligned_fixture(sixteen,&[word,HLT]);
            let entry=leaf+16;
            let mut descriptor=u64::from_le_bytes(tables[entry..entry+8].try_into().unwrap());
            descriptor=match failure {0=>0,1=>descriptor&!0x400,_=>descriptor|0x80};
            tables[entry..entry+8].copy_from_slice(&descriptor.to_le_bytes());
            let address=VA+2*step as u64-3;let before=ram.clone();
            let(r,requests)=run(c,&tables,&mut ram,VA,[0x12345678,address+if mode==3 {1}else{0},0xabcdef,4],0);
            let b=r.base.execution.base;let fsc=match failure {0=>7,1=>11,_=>15};
            assert_eq!((b.status,b.retired,r.base.provider_status,r.base.completed_data_operations),(17,0,0,0));
            assert_eq!((b.pc,b.x0,b.x1,b.x2),(VA,0x12345678,address+if mode==3 {1}else{0},0xabcdef));
            assert_eq!(r.last_reply.address,VA+2*step as u64);
            assert_eq!(r.base.guest_far,VA+2*step as u64);
            assert_eq!(r.base.execution.esr,0x96000000|fsc|if read {0}else{64});
            assert_eq!(requests.len(),2);assert_eq!(ram,before);
        }
    }}}}
}


#[test]fn indexed_all_forms_provider_writeback() {
 for mode in [1u32,3] {for size in 0..4 {for opc in 0..4 {if opc>=2 && (size==3 ||(size==2&&opc==3)){continue;}
 for offset in [-256i64,-1,0,255] {for sp in [false,true] {for rt in [0u32,31] {
  let rn=if sp{31}else{1};let word=0x38000000|size<<30|opc<<22|((offset as u32)&511)<<12|mode<<10|rn<<5|rt;
  let(c,t,mut ram,step,_)=unaligned_fixture(true,&[word,HLT]);let address=VA+step as u64+512;
  let base=address.wrapping_sub(if mode==3 {offset as u64}else{0});let updated=base.wrapping_add(offset as u64);let width=1usize<<size;
  let raw=0x8687848582838081u64;ram[3*step+512..3*step+520].copy_from_slice(&raw.to_le_bytes());let mut expected=ram.clone();let initial=0x1122334455667788u64;
  let mut value=initial;
  if opc==0{let v=if rt==31{0}else{initial};for i in 0..width{expected[3*step+512+i]=(v>>(8*i))as u8;}}
  else if rt!=31{let shift=64-width*8;value=if opc>=2{((raw<<shift)as i64>>shift)as u64}else{raw<<shift>>shift};if opc==3||(opc==1&&width<8){value=value as u32 as u64;}}
  let(result,q)=run_with_stack(c,&t,&mut ram,VA,if sp{base}else{VA+0x400},[initial,base,22,33],0,None);let b=result.base.execution.base;
  assert_eq!((b.status,b.retired,b.x0,b.x1,result.base.execution.sp),(1,2,value,if sp{base}else{updated},if sp{updated}else{VA+0x400}));
  assert_eq!((q.len(),q[1].operation,q[1].address,q[1].width,q[1].count),(3,if opc==0{STORE}else{LOAD},address,width as u32,1));assert_eq!(ram,expected);
 }}}}}}
}
#[test]fn indexed_overlap_rejected_without_data_or_writeback(){
 for mode in [1u32,3]{for opc in [0u32,1]{let word=0x38000000|3<<30|opc<<22|mode<<10|1<<5|1;let(c,t,mut ram,_,_)=unaligned_fixture(true,&[word]);let before=ram.clone();let(r,q)=run(c,&t,&mut ram,VA,[11,VA+8,22,33],0);let b=r.base.execution.base;assert_eq!((b.status,b.retired,b.pc,b.x1,q.len(),r.base.data_requests),(8,0,VA,VA+8,1,0));assert_eq!(ram,before);}}
}

#[test]fn indexed_repeated_pc_writeback_cache_equivalence(){
 // STR X0,[X1],#8; STR X0,[X1,#-8]!; B first.
 // Both stores target the same eight bytes; the two distinct writebacks cancel.
 let words=[0xf8008420,0xf81f8c20,0x17fffffe];
 let(c,t,mut ram,step,_)=unaligned_fixture(true,&words);let address=VA+step as u64+512;
 let mut expected=ram.clone();let value=0x1122334455667788u64;expected[3*step+512..3*step+520].copy_from_slice(&value.to_le_bytes());
 let(r,q)=run(c,&t,&mut ram,VA,[value,address,22,33],0);let b=r.base.execution.base;
 assert_eq!((b.status,b.retired,b.pc,b.x0,b.x1,b.x2,b.x3),(5,32,VA+8,value,address,22,33));
 assert_eq!((r.base.fetch_requests,r.base.data_requests,r.base.completed_data_operations,q.len()),(32,22,22,54));
 let mut cursor=0;for i in 0..32{assert_eq!((q[cursor].operation,q[cursor].address),(FETCH,VA+4*(i%3)));cursor+=1;if i%3!=2{assert_eq!((q[cursor].operation,q[cursor].address,q[cursor].width,q[cursor].count),(STORE,address,8,1));cursor+=1;}}
 assert_eq!(ram,expected);
 let mut snapshot=Vec::new();
 // ABI structs have explicit layout and initialized fields; these are public
 // result/request bytes, not an assertion about unexposed private CPU state.
 snapshot.extend_from_slice(unsafe{core::slice::from_raw_parts((&r as *const Run).cast::<u8>(),core::mem::size_of::<Run>())});
 for request in q{snapshot.extend_from_slice(unsafe{core::slice::from_raw_parts((&request as *const Request).cast::<u8>(),core::mem::size_of::<Request>())});}
 snapshot.extend_from_slice(&ram);std::fs::write(std::env::var("NEXTCORE_INDEXED_SNAPSHOT").unwrap(),snapshot).unwrap();
}
