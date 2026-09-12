#!/usr/bin/env python3
"""Validate bounded optional-table-control rejection; no architectural universal trap claim."""
import argparse,hashlib,json,os,re,shutil,subprocess,signal,time
from types import SimpleNamespace
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--runtime',type=Path);p.add_argument('--expect-old-rejection',action='store_true');a=p.parse_args()
 tests=Path(__file__).resolve().parents[1];root=(a.runtime or tests).resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);src=out/'source';shutil.copytree(root,src,ignore=shutil.ignore_patterns('target','__pycache__'))
 digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 consumed=[p for p in root.iterdir() if p.is_file() and p.suffix in('.c','.h','.inc')]+list((root/'preos/src').glob('*.rs'))+list((root/'memory-service/src').glob('*.rs'))
 before={str(p.relative_to(root)):digest(p) for p in consumed};testnames=['test_optional_tcr.c','test_optional_tcr.rs','tests/verify_optional_tcr.py'];testhash={n:digest(tests/n) for n in testnames}
 for n in testnames:shutil.copyfile(tests/n,src/n)
 commands=[];results={}
 def run(cmd,name,expected_failure=False):
  cmd=list(map(str,cmd));start=time.monotonic();deadline=start+120;timed_out=False
  child=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
  def alive():
   try:os.killpg(child.pid,0);return True
   except ProcessLookupError:return False
  try:
   stdout,stderr=child.communicate(timeout=114)
  except subprocess.TimeoutExpired:
   timed_out=True
   if alive():os.killpg(child.pid,signal.SIGTERM)
   try:stdout,stderr=child.communicate(timeout=2)
   except subprocess.TimeoutExpired:
    if alive():os.killpg(child.pid,signal.SIGKILL)
    stdout,stderr=child.communicate(timeout=2)
  finally:
   if alive():
    os.killpg(child.pid,signal.SIGTERM)
    until=min(deadline-1,time.monotonic()+1)
    while alive() and time.monotonic()<until:time.sleep(.02)
    if alive():os.killpg(child.pid,signal.SIGKILL)
    while alive() and time.monotonic()<deadline:time.sleep(.02)
  z=SimpleNamespace(stdout=stdout,stderr=stderr,returncode=child.returncode)
  (out/(name+'.log')).write_bytes(stdout+stderr)
  commands.append(dict(argv=cmd,return_code=z.returncode,log=name+'.log',timed_out=timed_out,reaped=child.poll()is not None,process_group_exited=not alive(),elapsed_seconds=time.monotonic()-start))
  (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  assert not timed_out and child.poll()is not None and not alive(),commands[-1]
  assert (z.returncode!=0)==expected_failure,(name,z.returncode,z.stderr[-2000:],z.stdout[-1200:]);return z
 run(['clang-18','-std=c11','-D_GNU_SOURCE','-O2','-Wall','-Wextra','-Werror',src/'test_optional_tcr.c',src/'arch.c','-o',out/'bank-c'],'c-build')
 z=run([out/'bank-c'],'c-run',a.expect_old_rejection);results['c']=json.loads(z.stdout)
 for mode in ['bank','mmu']:
  copy=out/mode;shutil.copytree(src/'preos/src',copy);target=copy/('arch.rs'if mode=='bank'else'mmu.rs');target.write_bytes(target.read_bytes()+b'\n'+(src/'test_optional_tcr.rs').read_bytes())
  wrapper=out/(mode+'.rs');wrapper.write_text('\n'.join('#[path='+json.dumps(str(copy/(n+'.rs')))+']mod '+n+';' for n in ['mmu','pauth','exception_level','platform','arch','m1']))
  cmd=['rustc','--edition=2021','--test','-Copt-level=2',wrapper,'-o',out/('rust-'+mode)]
  if mode=='mmu':cmd+=['--cfg','optional_tcr_mmu']
  run(cmd,mode+'-build');cmd=[out/('rust-'+mode),'--test-threads=1']
  if a.expect_old_rejection:cmd+=['optional_tcr_']
  z=run(cmd,mode+'-run',a.expect_old_rejection)
  if a.expect_old_rejection:assert b'assertion' in z.stdout;results[mode]='old acceptance bug detected'
  else:results[mode]=int(re.search(rb'test result: ok. (\d+) passed',z.stdout)[1])
 lib=out/'libservice.rlib';run(['rustc','--edition=2021','--crate-type=rlib','--crate-name=nextcore_memory_service','-Copt-level=2',src/'memory-service/src/lib.rs','-o',lib],'service-build')
 run(['rustc','--edition=2021','--test','--cfg','optional_tcr_service',src/'test_optional_tcr.rs','--extern','nextcore_memory_service='+str(lib),'-o',out/'service'],'service-test-build');run([out/'service'],'service-run');results['service']='all15combinations rejected by profiles1/3 and dynamic, bothgranules; zero accepted'
 assert before=={str(p.relative_to(root)):digest(p) for p in consumed};assert testhash=={n:digest(tests/n) for n in testnames}
 receipt=dict(passed=True,expected_old_acceptance_bug=a.expect_old_rejection,results=results,source_sha256=before,test_sha256=testhash,sources_preserved=True,commands=commands,scope='Bounded unsupported HA/HD/HPD0/HPD1 policy. Native C full CPU byte preservation, Rust bank fields and warm cache, direct MMU configuration state, immutable/dynamic control admission. No MMFR1 implementation or architectural universal write-trap claim.',original_inputs_used=False,physical_boot_verified=False)
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(dict(passed=True,results=results,old=a.expect_old_rejection)))
if __name__=='__main__':main()
