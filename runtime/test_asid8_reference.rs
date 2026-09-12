// Appended inside the real arch module, retaining all existing tests.
#[test]fn asid8_reference_selection_and_transactional_reject(){
 for sixteen in [false,true]{for tag in [0u64,1,127,255]{for a1 in [false,true]{
  let mut cpu=GuestCpuState::reset(0);let other=255-tag;
  let tcr=if sixteen{17|(17<<16)|(2<<14)|(1<<30)|(5<<32)}else{16|(16<<16)|(2<<30)|(5<<32)};
  cpu.write_sysreg(SystemRegister::Ttbr0El1,0x10000000|((if a1{other}else{tag})<<48)).unwrap();
  cpu.write_sysreg(SystemRegister::Ttbr1El1,0x10000000|((if a1{tag}else{other})<<48)).unwrap();
  cpu.write_sysreg(SystemRegister::TcrEl1,tcr|(u64::from(a1)<<22)).unwrap();cpu.write_sysreg(SystemRegister::SctlrEl1,0x30d00803).unwrap();assert_eq!(cpu.mmu.asid,tag as u16);
  for (reg,bad) in [(SystemRegister::TcrEl1,cpu.sys.tcr_el1|1<<36),(SystemRegister::Ttbr0El1,cpu.sys.ttbr0_el1|1<<56),(SystemRegister::Ttbr1El1,cpu.sys.ttbr1_el1|1<<63)] {
   let before=(cpu.sys.ttbr0_el1,cpu.sys.ttbr1_el1,cpu.sys.tcr_el1,cpu.mmu.asid,cpu.pc,cpu.sp,cpu.pstate,cpu.x);
   assert!(cpu.write_sysreg(reg,bad).is_err());assert_eq!((cpu.sys.ttbr0_el1,cpu.sys.ttbr1_el1,cpu.sys.tcr_el1,cpu.mmu.asid,cpu.pc,cpu.sp,cpu.pstate,cpu.x),before);
  }
 }}}
}
