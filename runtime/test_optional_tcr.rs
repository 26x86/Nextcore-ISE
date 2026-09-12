// Appended to copied real modules, never compiled into production.
#[cfg(not(any(optional_tcr_mmu,optional_tcr_service)))]
#[test]fn optional_tcr_bank_rejection_preserves_state_and_warm_cache(){
 use crate::mmu::Access;
 for on in [false,true]{for combo in 1u64..16{
  let mut cpu=GuestCpuState::reset(0x1234);cpu.x=[0x55;31];cpu.sp=0x8800;cpu.pstate=0xf00003c5;cpu.write_sysreg(SystemRegister::TcrEl1,16).unwrap();cpu.sys.sctlr_el1=u64::from(on);cpu.sys.ttbr0_el1=0x1000|(127<<48);
  assert!(cpu.mmu.configure_tcr(0x1000,0,16,127));assert!(cpu.mmu.translate(0,Access::Read,ExceptionLevel::El1,|pa|match pa{0x1000=>Some(0x2003),0x2000=>Some(0x3003),0x3000=>Some(0x4003),0x4000=>Some(0x80000403),_=>None}).is_ok());
  cpu.mmu.enabled=on;let before=cpu;
  assert_eq!(cpu.write_sysreg(SystemRegister::TcrEl1,16|(combo<<39)),Err(SysRegFault::InvalidValue));
  assert_eq!((cpu.x,cpu.sp,cpu.sp_el,cpu.pc,cpu.pstate,cpu.current_el),(before.x,before.sp,before.sp_el,before.pc,before.pstate,before.current_el));
  // All bank fields are checked individually; padding bytes are never read.
  assert_eq!(cpu.sys.tpidr_el0,before.sys.tpidr_el0);assert_eq!(cpu.sys.tpidrro_el0,before.sys.tpidrro_el0);assert_eq!(cpu.sys.tpidr_el1,before.sys.tpidr_el1);assert_eq!(cpu.sys.sctlr_el1,before.sys.sctlr_el1);assert_eq!(cpu.sys.ttbr0_el1,before.sys.ttbr0_el1);assert_eq!(cpu.sys.ttbr1_el1,before.sys.ttbr1_el1);assert_eq!(cpu.sys.tcr_el1,before.sys.tcr_el1);assert_eq!(cpu.sys.mair_el1,before.sys.mair_el1);assert_eq!(cpu.sys.vbar_el1,before.sys.vbar_el1);assert_eq!(cpu.sys.esr_el1,before.sys.esr_el1);assert_eq!(cpu.sys.far_el1,before.sys.far_el1);assert_eq!(cpu.sys.elr_el1,before.sys.elr_el1);assert_eq!(cpu.sys.spsr_el1,before.sys.spsr_el1);assert_eq!(cpu.sys.cntfrq_el0,before.sys.cntfrq_el0);assert_eq!(cpu.sys.cntpct_el0,before.sys.cntpct_el0);assert_eq!(cpu.sys.cntp_ctl_el0,before.sys.cntp_ctl_el0);assert_eq!(cpu.sys.cntp_cval_el0,before.sys.cntp_cval_el0);assert_eq!(cpu.sys.id_aa64mmfr0_el1,before.sys.id_aa64mmfr0_el1);assert_eq!(cpu.sys.id_aa64isar1_el1,before.sys.id_aa64isar1_el1);assert_eq!(cpu.sys.cntvct_el0,before.sys.cntvct_el0);assert_eq!(cpu.sys.cntv_ctl_el0,before.sys.cntv_ctl_el0);assert_eq!(cpu.sys.cntv_cval_el0,before.sys.cntv_cval_el0);assert_eq!(cpu.sys.cntp_tval_el0,before.sys.cntp_tval_el0);assert_eq!(cpu.sys.hcr_el2,before.sys.hcr_el2);assert_eq!(cpu.sys.cnthctl_el2,before.sys.cnthctl_el2);assert_eq!(cpu.sys.vbar_el2,before.sys.vbar_el2);assert_eq!(cpu.sys.esr_el2,before.sys.esr_el2);assert_eq!(cpu.sys.far_el2,before.sys.far_el2);assert_eq!(cpu.sys.elr_el2,before.sys.elr_el2);assert_eq!(cpu.sys.spsr_el2,before.sys.spsr_el2);assert_eq!(cpu.sys.scr_el3,before.sys.scr_el3);assert_eq!(cpu.sys.vbar_el3,before.sys.vbar_el3);assert_eq!(cpu.sys.esr_el3,before.sys.esr_el3);assert_eq!(cpu.sys.far_el3,before.sys.far_el3);assert_eq!(cpu.sys.elr_el3,before.sys.elr_el3);assert_eq!(cpu.sys.spsr_el3,before.sys.spsr_el3);
  assert_eq!(cpu.mmu.asid,127);assert_eq!(cpu.mmu.enabled,on);let mut retained=cpu.mmu;retained.enabled=true;assert!(retained.translate(0,Access::Read,ExceptionLevel::El1,|_|panic!("rejected TCR must preserve warm cache")).is_ok());
 }}
}
#[cfg(optional_tcr_mmu)]
#[test]fn optional_tcr_direct_configuration_is_transactional(){
 for sixteen in [false,true]{for combo in 1u64..16{
  let mut mmu=VfMmu::disabled();let step=if sixteen{0x4000}else{0x1000};let tcr=if sixteen{17|(2<<14)}else{16};let start=if sixteen{1}else{0};
  assert!(mmu.configure_tcr(step,0,tcr,127));assert!(mmu.translate(0,Access::Read,ExceptionLevel::El1,|pa|{let n=pa/step;Some(if n==4-start{0x80000403}else{pa+step|3})}).is_ok());let before=mmu;
  assert!(!mmu.configure_tcr(step+0x10000,0,tcr|(combo<<39),255));
  assert_eq!(mmu.enabled,before.enabled);assert_eq!(mmu.ttbr0,before.ttbr0);assert_eq!(mmu.ttbr1,before.ttbr1);assert_eq!(mmu.tcr_t0sz,before.tcr_t0sz);assert_eq!(mmu.tcr_t1sz,before.tcr_t1sz);assert_eq!(mmu.asid,before.asid);assert_eq!(mmu.granule,before.granule);assert_eq!(mmu.walk_disabled,before.walk_disabled);assert_eq!(mmu.physical_address_mask,before.physical_address_mask);assert_eq!(mmu.next,before.next);assert_eq!(mmu.strict_nc,before.strict_nc);
  for(a,b)in mmu.tlb.iter().zip(before.tlb.iter()){assert_eq!(a.valid,b.valid);assert_eq!(a.el10,b.el10);assert_eq!(a.asid,b.asid);assert_eq!(a.va_tag,b.va_tag);assert_eq!(a.pa_base,b.pa_base);assert_eq!(a.page_shift,b.page_shift);assert_eq!(a.writable,b.writable);assert_eq!(a.user_accessible,b.user_accessible);assert_eq!(a.executable_el0,b.executable_el0);assert_eq!(a.executable_privileged,b.executable_privileged);assert_eq!(a.level,b.level);assert_eq!(a.descriptor_pa,b.descriptor_pa);assert_eq!(a.attributes,b.attributes);}
  assert!(mmu.translate(0,Access::Read,ExceptionLevel::El1,|_|panic!("rejected configure must preserve warm cache")).is_ok());
 }}
}
#[cfg(optional_tcr_service)]
#[test]fn optional_tcr_immutable_and_dynamic_admission_stays_closed(){
 use nextcore_memory_service::{abi_v2::Controls,stage1::controls_valid,dynamic::controls_valid_dynamic};
 for sixteen in [false,true]{for profile in [1,3,2]{
  let tcr=if sixteen{17|(17<<16)|(2<<14)|(1<<30)|(5<<32)}else{16|(16<<16)|(2<<30)|(5<<32)};
  let c=Controls{abi_version:if profile==2{3}else{2},struct_size:80,profile,sctlr:if profile==3{0x30d00801}else{0x30d00803},ttbr0:0x10000000,ttbr1:0x10000000,tcr,mair:0x44,epoch:1,..Default::default()};
  let valid=|v:&Controls|if profile==2{controls_valid_dynamic(v)}else{controls_valid(v)};assert!(valid(&c));
  for combo in 1u64..16{let mut bad=c;bad.tcr|=combo<<39;assert!(!valid(&bad));}
 }}
}
