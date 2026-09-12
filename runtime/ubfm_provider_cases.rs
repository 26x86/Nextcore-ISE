//! Authored profile coverage; expectation places individual source bits.
pub struct Case {pub word:u32,pub source:u64,pub destination:usize,pub expected:u64,pub flags:u64}
pub fn cases()->Vec<Case> {
    let mut cases=Vec::new();
    for width in [32u32,64] {for rotate in [0,1,width/2,width-1] {for end in [0,1,width/2,width-1] {
        for mode in 0..4 {for source in [0,u64::MAX,0xdeadbeef01234567] {
            let rn=if mode&1!=0 {31}else{1};let rd=if mode==3 {31}else if mode==2 {1}else{3};
            let input=if rn==31 {0}else{source};let mut expected=0;
            for bit in 0..width {
                if input&(1u64<<bit)==0 {continue;}
                if end>=rotate && bit>=rotate && bit<=end {expected|=1u64<<(bit-rotate);}
                if end<rotate && bit<=end {expected|=1u64<<(bit+width-rotate);}
            }
            cases.push(Case{word:0x53000000|(u32::from(width==64)<<31)|(u32::from(width==64)<<22)|
                (rotate<<16)|(end<<10)|(rn<<5)|rd,source,destination:rd as usize,expected,flags:((rotate+end+mode)&15)as u64});
        }}
    }}}
    cases
}
pub const INVALID:[u32;11]=[0xd3000023,0x53400023,0x53200023,0x53008023,0x53608023,
    0xb3000023,0x33400023,0x13000023,0x93400023,0x73000023,0xf3400023];
