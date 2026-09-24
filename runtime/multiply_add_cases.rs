// SPDX-License-Identifier: BSD-4-Clause; authored provider arithmetic cases.
#[derive(Clone,Copy)]
pub struct Case { pub word:u32, pub wide:bool, pub sub:bool, pub rn:usize, pub rm:usize, pub ra:usize, pub rd:usize, pub initial:[u64;4] }
pub fn each(mut visit:impl FnMut(Case)) {
    let values=[0u64,1,u64::MAX,0x7fffffff,0x80000000,0xffffffff,0x8000000000000000,0xabcdef0187654321];
    let roles=[(0,1,2,3),(31,1,2,3),(0,31,2,3),(0,1,31,3),(0,1,2,31),(0,1,2,0),(0,1,2,1),(0,1,2,2),(0,0,0,0),(31,31,31,31)];
    for wide in [false,true] {for sub in [false,true] {for (i,&value) in values.iter().enumerate() {for (rn,rm,ra,rd) in roles {
        let word=0x1b000000|(u32::from(wide)<<31)|(u32::from(sub)<<15)|((rm as u32)<<16)|((ra as u32)<<10)|((rn as u32)<<5)|rd as u32;
        visit(Case{word,wide,sub,rn,rm,ra,rd,initial:[value,values[(i+1)%8],values[(i+3)%8],0xaabbccddeeff0011]});
    }}}}
}
pub fn expected(c:Case,initial:[u64;4])->[u64;4] {
    let mask=if c.wide{u64::MAX}else{u32::MAX as u64};
    let read=|r:usize|if r==31{0u128}else{u128::from(initial[r]&mask)};
    let product=read(c.rn)*read(c.rm);let total=if c.sub{u128::from(mask)+1+read(c.ra)-(product&u128::from(mask))}else{read(c.ra)+product};
    let mut result=initial;if c.rd!=31{result[c.rd]=total as u64&mask;}result
}
