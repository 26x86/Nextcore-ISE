#!/usr/bin/env python3
"""Authored variable_shift scalar actual native, Arm oracle and canonical memory proof."""
import argparse,hashlib,json,pathlib,re,shutil,socket,struct,subprocess,time,os,signal

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--qemu',default='qemu-system-aarch64');a=p.parse_args()
 r=pathlib.Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 files=sorted(x for x in r.rglob('*') if x.is_file() and x.suffix in ('.c','.h','.inc','.rs','.py') and 'target' not in x.parts)
 sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest();before={str(x.relative_to(r)):sha(x) for x in files};commands=[]
 def run(cmd,log):
  z=subprocess.run(list(map(str,cmd)),capture_output=True,timeout=180);(out/log).write_bytes(z.stdout+z.stderr);commands.append(list(map(str,cmd)));assert z.returncode==0,(log,z.stdout[-1200:],z.stderr[-1200:]);return z.stdout
 def load(reg,value):return [f'movz {reg},#{value&65535}']+[f'movk {reg},#{(value>>shift)&65535},lsl #{shift}' for shift in [16,32,48]]
 asm=['.text','.global _start','_start:','mov x18,#0x40100000'];vectors=[]
 names=['lslv','lsrv','asrv','rorv']
 for width in [32,64]:
  reg='w' if width==32 else 'x'
  for op,name in enumerate(names):
   counts=list(range(width))+[width,width+1,2*width-1,0x8000000000000041,0xffffffffffffffff]
   cases=[(0,1,2,a0,count) for count in counts for a0 in [0x91a2b3c4d5e6f780,0,0xffffffffffffffff,0x8000000080000001]]
   cases += [(rn,rm,rd,0x91a2b3c4d5e6f780,count) for rn,rm,rd in [(0,1,0),(0,1,1),(0,0,0),(31,1,2),(0,31,2),(0,1,31),(31,31,31)] for count in [0,width,width+1,0xffffffffffffffff]]
   for rn,rm,rd,a0,count in cases:
    word=0x1ac02000|(int(width==64)<<31)|(op<<10)|(rm<<16)|(rn<<5)|rd
    operand=lambda n: reg+str(n) if n!=31 else reg+'zr'
    asm+=load('x0',a0)+load('x1',count)+load('x2',0xface)+load('x4',0xf0000000)+['msr nzcv,x4',f'{name} {operand(rd)},{operand(rn)},{operand(rm)}',f'mov x4,{"x"+str(rd) if rd!=31 else "xzr"}','str x4,[x18],#8','mrs x4,nzcv','str x4,[x18],#8']
    vectors.append((word,a0,count))
 asm+=['mov x0,#0x64e','1:b 1b'];(out/'oracle.S').write_text('\n'.join(asm)+'\n')
 run(['clang','--target=aarch64-none-elf','-nostdlib','-Wl,-Ttext=0x40080000','-Wl,-e,_start',out/'oracle.S','-o',out/'oracle.elf'],'oracle-build.log')
 run(['llvm-objcopy-18','-O','binary',out/'oracle.elf',out/'oracle.bin'],'oracle-extract.log')
 for word,_,_ in vectors:assert struct.pack('<I',word) in (out/'oracle.bin').read_bytes()
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
    qmp('stop');qmp('human-monitor-command',{'command-line':f'pmemsave 0x40100000 {len(vectors)*16} "{out / "oracle-results.bin"}"'})
  finally:
   if proc.poll() is None:os.killpg(proc.pid,signal.SIGTERM)
   try:proc.wait(timeout=5)
   except subprocess.TimeoutExpired:
    os.killpg(proc.pid,signal.SIGKILL);proc.wait(timeout=5)
 values=struct.unpack('<'+'Q'*len(vectors)*2,(out/'oracle-results.bin').read_bytes());header=[]
 for i,(word,a0,b0) in enumerate(vectors):
  value,nzcv=values[i*2:i*2+2]
  header.append(f'{{0x{word:x}u,0x{a0:x}ULL,0x{b0:x}ULL,0x{value:x}ULL,0x{nzcv:x}ULL}}')
 (out/'oracle.h').write_text('static const struct {unsigned word;unsigned long long a,b,value,nzcv;} oracle[]={'+','.join(header)+'};\n')
 objects=[]
 for name in ['jit','arch','boot_jit','memory_boot','memory_boot_v2','memory_layout','memory_layout_v2']:
  obj=out/(name+'.o');run(['clang','-std=c11','-D_GNU_SOURCE','-O2','-Wall','-Wextra','-Werror','-c',r/(name+'.c'),'-o',obj],name+'.log');objects.append(obj)
 c=(r/'test_variable_shift_jit.c').read_text()
 proof="""for(unsigned i=0;i<sizeof(oracle)/sizeof(oracle[0]);i++) {
 vf_cpu cpu;vf_cpu_reset(&cpu,VF_EL1);cpu.x[0]=oracle[i].a;cpu.x[1]=oracle[i].b;cpu.x[2]=0xface;cpu.pstate=0xf00003c5;cpu.sp=0x12345678;uint8_t ram[8]={0};unsigned w=oracle[i].word;
 CHECK(vf_run(&cpu,(uint8_t*)&w,4,ram,8,&code,1,perms,0)==VF_BUDGET);
 unsigned rd=w&31;uint64_t expected_regs[3]={oracle[i].a,oracle[i].b,0xface};if(rd!=31)expected_regs[rd]=oracle[i].value;
 CHECK(!memcmp(cpu.x,expected_regs,sizeof(expected_regs)) && cpu.pstate==(oracle[i].nzcv|0x3c5) && cpu.sp==0x12345678 && cpu.pc==4 && cpu.retired==1);
 }"""
 (out/'native.c').write_text('#include "oracle.h"\n'+c.replace('/* ORACLE_INSERT */',proof))
 run(['clang','-std=c11','-D_GNU_SOURCE','-O2','-I',r,out/'native.c',*objects,'-o',out/'native'],'native-build.log');run([out/'native'],'native.log')
 oracle_rust='const VARIABLE_SHIFT_ORACLE:&[(u32,u64,u64,u64,u64)]=&['+','.join(f'({word},{a0},{b0},{values[i*2]},{values[i*2+1]})' for i,(word,a0,b0) in enumerate(vectors))+'];\n'
 service=out/'libservice.rlib';run(['rustc','--edition=2021','--crate-name=nextcore_memory_service','--crate-type=rlib','-Copt-level=2',r/'memory-service/src/lib.rs','-o',service],'service.log')
 base=(r/'test_stage1_provider.rs').read_text();base=re.sub(r'#\[path="([^"]+)"\]',lambda m:'#[path='+json.dumps(str(r/m[1]))+']',base)
 extra=(r/'test_variable_shift_provider.rs').read_text()
 (out/'provider.rs').write_text(base+'\n'+oracle_rust+extra)
 run(['rustc','--edition=2021','--test','-Copt-level=2',out/'provider.rs','--extern','nextcore_memory_service='+str(service),'-o',out/'provider',*['-Clink-arg='+str(x) for x in objects]],'provider-build.log');os.environ['NEXTCORE_VARIABLE_SHIFT_SNAPSHOT']=str(out/'cached.snapshot');run([out/'provider','--test-threads=1'],'provider.log')
 (out/'arch-reference.rs').write_text((r/'preos/src/arch.rs').read_text().split('#[cfg(test)]',1)[0]+'\n'+oracle_rust+(r/'test_variable_shift_reference.rs').read_text())
 wrapper='\n'.join('#[path='+json.dumps(str(r/'preos/src'/(n+'.rs')))+']mod '+n+';' for n in ['mmu','pauth','exception_level','platform'])+'\n#[path="arch-reference.rs"]mod arch;\n'
 (out/'reference.rs').write_text(wrapper)
 run(['rustc','--edition=2021','--test','-Copt-level=2',out/'reference.rs','-o',out/'reference'],'reference-build.log');run([out/'reference','--test-threads=1'],'reference.log')
 for mode,flags in [('uncached',['-DNEXTCORE_DISABLE_PROVIDER_CACHE']),('small-slot',['-DNEXTCORE_PROVIDER_CACHE_SLOT_BYTES=64'])]:
  obj=out/(mode+'.o');run(['clang','-std=c11','-D_GNU_SOURCE','-O2',*flags,'-c',r/'jit.c','-o',obj],mode+'-build.log')
  run(['rustc','--edition=2021','--test','-Copt-level=2',out/'provider.rs','--extern','nextcore_memory_service='+str(service),'-o',out/mode,'-Clink-arg='+str(obj),*['-Clink-arg='+str(x) for x in objects[1:]]],mode+'-link.log');os.environ['NEXTCORE_VARIABLE_SHIFT_SNAPSHOT']=str(out/(mode+'.snapshot'));run([out/mode,'--test-threads=1'],mode+'.log');assert (out/(mode+'.snapshot')).read_bytes()==(out/'cached.snapshot').read_bytes()
 assert before=={str(x.relative_to(r)):sha(x) for x in files}
 receipt=dict(passed=True,operations=4,widths=[32,64],count_residues_exhaustive=True,actual_arm_aliases_and_zr=True,exposed_result_requests_ram_equal=True,native_modes=['cached','uncached','small-slot'],reference_oracle_vectors=len(vectors),process_reaped=True,oracle_vectors=len(vectors),qemu_sha256=sha(pathlib.Path(qemu)),qemu_version=subprocess.check_output([qemu,'--version'],text=True).splitlines()[0],source_sha256=before,sources_preserved=True,commands=commands,original_images_used=False,physical_boot_verified=False)
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({'passed':True,'receipt':str(out/'receipt.json')}))
if __name__=='__main__':main()
