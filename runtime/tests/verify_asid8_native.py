#!/usr/bin/env python3
"""Independent immutable ASID8 admission, translation and cache proof."""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--runtime',type=Path);p.add_argument('--expect-old-rejection',action='store_true');a=p.parse_args()
 tests=Path(__file__).resolve().parents[1];root=(a.runtime or tests).resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 files=[p for p in root.rglob('*') if p.is_file() and p.suffix in ('.c','.h','.inc','.rs','.py') and 'target' not in p.parts];before={str(p.relative_to(root)):sha(p) for p in files};commands=[]
 src=out/'source';shutil.copytree(root,src,ignore=shutil.ignore_patterns('target','__pycache__'))
 # Test-only visibility getter: no production algorithm or field is replaced.
 stage=src/'memory-service/src/stage1.rs';stage.write_text(stage.read_text()+"\nimpl MemoryServiceV2<'_>{pub fn test_asid8_tag(&self)->u16{self.mmu.asid}}\n")
 for n in ['test_asid8_provider.rs','test_asid8_reference.rs','test_asid8_validator.c']:shutil.copyfile(tests/n,src/n)
 provider=src/'test_stage1_provider.rs';provider.write_text(provider.read_text()+'\n'+(src/'test_asid8_provider.rs').read_text())
 arch=src/'preos/src/arch.rs';arch.write_text(arch.read_text()+'\n'+(src/'test_asid8_reference.rs').read_text())
 def run(cmd,log,allow=False,env=None):
  cmd=list(map(str,cmd));commands.append(cmd);z=subprocess.run(cmd,capture_output=True,timeout=180,env=env);(out/log).write_bytes(z.stdout+z.stderr)
  if not allow:assert z.returncode==0,(log,z.stderr[-1600:],z.stdout[-1600:])
  return z
 service=out/'libservice.rlib';run(['rustc','--edition=2021','--crate-name=nextcore_memory_service','--crate-type=rlib','-Copt-level=2',src/'memory-service/src/lib.rs','-o',service],'service.log')
 objects=[]
 for n in ['jit','arch','boot_jit','memory_boot','memory_boot_v2','memory_layout','memory_layout_v2','test_asid8_validator']:
  obj=out/(n+'.o');run(['clang-18','-std=c11','-D_GNU_SOURCE','-O2','-Wall','-Wextra','-Werror','-c',src/(n+'.c'),'-o',obj],n+'.log');objects.append(obj)
 counts={}
 for mode,flags in [('cached',[]),('uncached',['-DNEXTCORE_DISABLE_PROVIDER_CACHE']),('small-slot',['-DNEXTCORE_PROVIDER_CACHE_SLOT_BYTES=64'])]:
  if a.expect_old_rejection and mode!='cached':break
  linked=objects[:]
  if flags:
   obj=out/(mode+'.o');run(['clang-18','-std=c11','-D_GNU_SOURCE','-O2',*flags,'-c',src/'jit.c','-o',obj],mode+'-compile.log');linked[0]=obj
  exe=out/mode;run(['rustc','--edition=2021','--test','-Copt-level=2',provider,'--extern','nextcore_memory_service='+str(service),'-o',exe,*['-Clink-arg='+str(x) for x in linked]],mode+'-build.log')
  cmd=[exe,'--test-threads=1'];
  if a.expect_old_rejection:cmd+=['asid8_actual_selected_tag_cold_warm_and_native_execution']
  z=run(cmd,mode+'.log',a.expect_old_rejection,{**os.environ,'NEXTCORE_ASID8_SNAPSHOT':str(out/(mode+'.snapshot'))})
  if a.expect_old_rejection:
   assert z.returncode!=0 and b'asid8_actual_selected_tag' in z.stdout
  else:counts[mode]=int(re.search(rb'test result: ok. (\d+) passed',z.stdout)[1])
 if not a.expect_old_rejection:
  for mode in ['uncached','small-slot']:assert (out/(mode+'.snapshot')).read_bytes()==(out/'cached.snapshot').read_bytes()
  wrapper=out/'reference.rs';wrapper.write_text('\n'.join('#[path='+json.dumps(str(src/'preos/src'/(n+'.rs')))+']mod '+n+';' for n in ['mmu','pauth','exception_level','platform','arch','m1']))
  run(['rustc','--edition=2021','--test','-Copt-level=2',wrapper,'-o',out/'reference'],'reference-build.log');z=run([out/'reference','--test-threads=1'],'reference.log');counts['reference']=int(re.search(rb'test result: ok. (\d+) passed',z.stdout)[1])
 if not a.expect_old_rejection:
  mutants={}
  mmu=src/'preos/src/mmu.rs';original_mmu=mmu.read_text();original_stage=stage.read_text()
  expression='((selected>>48)&255) as u16'
  assert original_mmu.count(expression)==1
  for name in ['forced-zero','ignored-a1','va-based-tag']:
   if name=='forced-zero':mmu.write_text(original_mmu.replace(expression,'0'))
   elif name=='ignored-a1':mmu.write_text(original_mmu.replace(expression,'((ttbr0>>48)&255) as u16'))
   else:
    needle='pub fn execute(&mut self,r:&Request)->Reply {self.execute_counted(r,&mut 0)}'
    assert needle in original_stage
    stage.write_text(original_stage.replace(needle,'pub fn execute(&mut self,r:&Request)->Reply {self.mmu.asid=(((if r.address>>63!=0 {self.controls.ttbr1}else{self.controls.ttbr0})>>48)&255)as u16;self.execute_counted(r,&mut 0)}'))
   library=out/('lib'+name+'.rlib');run(['rustc','--edition=2021','--crate-name=nextcore_memory_service','--crate-type=rlib','-Copt-level=2',src/'memory-service/src/lib.rs','-o',library],name+'-service.log')
   exe=out/name;run(['rustc','--edition=2021','--test','-Copt-level=2',provider,'--extern','nextcore_memory_service='+str(library),'-o',exe,*['-Clink-arg='+str(x) for x in objects]],name+'-build.log')
   z=run([exe,'asid8_actual_selected_tag_cold_warm_and_native_execution','--test-threads=1'],name+'.log',True,{**os.environ,'NEXTCORE_ASID8_SNAPSHOT':str(out/(name+'.snapshot'))});assert z.returncode!=0 and b'assertion' in z.stdout;mutants[name]='rejected by actual selected-tag assertion'
   mmu.write_text(original_mmu);stage.write_text(original_stage)
  (out/'mutants.json').write_text(json.dumps(mutants,indent=2)+'\n')
 assert before=={str(p.relative_to(root)):sha(p) for p in files}
 receipt=dict(passed=True,expected_baseline_rejected=a.expect_old_rejection,tests=counts,source_sha256=before,sources_preserved=True,commands=commands,test_only_observability='Added getter returns actual canonical MMU ASID; no algorithm replacement',original_images_used=False,physical_boot_verified=False)
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({k:v for k,v in receipt.items() if k not in ['source_sha256','commands']}))
if __name__=='__main__':main()
