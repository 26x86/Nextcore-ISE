//! Adapted QEMU APA1 control oracle; generated constants are supplied by the runner.
#![allow(dead_code)]
#[path="preos/src/pauth.rs"] mod pauth;
// ORACLE_INSERT
fn main() {
 let mut s=pauth::PauthState::reset();
 s.keys=[pauth::Key{lo:0x48ad369c24681357,hi:0xb752c963db97eca8};5];
 let mut ffi_cases=0;
 for &(t,p,k,expected) in ORACLE {
  let a=s.sign(p,0x9876,k,t).unwrap();
  let got=[a,s.authenticate(a,0x9876,k,t).unwrap(),pauth::PauthState::strip(p,t).unwrap(),pauth::PauthState::strip(a,t).unwrap(),s.authenticate(a^(1<<54),0x9876,k,t).unwrap(),s.authenticate(p,0x9876,k,t).unwrap()];
  assert_eq!(got,expected,"tcr={t:x} ptr={p:x} key={k}");
  // Exercise the actual C ABI callback with LLVM-assembled register instructions.
  for (op,input,want) in [(0,p,expected[0]),(1,a,expected[1]),(2,p,expected[2]),(2,a,expected[3]),(1,a^(1<<54),expected[4]),(1,p,expected[5])] {
   let mut regs=core::array::from_fn(|i|0x1000+i as u64);regs[2]=0x9876;regs[3]=input;
   let mut c=pauth::PauthContext::new(regs,0x12340000,0x8000,SCTLR,t,s,1);
   assert_eq!(unsafe{pauth::vf_preos_pauth_step(&mut c,WORDS[k][op])},0);
   regs[3]=want;assert_eq!(c.x,regs);assert_eq!(c.pc,0x8004);assert_eq!(c.sp,0x12340000);assert_eq!(c.tcr,t);assert_eq!(c.sctlr,SCTLR);assert_eq!(c.current_el,1);assert_eq!(c.state(),s);ffi_cases+=1;
  }
 }
 let base=0x540118010u64;
 let invalid=[base|(1<<37),base|(1<<38),base|(1<<59),(base&!0x3f003f)|18|(18<<16),(base&!0x3f003f)|15|(15<<16)];
 let mut unsupported=0;
 for t in invalid {for p in [0x130,0xffff800000000130] {for k in 0..4 {
  assert!(s.sign(p,0x9876,k,t).is_err());assert!(s.authenticate(p,0x9876,k,t).is_err());assert!(pauth::PauthState::strip(p,t).is_err());unsupported+=1;
 }}}
 for key in [4,5,usize::MAX] {assert!(s.sign(0x130,0x9876,key,base).is_err());assert!(s.authenticate(0x130,0x9876,key,base).is_err());}
 println!("oracle_rows={} ffi_cases={} unsupported_rows={} invalid_keys=3",ORACLE.len(),ffi_cases,unsupported);
}
