// Appended to a scratch copy of the canonical reference, not production code.
#[test]fn logical_shifted_actual_arm_oracle(){
 for &(word,a,b,value,nzcv) in LOGICAL_ORACLE {
  let mut ram=[0u8;8];ram[..4].copy_from_slice(&word.to_le_bytes());let mut cpu=GuestCpuState::reset(0);cpu.x[0]=a;cpu.x[1]=b;cpu.x[2]=0xface;cpu.sp=0x12345678;cpu.pstate=0xf00003c5;
  assert_eq!(cpu.execute_one(&mut RamBus{ram:&mut ram,base:0}),StepResult::Continue);
  assert_eq!((cpu.x[0],cpu.x[1],cpu.x[2],cpu.sp,cpu.pc,cpu.pstate),(a,b,value,0x12345678,4,nzcv as u32|0x3c5));
 }
 for op in 0..8 {for shift in 0..4 {for amount in 32..64 {
  let word=0x0a000000u32|(op>>1)<<29|(op&1)<<21|shift<<22|amount<<10;let mut ram=[0u8;8];ram[..4].copy_from_slice(&word.to_le_bytes());let mut cpu=GuestCpuState::reset(0);cpu.x[0]=0xabcdef;cpu.pstate=0xf00003c5;
  assert_ne!(cpu.execute_one(&mut RamBus{ram:&mut ram,base:0}),StepResult::Continue);
  assert_eq!((cpu.x[0],cpu.pc,cpu.pstate),(0xabcdef,0,0xf00003c5));
 }}}
}
