// Appended to a scratch copy of the canonical reference, not production code.
#[test]fn variable_shift_actual_arm_oracle(){
 for &(word,a,b,value,nzcv) in VARIABLE_SHIFT_ORACLE {
  let mut ram=[0u8;8];ram[..4].copy_from_slice(&word.to_le_bytes());let mut cpu=GuestCpuState::reset(0);cpu.x[0]=a;cpu.x[1]=b;cpu.x[2]=0xface;cpu.sp=0x12345678;cpu.pstate=0xf00003c5;
  assert_eq!(cpu.execute_one(&mut RamBus{ram:&mut ram,base:0}),StepResult::Continue);
  let mut expected=[a,b,0xface];let rd=(word&31)as usize;if rd!=31{expected[rd]=value;}
  assert_eq!(&cpu.x[..3],&expected);assert_eq!((cpu.sp,cpu.pc,cpu.pstate),(0x12345678,4,nzcv as u32|0x3c5));
 }
 for wide in [0u32,1] {for extra in [1u32<<29,1<<30,1<<15] {
  let word=0x1ac02000|wide<<31|extra;let mut ram=[0u8;8];ram[..4].copy_from_slice(&word.to_le_bytes());let mut cpu=GuestCpuState::reset(0);cpu.x[0]=0xabcdef;cpu.pstate=0xf00003c5;
  assert_ne!(cpu.execute_one(&mut RamBus{ram:&mut ram,base:0}),StepResult::Continue);
  assert_eq!((cpu.x[0],cpu.pc,cpu.pstate),(0xabcdef,0,0xf00003c5));
 }}
}
