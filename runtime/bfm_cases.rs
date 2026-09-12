// SPDX-License-Identifier: BSD-4-Clause; independent provider bit-origin oracle.
#[derive(Clone,Copy)]
pub struct Case { pub word:u32, pub width:u32, pub r:u32, pub s:u32, pub rn:usize, pub rd:usize, pub initial:[u64;4] }
pub fn each(mut visit:impl FnMut(Case)) {
    for width in [32,64] {for r in 0..width {for s in 0..width {for (rn,rd) in [(1,2),(31,2),(1,31),(1,1),(31,31)] {
        let word=0x33000000|(u32::from(width==64)<<31)|(u32::from(width==64)<<22)|(r<<16)|(s<<10)|((rn as u32)<<5)|rd as u32;
        visit(Case{word,width,r,s,rn,rd,initial:[0x1234,0x89abcdef01234567u64.rotate_left(r),0xa5a5a5a55a5a5a5au64.rotate_left(s),0xfedcba9876543210]});
    }}}}
}
pub fn expected(c:Case,initial:[u64;4])->[u64;4] {
    let source=if c.rn==31{0}else{initial[c.rn]};let old=if c.rd==31{0}else{initial[c.rd]};let mut value=0;
    for bit in 0..c.width {
        let first=if c.s>=c.r{0}else{c.width-c.r};let last=if c.s>=c.r{c.s-c.r}else{c.width-c.r+c.s};
        let b=if bit<first||bit>last{(old>>bit)&1}else{(source>>(if c.s>=c.r{bit+c.r}else{bit-first}))&1};value|=b<<bit;
    }
    let mut out=initial;if c.rd!=31{out[c.rd]=value;}out
}
pub fn invalid()->[u32;8] {[0xb3000002,0x33400002,0x33200002,0x33008002,0x33800002,0x73000002,0x13000002,0x93400002]}
