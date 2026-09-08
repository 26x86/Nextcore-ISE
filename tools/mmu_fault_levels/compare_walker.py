#!/usr/bin/env python3
"""Feed captured architectural-oracle inputs to the actual detailed Rust walker."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess

WRAPPER = r'''
use std::io::{self, BufRead};
// Minimal type adapter: mmu.rs consumes only equality with El0/El1. No walker
// logic or runtime source is copied; the actual module is imported below.
mod arch { #[derive(Clone,Copy,Debug,PartialEq,Eq)] #[repr(u8)] pub enum ExceptionLevel { El0=0,El1=1,El2=2,El3=3 } }
#[path = "@MMU_PATH@"] mod mmu;
use mmu::{VfMmu,Access,TableReadError,TranslationFailureKind};
fn main() {
 for line in io::stdin().lock().lines() {
  let line=line.unwrap();let fields:Vec<_>=line.split_whitespace().collect();let name=fields[0];
  let n:Vec<u64>=fields[1..].iter().map(|s|u64::from_str_radix(s,16).unwrap()).collect();
  let mut mmu=VfMmu::disabled();
  if !mmu.configure_tcr(n[0],n[1],n[2],0) {println!("{} CONFIG_REJECTED",name);continue}
  let access=match n[4] {0=>Access::Read,1=>Access::Write,2=>Access::Execute,_=>panic!()};
  let mut reads=0u64;
  let result=mmu.translate_detailed(n[3],access,arch::ExceptionLevel::El1,|pa| {
   reads+=1;
   for i in 0..4 {let base=n[5+i*2];if pa>=base && pa<base+16384 && pa&7==0 {
       return Ok(if pa==base {n[6+i*2]} else {0});
   }}
   Err(TableReadError::Unavailable)
  });
  match result {
   Ok(t)=>println!("{} OK {:x} {}",name,t.pa,reads),
   Err(e)=>{
    let kind=match e.kind {TranslationFailureKind::Architectural(f)=>format!("{:?}",f),TranslationFailureKind::TableRead(t)=>format!("Provider{:?}",t)};
    println!("{} {} {} {:?} {} {} {}",name,kind,e.level.map_or("none".to_string(),|v|v.to_string()),e.context,
       e.descriptor_pa.map_or("none".to_string(),|v|format!("{:x}",v)),
       e.output_pa.map_or("none".to_string(),|v|format!("{:x}",v)),reads);
   }
  }
 }
}
'''

def expected_kind(fsc):
 if fsc is None:return 'OK'
 if 0<=fsc<=3:return 'AddressSize'
 if 4<=fsc<=7:return 'Translation'
 if 8<=fsc<=11:return 'AccessFlag'
 if 12<=fsc<=15:return 'Permission'
 raise ValueError(fsc)

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--runtime-checkout',type=Path,required=True)
 parser.add_argument('--at-report',type=Path,required=True);parser.add_argument('--abort-report',type=Path,required=True)
 parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
 source=args.runtime_checkout.resolve()/'runtime/preos/src/mmu.rs';output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
 before=hashlib.sha256(source.read_bytes()).hexdigest();sources={};vectors=[];entries=[]
 for suite,path in [('at',args.at_report),('abort',args.abort_report)]:
  report=json.loads(path.read_text());assert report['passed']
  sources[suite]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'report':str(path.resolve())}
  for c,negative in [(c,False) for c in report['cases']]+[(report['negative_control'],True)]:
   name=suite+('-negative' if negative else '')+'__'+c['name'];i=c['observed_input'];keys=['ttbr0','ttbr1','tcr','va','access']+[f'{kind}{n}' for n in range(4) for kind in ['table','value']]
   vectors.append(name+' '+' '.join(format(i[key],'x') for key in keys))
   fsc=c['expected_fsc'];entry={'name':name,'oracle_fsc':fsc,'expected_kind':expected_kind(fsc),'expected_level':None if fsc is None else fsc&3,'observed_input':i,'negative_control':negative,'requires_no_table_read':c['kind'] in [7,8,10]}
   if negative:entry['mutated_oracle_fsc']=c['observed_fsc'] if suite=='at' else c['esr']&63
   if fsc is None:entry['expected_pa']=(c['par']&0x0000fffffffff000)|(i['va']&0xfff)
   entries.append(entry)
 (output/'inputs.txt').write_text('\n'.join(vectors)+'\n')
 wrapper=output/'wrapper.rs';wrapper.write_text(WRAPPER.replace('@MMU_PATH@',str(source).replace('\\','\\\\').replace('"','\\"')))
 rustc=shutil.which('rustc') or str(Path.home()/'.cargo/bin/rustc')
 compile_cmd=[rustc,'--edition=2021','-Awarnings',str(wrapper),'-o',str(output/'compare')]
 compiled=subprocess.run(compile_cmd,text=True,capture_output=True,check=True)
 result=subprocess.run([str(output/'compare')],input='\n'.join(vectors)+'\n',text=True,capture_output=True,check=True)
 (output/'stdout.log').write_text(result.stdout);rows={x.split()[0]:x.split()[1:] for x in result.stdout.splitlines()}
 for e in entries:
  row=rows[e['name']];e['observed_kind']=row[0]
  if row[0]=='OK':e['observed_pa']=int(row[1],16);e['reads']=int(row[2]);e['passed']=e['expected_kind']=='OK' and e['observed_pa']==e['expected_pa']
  elif row[0]=='CONFIG_REJECTED':e['passed']=False
  else:
   e.update(observed_level=None if row[1]=='none' else int(row[1]),context=row[2],descriptor_pa=row[3],output_pa=row[4],reads=int(row[5]))
   e['passed']=e['observed_kind']==e['expected_kind'] and e['observed_level']==e['expected_level']
  if e['requires_no_table_read']:e['passed']=e['passed'] and e.get('reads')==0
 after=hashlib.sha256(source.read_bytes()).hexdigest()
 positive=[e for e in entries if not e['negative_control']];negative=[e for e in entries if e['negative_control']]
 for e in negative:
  fsc=e['mutated_oracle_fsc'];e['detected']=not e['passed'] and e['observed_kind']==expected_kind(fsc) and e.get('observed_level')==fsc&3
 report={'schema':'nextcore.mmu-walker-oracle-differential.v1','case_count':len(positive),'cases':positive,'negative_controls':negative,'negative_controls_detected':all(e['detected'] for e in negative),'passed':before==after and all(e['passed'] for e in positive) and all(e['detected'] for e in negative),'walker_source_sha256_before':before,'walker_source_sha256_after':after,'walker_path':str(source),'oracle_inputs':sources,'compile_command':compile_cmd,'rustc_version':subprocess.check_output([rustc,'--version'],text=True).strip(),'wrapper_sha256':hashlib.sha256(wrapper.read_bytes()).hexdigest(),'native_jit_mmu_verified':False}
 (output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({'passed':report['passed'],'case_count':len(positive),'negative_controls_detected':report['negative_controls_detected'],'failures':[{k:e.get(k) for k in ['name','expected_kind','expected_level','observed_kind','observed_level']} for e in positive if not e['passed']]}))
 return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
