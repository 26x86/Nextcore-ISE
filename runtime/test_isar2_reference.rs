#[test]fn isar2_actual_arm_oracle(){
 for &(word,_,_,value,nzcv) in ISAR2_ORACLE {
  assert_eq!(value,0);let mut ram=[0u8;8];ram[..4].copy_from_slice(&word.to_le_bytes());let before=ram;let mut cpu=GuestCpuState::reset(0);for i in 0..31{cpu.x[i]=0x100+i as u64;}let mut expected=cpu.x;let rd=(word&31)as usize;if rd!=31{expected[rd]=value;}cpu.sp=0x12345678;cpu.pstate=0xf00003c5;
  assert_eq!(cpu.execute_one(&mut RamBus{ram:&mut ram,base:0}),StepResult::Continue);assert_eq!(cpu.x,expected);assert_eq!((cpu.sp,cpu.pc,cpu.pstate),(0x12345678,4,nzcv as u32|0x3c5));assert_eq!(ram,before);
 }
 for failure in 0..11 {let mut word=0xd5380640u32;let mut cpu=GuestCpuState::reset(0);cpu.x[0]=0xabc;cpu.sp=0x800;cpu.pstate=0xf00003c5;
  match failure{0=>{cpu.current_el=ExceptionLevel::El0;cpu.pstate=0xf00003c0;},1=>cpu.sys.hcr_el2=1,2=>cpu.sys.scr_el3=1,3=>word^=1<<21,4=>word^=1<<5,5=>word^=1<<12,6=>word^=1<<16,7=>cpu.sys.hcr_el2=1u64<<63,8=>cpu.sys.scr_el3=1u64<<63,9=>{cpu.current_el=ExceptionLevel::El2;cpu.pstate=0xf00003c9;},_=>{cpu.current_el=ExceptionLevel::El3;cpu.pstate=0xf00003cd;}};let state=cpu.pstate;let mut ram=[0u8;8];ram[..4].copy_from_slice(&word.to_le_bytes());
  assert_ne!(cpu.execute_one(&mut RamBus{ram:&mut ram,base:0}),StepResult::Continue);assert_eq!((cpu.x[0],cpu.sp,cpu.pc,cpu.pstate),(0xabc,0x800,0,state));
 }
}

#[test]fn isar2_extension_policy_rejects(){for &word in EXTENSION_WORDS{let mut cpu=GuestCpuState::reset(0);cpu.x[0]=11;cpu.x[1]=22;cpu.x[2]=32;cpu.sp=0x800;cpu.pstate=0xf00003c5;let before_regs=cpu.x;let mut ram=[0u8;64];ram[..4].copy_from_slice(&word.to_le_bytes());let before=ram;let result=cpu.execute_one(&mut RamBus{ram:&mut ram,base:0});assert!(matches!(result,StepResult::Exception(ExceptionKind::UndefinedInstruction|ExceptionKind::SystemRegisterTrap)),"extension {word:x}: {result:?}");assert_eq!(cpu.x,before_regs);assert_eq!((cpu.sp,cpu.pc,cpu.pstate),(0x800,0,0xf00003c5));assert_eq!(ram,before);}}
