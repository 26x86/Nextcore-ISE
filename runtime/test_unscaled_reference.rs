// Appended inside a scratch copy of canonical arch.rs; no production edits.
#[test]fn unscaled_actual_arm_oracle() {
    for &(word,displacement,expected_x,expected_mem) in UNSCALED_ORACLE {
        let base=0x40000000u64;let mut ram=[0u8;1024];ram[..4].copy_from_slice(&word.to_le_bytes());
        ram[512..520].copy_from_slice(&0x8687848582838081u64.to_le_bytes());
        let mut cpu=GuestCpuState::reset(0);cpu.pc=base;cpu.x[2]=0x1122334455667788;
        cpu.x[3]=(base+512).wrapping_sub(displacement as u64);cpu.pstate=0xb00003c5;
        assert_eq!(cpu.execute_one(&mut RamBus{ram:&mut ram,base}),StepResult::Continue);
        assert_eq!(cpu.x[2],expected_x);assert_eq!(u64::from_le_bytes(ram[512..520].try_into().unwrap()),expected_mem);
        assert_eq!((cpu.pc,cpu.x[3],cpu.pstate),(base+4,(base+512).wrapping_sub(displacement as u64),0xb00003c5));
    }
}
