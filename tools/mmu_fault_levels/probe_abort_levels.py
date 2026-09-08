#!/usr/bin/env python3
"""Execute authored EL1 accesses and compare exact ESR/FAR/ELR fault metadata."""
from pathlib import Path
import argparse,hashlib,json,subprocess

def cases():
 result=[]
 def add(g,tsz,start,level,kind,fsc,access=0):
  result.append(dict(name=f'{g}_L{level}_kind{kind}_access{access}',granule=g,tsz=tsz,upper=1,start=start,level=level,kind=kind,write=access,ips=0,expected_fsc=fsc))
 for g,tsz,start in [(4096,16,0),(16384,17,1)]:
  for level in range(start,4):
   add(g,tsz,start,level,0,4+level)
   if level<3:add(g,tsz,start,level,6,level)
   if level in ([0,3] if g==4096 else [1,3]):add(g,tsz,start,level,2,4+level)
  for level in ([1,2,3] if g==4096 else [2,3]):
   add(g,tsz,start,level,3,8+level)
   add(g,tsz,start,level,4,12+level,1)
   add(g,tsz,start,level,5,level)
   add(g,tsz,start,level,11,12+level,2)
  add(g,tsz,start,3,7,0)
  add(g,tsz,start,3,8,4)
 return result

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
 output=args.output.resolve();output.mkdir(parents=True,exist_ok=False);source=Path(__file__).resolve().parent;cs=cases()
 header='static const struct test_case cases[]={\n'+''.join('{"'+c['name']+'",'+','.join(str(c[k]) for k in ['granule','tsz','upper','start','level','kind','write','ips'])+'},\n' for c in cs)+'};\n'
 (output/'abort_cases.h').write_text(header);commands=[];executables={};regimes={}
 source_paths=[source/name for name in ['abort_start.S','abort_oracle.c','oracle.ld','probe_abort_levels.py']]
 before={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths}
 def run(argv):
  p=subprocess.run(argv,text=True,capture_output=True,timeout=30);commands.append(dict(argv=argv,returncode=p.returncode,stdout=p.stdout,stderr=p.stderr))
  if p.returncode:raise RuntimeError(json.dumps(commands[-1]))
  return p.stdout+p.stderr
 common=['clang','--target=aarch64-none-elf','-ffreestanding','-fno-builtin','-mgeneral-regs-only','-O2','-I',str(output)]
 run(common+['-c',str(source/'abort_start.S'),'-o',str(output/'start.o')]);outcomes={}
 for variant in ['normal','negative']:
  run(common+(['-DNEGATIVE_CONTROL'] if variant=='negative' else [])+['-c',str(source/'abort_oracle.c'),'-o',str(output/(variant+'.o'))])
  run(['ld.lld','-T',str(source/'oracle.ld'),str(output/'start.o'),str(output/(variant+'.o')),'-o',str(output/(variant+'.elf'))])
  executables[variant]=hashlib.sha256((output/(variant+'.elf')).read_bytes()).hexdigest()
  text=run(['qemu-system-aarch64','-machine','virt,virtualization=on','-cpu','max','-m','128','-nographic','-monitor','none','-serial','none','-net','none','-semihosting-config','enable=on,target=native','-kernel',str(output/(variant+'.elf'))]);(output/(variant+'.log')).write_text(text)
  records={fields[0]:[int(x,16) for x in fields[1:]] for fields in [line.split() for line in text.splitlines()]};observed=[]
  regimes[variant]={key:records[key][0] for key in ['HCR','CURRENT_EL']}
  for c in cs:
   esr,far,elr,taken,va,expected_elr=records[c['name']];ec=0x21 if c['write']==2 else 0x25
   checks={'exception_taken':taken==1,'fsc':esr&63==c['expected_fsc'],'ec':esr>>26==ec,'il':bool(esr&(1<<25)),'far':far==va,'elr':elr==expected_elr}
   if c['write']!=2:checks['write_direction']=((esr>>6)&1)==c['write']
   keys=['ttbr0','ttbr1','tcr','va','access']+[f'{kind}{i}' for i in range(4) for kind in ['table','value']]
   captured={key:records[c['name']+'.'+key][0] for key in keys}
   observed.append(dict(**c,esr=esr,far=far,elr=elr,observed_input=captured,checks=checks,passed=all(checks.values())))
  outcomes[variant]=observed
 report={'schema':'nextcore.mmu-abort-level-oracle.v1','cases':outcomes['normal'],'case_count':len(cs),'passed':all(c['passed'] for c in outcomes['normal']) and not outcomes['negative'][0]['passed'],'negative_control_detected':not outcomes['negative'][0]['passed'],'negative_control':outcomes['negative'][0],'commands':commands,'apple_assets_used':False,'native_jit_mmu_verified':False}
 report['source_sha256_before']=before;report['source_sha256_after']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths}
 report['executable_sha256']=executables;report['observed_regimes']=regimes;report['qemu_version']=run(['qemu-system-aarch64','--version']).splitlines()[0]
 report['passed']=report['passed'] and report['source_sha256_before']==report['source_sha256_after'] and all(r['HCR']==1<<31 and r['CURRENT_EL']==8 for r in regimes.values())
 (output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'passed':report['passed'],'case_count':len(cs),'failures':[(c['name'],c['checks']) for c in outcomes['normal'] if not c['passed']]}));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
