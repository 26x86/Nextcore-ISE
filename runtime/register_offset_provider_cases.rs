//! Independently authored register-offset provider fixtures; no original images.
#![allow(dead_code)]
pub struct Case {pub word:u32,pub index:u64,pub offset:u64,pub bytes:usize,pub opc:u32,pub rt:usize}
pub fn word(size:u32,opc:u32,option:u32,scale:u32,rn:u32,rm:u32,rt:u32)->u32 {
    0x38200800|size<<30|opc<<22|rm<<16|option<<13|scale<<12|rn<<5|rt
}
pub fn cases()->Vec<Case> {
    let mut out=Vec::new();
    for size in 0..4 {for opc in 0..4 {if opc>=2 && (size==3 || (size==2 && opc==3)){continue;}
    for option in [2u32,3,6,7] {for scale in 0..2 {for index in [1u64,u64::MAX,0xdeadbeef00000001,0x80000000] {for rt in [1u32,2,3,31] {
        let mut signed=if option&1!=0 {i128::from(index)} else {i128::from(index&0xffffffff)};
        if option==6 && index&0x80000000!=0 {signed-=1i128<<32;}
        out.push(Case{word:word(size,opc,option,scale,1,2,rt),index,
            offset:(signed*(1i128<<if scale!=0 {size}else{0}))as u64,bytes:1usize<<size,opc,rt:rt as usize});
    }}}}}}
    out
}
pub fn expected(case:&Case,registers:&mut[u64;4],ram:&mut[u8],at:usize) {
    if case.opc==0 {
        let value=if case.rt==31 {0} else {registers[case.rt]};
        for i in 0..case.bytes {ram[at+i]=(value>>(i*8))as u8;}
    } else {
        let mut value=0u64;for i in 0..case.bytes {value|=u64::from(ram[at+i])<<(i*8);}
        let mut signed=i128::from(value);
        if case.opc>=2 && value&(1u64<<(case.bytes*8-1))!=0 {signed-=1i128<<(case.bytes*8);}
        if case.rt!=31 {registers[case.rt]=if case.opc==3 || (case.opc==1 && case.bytes<8) {signed as u32 as u64}else{signed as u64};}
    }
}
pub fn invalid()->Vec<u32> {
    let mut out=Vec::new();
    for size in 0..4 {for opc in 0..4 {for option in 0..8 {for vector in 0..2 {
        if vector!=0 || option&2==0 || (opc>=2 && (size==3 || (size==2 && opc==3))) {
            out.push(word(size,opc,option,1,1,2,3)|(vector<<26));
        }
    }}}}out
}
