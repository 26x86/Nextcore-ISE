#!/usr/bin/env python3
"""Authored unscaled scalar actual native, Arm oracle and canonical memory proof."""
import argparse,hashlib,json,pathlib,re,shutil,socket,struct,subprocess,time,os,signal

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--qemu',default='qemu-system-aarch64');a=p.parse_args()
 r=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 files=sorted(x for x in r.rglob('*') if x.is_file() and x.suffix in ('.c','.h','.inc','.rs','.py') and 'target' not in x.parts)
 sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest();before={str(x.relative_to(r)):sha(x) for x in files};commands=[]
 def run(cmd,log):
  z=subprocess.run(list(map(str,cmd)),capture_output=True,timeout=180);(out/log).write_bytes(z.stdout+z.stderr);commands.append(list(map(str,cmd)));assert z.returncode==0,(log,z.stdout[-1200:],z.stderr[-1200:]);return z.stdout
 forms=[('sturb','w',0,0),('ldurb','w',0,1),('ldursb','x',0,2),('ldursb','w',0,3),('sturh','w',1,0),('ldurh','w',1,1),('ldursh','x',1,2),('ldursh','w',1,3),('stur','w',2,0),('ldur','w',2,1),('ldursw','x',2,2),('stur','x',3,0),('ldur','x',3,1)]
 asm=['.text','.global _start','_start:','mov x18,#0x40100000','mov x19,#0x40110000'];vectors=[]
 for mnemonic,reg,size,opc in forms:
  for disp in [-256,-1,255]:
   word=0x38000000|size<<30|opc<<22|(disp&511)<<12|3<<5|2
   asm+=['mov x2,#0x7788','movk x2,#0x5566,lsl #16','movk x2,#0x3344,lsl #32','movk x2,#0x1122,lsl #48','mov x4,#0x8081','movk x4,#0x8283,lsl #16','movk x4,#0x8485,lsl #32','movk x4,#0x8687,lsl #48','str x4,[x19]',f"{'sub' if disp>0 else 'add'} x3,x19,#{abs(disp)}",f'{mnemonic} {reg}2,[x3,#{disp}]','str x2,[x18],#8','ldr x4,[x19]','str x4,[x18],#8','sub x4,x3,x19','str x4,[x18],#8']
   vectors.append((word,disp))
 asm+=['mov x0,#0x64e','1:b 1b'];(out/'oracle.S').write_text('\n'.join(asm)+'\n')
 run(['clang','--target=aarch64-none-elf','-nostdlib','-Wl,-Ttext=0x40080000','-Wl,-e,_start',out/'oracle.S','-o',out/'oracle.elf'],'oracle-build.log')
 # Verify assembler words independently rather than assuming the hand decoder.
 run(['llvm-objcopy-18','-O','binary',out/'oracle.elf',out/'oracle.bin'],'oracle-extract.log')
 for word,_ in vectors:assert struct.pack('<I',word) in (out/'oracle.bin').read_bytes()
 qemu=shutil.which(a.qemu);assert qemu;ep=out/'qmp.sock';cmd=[qemu,'-machine','virt,virtualization=off,secure=off','-cpu','max','-m','128','-display','none','-serial','none','-monitor','none','-nic','none','-qmp',f'unix:{ep},server=on,wait=off','-kernel',str(out/'oracle.elf')];commands.append(cmd)
 with (out/'qemu.log').open('wb') as log:
  proc=subprocess.Popen(cmd,stdout=log,stderr=log,start_new_session=True)
  try:
   deadline=time.monotonic()+20
   while not ep.exists() and time.monotonic()<deadline:time.sleep(.05)
   with socket.socket(socket.AF_UNIX) as sock:
    sock.settimeout(5);sock.connect(str(ep));stream=sock.makefile('rwb',buffering=0);json.loads(stream.readline())
    def qmp(execute,arguments=None):
     v={'execute':execute}
     if arguments is not None:v['arguments']=arguments
     stream.write((json.dumps(v)+'\n').encode())
     while True:
      v=json.loads(stream.readline());assert 'error' not in v,v
      if 'return' in v:return v['return']
    qmp('qmp_capabilities')
    while time.monotonic()<deadline:
     regs=qmp('human-monitor-command',{'command-line':'info registers'})
     if 'X00=000000000000064e' in regs:break
     time.sleep(.05)
    else:raise AssertionError(regs)
    qmp('stop');qmp('human-monitor-command',{'command-line':f'pmemsave 0x40100000 {len(vectors)*24} "{out / "oracle-results.bin"}"'})
  finally:
   if proc.poll() is None:os.killpg(proc.pid,signal.SIGTERM)
   try:proc.wait(timeout=5)
   except subprocess.TimeoutExpired:
    os.killpg(proc.pid,signal.SIGKILL);proc.wait(timeout=5)
 values=struct.unpack('<'+'Q'*len(vectors)*3,(out/'oracle-results.bin').read_bytes());header=[]
 for i,(word,disp) in enumerate(vectors):
  x,mem,base=values[i*3:i*3+3];assert base==(-disp)&((1<<64)-1)
  header.append(f'{{0x{word:x}u,{disp},0x{x:x}ULL,0x{mem:x}ULL}}')
 (out/'oracle.h').write_text('static const struct {unsigned word;int disp;unsigned long long x,mem;} oracle[]={'+','.join(header)+'};\n')
 objects=[]
 for name in ['jit','arch','boot_jit','memory_boot','memory_boot_v2','memory_layout','memory_layout_v2']:
  obj=out/(name+'.o');run(['clang','-std=c11','-D_GNU_SOURCE','-O2','-Wall','-Wextra','-Werror','-c',r/(name+'.c'),'-o',obj],name+'.log');objects.append(obj)
 c=(r/'test_unscaled_jit.c').read_text();needle='    CHECK(munmap(code.bytes,4096)==0);'
 proof='    for(unsigned i=0;i<sizeof(oracle)/sizeof(oracle[0]);i++) {\n        vf_cpu cpu;reset(&cpu);cpu.x[2]=0x1122334455667788ULL;cpu.x[3]=BASE+512-oracle[i].disp;\n        uint8_t ram[1024]={0};uint64_t seed=0x8687848582838081ULL;memcpy(ram+512,&seed,8);\n        CHECK(run(&cpu,&code,oracle[i].word,ram,sizeof(ram))==VF_BUDGET);\n        uint64_t mem;memcpy(&mem,ram+512,8);CHECK(cpu.x[2]==oracle[i].x && mem==oracle[i].mem);\n        CHECK(cpu.x[3]==BASE+512-oracle[i].disp && cpu.retired==1);\n    }\n'
 (out/'native.c').write_text('#include "oracle.h"\n'+c.replace(needle,proof+needle))
 run(['clang','-std=c11','-D_GNU_SOURCE','-O2','-I',r,out/'native.c',*objects,'-o',out/'native'],'native-build.log');run([out/'native'],'native.log')
 service=out/'libservice.rlib';run(['rustc','--edition=2021','--crate-name=nextcore_memory_service','--crate-type=rlib','-Copt-level=2',r/'memory-service/src/lib.rs','-o',service],'service.log')
 base=(r/'test_stage1_provider.rs').read_text();base=re.sub(r'#\[path="([^"]+)"\]',lambda m:'#[path='+json.dumps(str(r/m[1]))+']',base)
 extra=(r/'test_unscaled_provider.rs').read_text()
 (out/'provider.rs').write_text(base+'\n'+extra)
 run(['rustc','--edition=2021','--test','-Copt-level=2',out/'provider.rs','--extern','nextcore_memory_service='+str(service),'-o',out/'provider',*['-Clink-arg='+str(x) for x in objects]],'provider-build.log');run([out/'provider','--test-threads=1'],'provider.log')
 oracle_rust='const UNSCALED_ORACLE:&[(u32,i64,u64,u64)]=&['+','.join(f'({word},{disp},{values[i*3]},{values[i*3+1]})' for i,(word,disp) in enumerate(vectors))+'];\n'
 (out/'arch-reference.rs').write_text((r/'preos/src/arch.rs').read_text().split('#[cfg(test)]',1)[0]+'\n'+oracle_rust+(r/'test_unscaled_reference.rs').read_text())
 wrapper='\n'.join('#[path='+json.dumps(str(r/'preos/src'/(n+'.rs')))+']mod '+n+';' for n in ['mmu','pauth','exception_level','platform'])+'\n#[path="arch-reference.rs"]mod arch;\n'
 (out/'reference.rs').write_text(wrapper)
 run(['rustc','--edition=2021','--test','-Copt-level=2',out/'reference.rs','-o',out/'reference'],'reference-build.log');run([out/'reference','unscaled_actual_arm_oracle'],'reference.log')
 for mode,flags in [('uncached',['-DNEXTCORE_DISABLE_PROVIDER_CACHE']),('small-slot',['-DNEXTCORE_PROVIDER_CACHE_SLOT_BYTES=64'])]:
  obj=out/(mode+'.o');run(['clang','-std=c11','-D_GNU_SOURCE','-O2',*flags,'-c',r/'jit.c','-o',obj],mode+'-build.log')
  run(['rustc','--edition=2021','--test','-Copt-level=2',out/'provider.rs','--extern','nextcore_memory_service='+str(service),'-o',out/mode,'-Clink-arg='+str(obj),*['-Clink-arg='+str(x) for x in objects[1:]]],mode+'-link.log');run([out/mode,'--test-threads=1'],mode+'.log')
 assert before=={str(x.relative_to(r)):sha(x) for x in files}
 receipt=dict(passed=True,forms=13,native_modes=['cached','uncached','small-slot'],reference_oracle_vectors=39,process_reaped=True,oracle_vectors=len(vectors),qemu_sha256=sha(pathlib.Path(qemu)),qemu_version=subprocess.check_output([qemu,'--version'],text=True).splitlines()[0],source_sha256=before,sources_preserved=True,commands=commands,original_images_used=False,physical_boot_verified=False)
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({'passed':True,'receipt':str(out/'receipt.json')}))
if __name__=='__main__':main()
