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
    for sixteen in [false,true] {for read in [false,true] {for paired in [false] {
        let word=if paired {pair(true,read,2,0,1,0,2)}else{0x38000000|3<<30|u32::from(read)<<22|0x1ff<<12|1<<5};
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
            let(r,requests)=run(c,&tables,&mut ram,VA,[values[0],address+1,values[1],4],0);
            let b=r.base.execution.base;
            assert_eq!((b.status,b.retired,r.base.provider_status,r.base.completed_data_operations),(1,2,0,1));
            assert_eq!(b.x1,address+1);assert_eq!(b.x0,if read {loaded[0]}else{values[0]});
            assert_eq!(b.x2,if read&&paired {loaded[1]}else{values[1]});
            assert_eq!(requests.len(),3);assert_eq!(requests[1].width,8);
            assert_eq!(requests[1].count,count as u32);assert_eq!(requests[1].address,address);
            assert_eq!(ram,expected);
        }
    }}}
}
#[test]fn second_page_fault_is_precise_and_never_partially_commits() {
    for sixteen in [false,true] {for read in [false,true] {for paired in [false] {
        let word=if paired {pair(true,read,2,0,1,0,2)}else{0x38000000|3<<30|u32::from(read)<<22|0x1ff<<12|1<<5};
        for failure in 0..3 {
            if read && failure==2 {continue;}
            let(c,mut tables,mut ram,step,leaf)=unaligned_fixture(sixteen,&[word,HLT]);
            let entry=leaf+16;
            let mut descriptor=u64::from_le_bytes(tables[entry..entry+8].try_into().unwrap());
            descriptor=match failure {0=>0,1=>descriptor&!0x400,_=>descriptor|0x80};
            tables[entry..entry+8].copy_from_slice(&descriptor.to_le_bytes());
            let address=VA+2*step as u64-3;let before=ram.clone();
            let(r,requests)=run(c,&tables,&mut ram,VA,[0x12345678,address+1,0xabcdef,4],0);
            let b=r.base.execution.base;let fsc=match failure {0=>7,1=>11,_=>15};
            assert_eq!((b.status,b.retired,r.base.provider_status,r.base.completed_data_operations),(17,0,0,0));
            assert_eq!((b.pc,b.x0,b.x1,b.x2),(VA,0x12345678,address+1,0xabcdef));
            assert_eq!(r.last_reply.address,VA+2*step as u64);
            assert_eq!(r.base.guest_far,VA+2*step as u64);
            assert_eq!(r.base.execution.esr,0x96000000|fsc|if read {0}else{64});
            assert_eq!(requests.len(),2);assert_eq!(ram,before);
        }
    }}}
}

#[test]fn unscaled_thirteen_forms_canonical_request_and_values() {
 for size in 0..4 {for opc in 0..4 {if opc>=2 && (size==3 || (size==2 && opc==3)){continue;}
 for displacement in [-256i64,-1,255] {for rt in [0u32,1,31] {
  let word=0x38000000|size<<30|opc<<22|((displacement as u32)&511)<<12|1<<5|rt;
  let(c,t,mut ram,step,_)=unaligned_fixture(true,&[word,HLT]);let address=VA+step as u64+8;
  let base=address.wrapping_sub(displacement as u64);let width=1usize<<size;let mut initial=[0x1122334455667788u64,base,22,33];
  let raw=0x8687848582838081u64;ram[3*step+8..3*step+16].copy_from_slice(&raw.to_le_bytes());let mut expected=ram.clone();
  if opc==0 {let value=if rt==31{0}else{initial[rt as usize]};for i in 0..width {expected[3*step+8+i]=(value>>(8*i))as u8;}}
  else if rt!=31 {let shift=64-width*8;let v=if opc>=2 {((raw<<shift)as i64>>shift)as u64}else{raw<<shift>>shift};initial[rt as usize]=if opc==3||(opc==1&&width<8){v as u32 as u64}else{v};}
  let(result,q)=run(c,&t,&mut ram,VA,[0x1122334455667788,base,22,33],0);let b=result.base.execution.base;
  assert_eq!((b.status,b.retired,q.len(),result.base.completed_data_operations),(1,2,3,1));
  assert_eq!((q[1].address,q[1].width,q[1].count,q[1].operation),(address,width as u32,1,if opc==0{STORE}else{LOAD}));
  assert_eq!((b.x0,b.x1,b.x2,b.x3),(initial[0],initial[1],22,33));assert_eq!(ram,expected);
 }}}}
}
