#!/usr/bin/env python3
"""APA1 non-TBI AddPAC bit63 selection; adapted-control QEMU oracle, not stock-QEMU conformance.
Requires the separately built one-APA-field QEMU 8.2.2 test model and its build receipt.
Original asymmetric TCR is restored before AUT/XPAC. No Apple inputs are used.
"""
import argparse, hashlib, json, os, pathlib, re, shutil, signal, socket, struct, subprocess, time

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--qemu',type=pathlib.Path,required=True);p.add_argument('--qemu-build-receipt',type=pathlib.Path,required=True);p.add_argument('--old-pauth',type=pathlib.Path,required=True);p.add_argument('--rustc',default='rustc');p.add_argument('--mapped-regression',action='store_true');a=p.parse_args()
 r=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
 sources=[r/'preos/src/pauth.rs',r/'test_pac_address_selection.rs',pathlib.Path(__file__).resolve()];before={str(x):sha(x) for x in sources};commands=[]
 build=json.loads(a.qemu_build_receipt.read_text());assert build['passed'] and build['only_apa_field_changed'];assert sha(a.qemu)==build['test_model']['sha256'];shutil.copy2(a.qemu_build_receipt,out/'qemu-build-receipt.json')
 oldhash=sha(a.old_pauth)
 def run(cmd,log):
  cmd=list(map(str,cmd));commands.append(cmd);z=subprocess.run(cmd,capture_output=True,timeout=180);(out/log).write_bytes(z.stdout+z.stderr);assert z.returncode==0,(log,z.stderr[-2000:]);return z.stdout
 def mov(reg,v):return [f'movz {reg},#{v&65535}']+[f'movk {reg},#{(v>>s)&65535},lsl #{s}' for s in (16,32,48)]
 def save(reg='x19'):return [f'str {reg},[x18],#8']
 lo=0x48ad369c24681357;hi=0xb752c963db97eca8;sctlr=0x30d00800|(1<<31)|(1<<30)|(1<<27)|(1<<13)
 ptrs=[0x130,0x0000800000000130,0xffff800000000130,0xffff000000000130,0x0080000000000130,0xff7f800000000130]
 keys=['APIA','APIB','APDA','APDB'];names=['pacia','pacib','pacda','pacdb'];auth=['autia','autib','autda','autdb'];rows=[]
 asm=['.arch armv8.3-a','.text','.global _start','_start:']+mov('x18',0x40100000)
 for reg in ['CurrentEL','ID_AA64ISAR1_EL1','S3_0_C0_C6_2']:asm += [f'mrs x19,{reg}']+save()
 asm+=mov('x19',48)+save()+['adr x19,vectors','msr VBAR_EL1,x19','isb']
 for key in keys:asm+=mov('x10',lo)+mov('x11',hi)+[f'msr {key}KeyLo_EL1,x10',f'msr {key}KeyHi_EL1,x11']
 for t0,t1 in [(16,17),(17,16)]:
  t=(5<<32)|(1<<30)|(2<<14)|(t1<<16)|t0
  for k in range(4):
   for ptr in ptrs:
    selected=t1 if ptr>>63 else t0;adapt=(t&~0x3f003f)|selected|(selected<<16);xpac='xpaci' if k<2 else 'xpacd'
    rows.append({'tcr':t,'adapted_tcr':adapt,'pointer':ptr,'modifier':0x9876,'key':k})
    asm+=mov('x10',t)+['msr TCR_EL1,x10']+mov('x10',sctlr)+['msr SCTLR_EL1,x10','isb','mrs x19,TCR_EL1']+save()+mov('x1',ptr)+mov('x2',0x9876)
    asm+=mov('x10',adapt)+['msr TCR_EL1,x10','isb','mrs x19,TCR_EL1']+save()+['mrs x19,SCTLR_EL1']+save()+save('x1')+save('x2')
    for part in ['Lo','Hi']:asm+=[f'mrs x19,{keys[k]}Key{part}_EL1']+save()
    asm+=['mov x3,x1',f'{names[k]} x3,x2']+mov('x10',t)+['msr TCR_EL1,x10','isb','mrs x19,TCR_EL1']+save()+['mrs x19,SCTLR_EL1']+save()+save('x1')+save('x2')
    for part in ['Lo','Hi']:asm+=[f'mrs x19,{keys[k]}Key{part}_EL1']+save()
    asm+=save('x3')+['mov x4,x3',f'{auth[k]} x4,x2']+save('x4')+['mov x4,x1',f'{xpac} x4']+save('x4')+['mov x4,x3',f'{xpac} x4']+save('x4')+['eor x4,x3,#0x0040000000000000',f'{auth[k]} x4,x2']+save('x4')+['mov x4,x1',f'{auth[k]} x4,x2']+save('x4')
 asm+=['mov x0,#0x64e','complete:b complete','.balign 2048','vectors:']
 for _ in range(16):asm+=['b trapped','.space 124']
 asm+=['trapped:','mrs x20,ESR_EL1','mrs x21,ELR_EL1','mov x0,#0xbad','b trapped']
 (out/'probe.S').write_text('\n'.join(asm)+'\n');(out/'manifest.json').write_text(json.dumps({'scope':'test-model APA1; PAC-only equal-size TCR adaptation; original controls restored before AUT/XPAC','rows':rows,'record':['original_tcr','during_tcr','during_sctlr','pointer_before','modifier_before','key_lo_before','key_hi_before','restored_tcr','restored_sctlr','pointer_after','modifier_after','key_lo_after','key_hi_after','signed','authenticated','xpac_original','xpac_signed','auth_corrupt54','auth_original']},indent=2)+'\n')
 run(['clang-18','--target=aarch64-none-elf','-nostdlib','-Wl,-Ttext=0x40080000','-Wl,-e,_start',out/'probe.S','-o',out/'probe.elf'],'arm-build.log')
 opasm=['.arch armv8.3-a','.text']
 for k in range(4):opasm += [f'{names[k]} x3,x2',f'{auth[k]} x3,x2',('xpaci' if k<2 else 'xpacd')+' x3']
 (out/'opcodes.S').write_text('\n'.join(opasm)+'\n');run(['clang-18','--target=aarch64-none-elf','-c',out/'opcodes.S','-o',out/'opcodes.o'],'opcodes-build.log');run(['llvm-objcopy-18','-O','binary','--only-section=.text',out/'opcodes.o',out/'opcodes.bin'],'opcodes-extract.log');words=struct.unpack('<12I',(out/'opcodes.bin').read_bytes())
 ep=out/'qmp.sock';cmd=[str(a.qemu),'-machine','virt,virtualization=off,secure=off','-cpu','neoverse-v1','-accel','tcg','-m','128','-display','none','-serial','none','-monitor','none','-nic','none','-qmp',f'unix:{ep},server=on,wait=off','-kernel',str(out/'probe.elf')];commands.append(cmd);start=time.monotonic();deadline=start+30
 with (out/'qemu.log').open('wb') as log:
  proc=subprocess.Popen(cmd,stdout=log,stderr=log,start_new_session=True)
  try:
   while not ep.exists() and time.monotonic()<deadline-8:
    assert proc.poll() is None;time.sleep(.05)
   with socket.socket(socket.AF_UNIX) as sock:
    sock.settimeout(2);sock.connect(str(ep));stream=sock.makefile('rwb',buffering=0);json.loads(stream.readline())
    def qmp(execute,arguments=None):
     v={'execute':execute}
     if arguments is not None:v['arguments']=arguments
     stream.write((json.dumps(v)+'\n').encode())
     while time.monotonic()<deadline-6:
      v=json.loads(stream.readline());assert 'error' not in v,v
      if 'return' in v:return v['return']
     raise TimeoutError('QMP deadline')
    qmp('qmp_capabilities')
    while time.monotonic()<deadline-8:
     regs=qmp('human-monitor-command',{'command-line':'info registers'})
     if 'X00=000000000000064e' in regs:break
     assert 'X00=0000000000000bad' not in regs,regs
     time.sleep(.05)
    else:raise TimeoutError('guest completion')
    (out/'registers.txt').write_text(regs);qmp('stop');qmp('human-monitor-command',{'command-line':f'pmemsave 0x40100000 {(4+48*19)*8} "{out / "results.bin"}"'})
  finally:
   if proc.poll() is None:os.killpg(proc.pid,signal.SIGTERM)
   try:proc.wait(timeout=3)
   except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);proc.wait(timeout=3)
 values=struct.unpack('<'+'Q'*(4+48*19),(out/'results.bin').read_bytes());assert values[:4]==(4,0x0111000001211012,0,48),values[:4]
 const='const SCTLR:u64='+str(sctlr)+';\nconst WORDS:[[u32;3];4]=['+','.join('['+','.join(map(str,words[k*3:k*3+3]))+']' for k in range(4))+'];\nconst ORACLE:&[(u64,u64,usize,[u64;6])]=&['
 for i,row in enumerate(rows):
  v=values[4+i*19:4+(i+1)*19];assert v[:13]==(row['tcr'],row['adapted_tcr'],sctlr,row['pointer'],0x9876,lo,hi,row['tcr'],sctlr,row['pointer'],0x9876,lo,hi),(i,v[:13])
  const+=f"({row['tcr']},{row['pointer']},{row['key']},["+','.join(map(str,v[13:]))+']),'
 const+='];\n';(out/'oracle.rs').write_text(const)
 template=(r/'test_pac_address_selection.rs').read_text();code=template.replace('#[path="preos/src/pauth.rs"]', '#[path='+json.dumps(str(r/'preos/src/pauth.rs'))+']').replace('// ORACLE_INSERT',const);(out/'check.rs').write_text(code)
 run([a.rustc,'--edition=2021','-Copt-level=2',out/'check.rs','-o',out/'check'],'rust-build.log');run([out/'check'],'rust-results.log')
 # Frozen pre-fix source must fail these independent expected results.
 (out/'old.rs').write_text(code.replace(json.dumps(str(r/'preos/src/pauth.rs')),json.dumps(str(a.old_pauth))))
 run([a.rustc,'--edition=2021','-Copt-level=2',out/'old.rs','-o',out/'old'],'old-build.log');negative=subprocess.run([str(out/'old')],capture_output=True,timeout=30);(out/'old-negative.log').write_bytes(negative.stdout+negative.stderr);assert negative.returncode!=0
 if a.mapped_regression:run(['python3',r/'tests/verify_mapped_native.py','--output',out/'mapped','--rustc',a.rustc],'mapped-run.log')
 assert before=={str(x):sha(x) for x in sources} and oldhash==sha(a.old_pauth)
 receipt={'passed':True,'adapted_qemu_test_model':True,'stock_qemu_or_physical_conformance':False,'rows':48,'results_compared':288,'ffi_callback_cases':288,'unsupported_rows':40,'invalid_key_indices':3,'all_control_input_key_readbacks_match':True,'old_source_negative_rejected':True,'qemu_guest_isar1':hex(values[1]),'qemu_guest_isar2':hex(values[2]),'qemu_sha256':sha(a.qemu),'qemu_process_reaped':proc.poll() is not None,'qemu_exit_code':proc.returncode,'elapsed_seconds':time.monotonic()-start,'source_hashes':before,'source_preserved':True,'old_pauth_sha256':oldhash,'commands':commands,'artifact_hashes':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'mapped_regression_requested':a.mapped_regression}
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({k:v for k,v in receipt.items() if k not in ['source_hashes','commands','artifact_hashes']}))
if __name__=='__main__':main()
